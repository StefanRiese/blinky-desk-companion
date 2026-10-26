// Package protocol implements the Blinky NDJSON protocol (see protocol/PROTOCOL.md).
package protocol

import (
	"bufio"
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"sync"
)

// Version is the protocol major version sent in hello.
const Version = 1

// MaxLine is the maximum length of one message line in bytes (without the newline).
const MaxLine = 2048

// ErrTooLong is returned by Writer.Write for messages longer than MaxLine.
var ErrTooLong = errors.New("protocol: message exceeds 2048 bytes")

// Envelope holds the common fields of every message plus the raw line.
type Envelope struct {
	T   string `json:"t"`
	ID  *int   `json:"id,omitempty"`
	V   int    `json:"v,omitempty"`
	Raw []byte `json:"-"`
}

// Decode parses the envelope of one line and keeps the raw bytes for typed decoding.
func Decode(line []byte) (Envelope, error) {
	var e Envelope
	if err := json.Unmarshal(line, &e); err != nil {
		return e, err
	}
	if e.T == "" {
		return e, errors.New("protocol: missing field t")
	}
	e.Raw = append([]byte(nil), line...)
	return e, nil
}

// As decodes the raw line into a typed message.
func (e Envelope) As(v any) error { return json.Unmarshal(e.Raw, v) }

// Hello is sent by both sides. Device fields: Dev, FW, Board, Caps, Token.
// Host fields: Host, Ver, Net, Srcs, and Dev when it answers a device hello.
type Hello struct {
	T     string   `json:"t"`
	V     int      `json:"v"`
	Dev   string   `json:"dev,omitempty"`
	FW    string   `json:"fw,omitempty"`
	Board string   `json:"board,omitempty"`
	Caps  []string `json:"caps,omitempty"`
	Token string   `json:"token,omitempty"`
	Host  string   `json:"host,omitempty"`
	Ver   string   `json:"ver,omitempty"`
	Net   *bool    `json:"net,omitempty"`
	Srcs  []string `json:"srcs,omitempty"`
	Role  string   `json:"role,omitempty"` // host hello: "setup" for a setup page (config only, no sources)
}

// Sub subscribes to a host source.
type Sub struct {
	T     string          `json:"t"`
	ID    *int            `json:"id,omitempty"`
	SID   string          `json:"sid"`
	Src   string          `json:"src"`
	Every int             `json:"every,omitempty"`
	P     json.RawMessage `json:"p,omitempty"`
}

// Unsub ends a subscription.
type Unsub struct {
	T   string `json:"t"`
	SID string `json:"sid"`
}

// Ack confirms a request. Every is set for sub (effective interval).
type Ack struct {
	T     string `json:"t"`
	ID    int    `json:"id"`
	Every int    `json:"every,omitempty"`
}

// Err reports a failed request or a protocol problem.
type Err struct {
	T    string `json:"t"`
	ID   *int   `json:"id,omitempty"`
	Code string `json:"code"`
	Msg  string `json:"msg,omitempty"`
}

// SubState reports the health of a subscription.
type SubState struct {
	T     string `json:"t"`
	SID   string `json:"sid"`
	State string `json:"state"` // ok | error | backoff
	Msg   string `json:"msg,omitempty"`
	Retry int    `json:"retry,omitempty"`
}

// HostState reports a change of the host's internet state.
type HostState struct {
	T   string `json:"t"`
	Net bool   `json:"net"`
}

// Time carries the current Unix time. The device applies its own time zone.
type Time struct {
	T     string `json:"t"`
	Epoch int64  `json:"epoch"`
}

// WeatherNow is the current weather block.
type WeatherNow struct {
	Temp  float64 `json:"temp"`
	Feels float64 `json:"feels"`
	Hum   float64 `json:"hum"`
	Code  int     `json:"code"`
	Wind  float64 `json:"wind"`
	IsDay int     `json:"is_day"`
}

// WeatherDay is one forecast day.
type WeatherDay struct {
	D       string  `json:"d"`
	Min     float64 `json:"min"`
	Max     float64 `json:"max"`
	Code    int     `json:"code"`
	Pop     int     `json:"pop"`
	Sunrise string  `json:"sunrise"`
	Sunset  string  `json:"sunset"`
}

// Weather is the weather message (WMO codes, °C, km/h, mm).
type Weather struct {
	T          string       `json:"t"`
	TTL        int          `json:"ttl,omitempty"`
	Now        WeatherNow   `json:"now"`
	RainNext2h []float64    `json:"rain_next_2h,omitempty"`
	Days       []WeatherDay `json:"days,omitempty"`
}

// Data carries generic key/values or key deletions.
type Data struct {
	T   string         `json:"t"`
	TTL int            `json:"ttl,omitempty"`
	KV  map[string]any `json:"kv,omitempty"`
	Del []string       `json:"del,omitempty"`
}

// Mood overrides the device mood for TTL seconds.
type Mood struct {
	T    string `json:"t"`
	Mood string `json:"mood"`
	TTL  int    `json:"ttl,omitempty"`
}

// Notify shows a toast on the device.
type Notify struct {
	T     string `json:"t"`
	Text  string `json:"text"`
	Icon  string `json:"icon,omitempty"`
	Mood  string `json:"mood,omitempty"`
	Sound string `json:"sound,omitempty"`
	Dur   int    `json:"dur,omitempty"`
}

// Config sets or reads device settings. Values is the device's reply to get.
type Config struct {
	T      string         `json:"t"`
	ID     *int           `json:"id,omitempty"`
	Set    map[string]any `json:"set,omitempty"`
	Get    []string       `json:"get,omitempty"`
	Values map[string]any `json:"values,omitempty"`
}

// Event is a device event (button, touch, gesture, page, pomodoro).
type Event struct {
	T     string `json:"t"`
	Kind  string `json:"kind"`
	Name  string `json:"name,omitempty"`
	State string `json:"state,omitempty"`
}

// Log is a device log line.
type Log struct {
	T   string `json:"t"`
	Lvl string `json:"lvl,omitempty"`
	Tag string `json:"tag,omitempty"`
	Msg string `json:"msg"`
}

// Ping is the keepalive message.
type Ping struct {
	T string `json:"t"`
}

// IntPtr returns a pointer to i (for optional id fields).
//
//go:fix inline
func IntPtr(i int) *int { return new(i) }

// BoolPtr returns a pointer to b.
//
//go:fix inline
func BoolPtr(b bool) *bool { return new(b) }

// Encode marshals a message and checks the line limit.
func Encode(v any) ([]byte, error) {
	b, err := json.Marshal(v)
	if err != nil {
		return nil, err
	}
	if len(b) > MaxLine {
		return nil, fmt.Errorf("%w (%d bytes)", ErrTooLong, len(b))
	}
	return b, nil
}

// Reader reads protocol lines. Lines that don't start with '{' are skipped
// (boot messages, raw debug output), and over-long lines are discarded.
type Reader struct {
	br *bufio.Reader
}

// NewReader wraps r.
func NewReader(r io.Reader) *Reader {
	return &Reader{br: bufio.NewReaderSize(r, 4096)}
}

// ReadLine returns the next JSON line without the trailing newline.
func (r *Reader) ReadLine() ([]byte, error) {
	for {
		line, tooLong, err := r.readRaw()
		if err != nil {
			return nil, err
		}
		if tooLong {
			continue
		}
		line = bytes.TrimSpace(line)
		if len(line) == 0 || line[0] != '{' {
			continue
		}
		return line, nil
	}
}

func (r *Reader) readRaw() ([]byte, bool, error) {
	var buf []byte
	tooLong := false
	for {
		chunk, isPrefix, err := r.br.ReadLine()
		if err != nil {
			if err == io.EOF && len(buf) > 0 && !tooLong {
				return buf, false, nil
			}
			return nil, false, err
		}
		if !tooLong {
			buf = append(buf, chunk...)
			if len(buf) > MaxLine {
				tooLong = true
				buf = nil
			}
		}
		if !isPrefix {
			return buf, tooLong, nil
		}
	}
}

// Writer writes messages as lines. It is safe for concurrent use.
type Writer struct {
	mu sync.Mutex
	w  io.Writer
}

// NewWriter wraps w.
func NewWriter(w io.Writer) *Writer { return &Writer{w: w} }

// Write encodes v and writes it followed by a newline. It returns the encoded line.
func (w *Writer) Write(v any) ([]byte, error) {
	b, err := Encode(v)
	if err != nil {
		return nil, err
	}
	return b, w.WriteLine(b)
}

// WriteLine writes an already encoded line.
func (w *Writer) WriteLine(b []byte) error {
	w.mu.Lock()
	defer w.mu.Unlock()
	buf := make([]byte, 0, len(b)+1)
	buf = append(buf, b...)
	buf = append(buf, '\n')
	_, err := w.w.Write(buf)
	return err
}
