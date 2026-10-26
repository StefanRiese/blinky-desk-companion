// Package session runs the protocol with one connected device: handshake,
// subscriptions, keepalive and request/response for forwarded config messages.
//
// A session keeps the device's subscriptions in memory only. When the device
// disconnects they are gone; the device sends them again after reconnecting.
package session

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log/slog"
	"math/rand"
	"regexp"
	"sort"
	"sync"
	"sync/atomic"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/hub"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/protocol"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/source"
)

// Timing of the keepalive (see PROTOCOL.md §3). Variables so tests can shorten them.
var (
	PingAfter    = 10 * time.Second
	SilenceLimit = 30 * time.Second
	tickEvery    = 1 * time.Second
)

// MaxSubs limits subscriptions per device.
const MaxSubs = 32

var (
	errAuth    = errors.New("device sent a wrong or missing token")
	errVersion = errors.New("protocol version mismatch")
	errSilence = errors.New("device silent for too long")
	sidRe      = regexp.MustCompile(`^[a-z0-9_]{1,16}$`)
)

// Options configures a session.
type Options struct {
	Transport string // "serial" or "tcp"
	Addr      string // port name or remote address
	Token     string // required device token (empty = none)
	HostName  string // e.g. "buddyd@laptop"
	Version   string
	Registry  *source.Registry
	Hub       *hub.Hub
}

// Session is one device connection.
type Session struct {
	opt  Options
	conn io.ReadWriteCloser
	w    *protocol.Writer

	lastRecv atomic.Int64
	lastSend atomic.Int64

	mu          sync.Mutex
	active      bool
	hello       protocol.Hello
	connectedAt time.Time
	sensor      map[string]any
	subs        map[string]*sub
	subCtx      context.Context
	subCancel   context.CancelFunc
	pending     map[int]chan map[string]any
	nextID      int
}

type sub struct {
	sid    string
	src    source.Source
	params json.RawMessage
	every  time.Duration
	cancel context.CancelFunc

	mu     sync.Mutex
	state  string
	msg    string
	lastOK time.Time
}

// New creates a session on conn. Serve runs it.
func New(conn io.ReadWriteCloser, opt Options) *Session {
	return &Session{
		opt:     opt,
		conn:    conn,
		w:       protocol.NewWriter(conn),
		subs:    map[string]*sub{},
		pending: map[int]chan map[string]any{},
		nextID:  1000,
	}
}

// keepOnSilence: a serial port stays open when the device goes quiet (it may be
// rebooting); a TCP connection is closed.
func (s *Session) keepOnSilence() bool { return s.opt.Transport == "serial" }

// Serve runs until the connection fails or ctx ends. The caller closes conn afterwards.
func (s *Session) Serve(ctx context.Context) error {
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	defer s.deactivate("connection closed")

	lines := make(chan []byte)
	errc := make(chan error, 1)
	go func() {
		r := protocol.NewReader(s.conn)
		for {
			line, err := r.ReadLine()
			if err != nil {
				errc <- err
				return
			}
			select {
			case lines <- line:
			case <-ctx.Done():
				return
			}
		}
	}()

	now := time.Now().UnixNano()
	s.lastRecv.Store(now)
	if s.keepOnSilence() {
		// The device may have booted before the port was opened: ask it to say hello.
		s.sendHostHello("")
	}
	ticker := time.NewTicker(tickEvery)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case err := <-errc:
			return err
		case line := <-lines:
			s.lastRecv.Store(time.Now().UnixNano())
			if err := s.handle(line); err != nil {
				return err
			}
		case <-ticker.C:
			if err := s.tick(); err != nil {
				return err
			}
		}
	}
}

func (s *Session) tick() error {
	sinceRecv := time.Since(time.Unix(0, s.lastRecv.Load()))
	sinceSend := time.Since(time.Unix(0, s.lastSend.Load()))
	s.mu.Lock()
	active := s.active
	s.mu.Unlock()
	if active && sinceRecv > SilenceLimit {
		if !s.keepOnSilence() {
			return errSilence
		}
		s.deactivate("device silent")
		active = false
	}
	if sinceSend > PingAfter {
		if active {
			s.Send(protocol.Ping{T: "ping"})
		} else if s.keepOnSilence() {
			s.sendHostHello("")
		}
	}
	return nil
}

func (s *Session) handle(line []byte) error {
	e, err := protocol.Decode(line)
	if err != nil {
		slog.Debug("ignoring invalid line", "addr", s.opt.Addr, "err", err)
		return nil
	}
	slog.Debug("rx", "addr", s.opt.Addr, "line", protocol.MaskJSON(line))
	if e.T == "hello" {
		return s.handleHello(e)
	}
	s.mu.Lock()
	active := s.active
	dev := s.hello.Dev
	s.mu.Unlock()

	switch e.T {
	case "ping":
	case "log", "event":
		var m map[string]any
		if e.As(&m) == nil {
			delete(m, "t")
			s.opt.Hub.Bus.Publish(e.T, dev, m)
		}
	case "sensor":
		var m map[string]any
		if e.As(&m) == nil {
			delete(m, "t")
			s.mu.Lock()
			s.sensor = m
			s.mu.Unlock()
			s.opt.Hub.Bus.Publish("sensor", dev, m)
		}
	case "sub":
		if !active {
			s.replyErr(e.ID, "no_hello", "send hello first")
			return nil
		}
		s.handleSub(e)
	case "unsub":
		var m protocol.Unsub
		if e.As(&m) == nil {
			s.stopSub(m.SID)
		}
		if e.ID != nil {
			s.Send(protocol.Ack{T: "ack", ID: *e.ID})
		}
	case "ack", "err", "config":
		if e.ID == nil {
			return nil
		}
		s.mu.Lock()
		ch, ok := s.pending[*e.ID]
		delete(s.pending, *e.ID)
		s.mu.Unlock()
		if ok {
			var m map[string]any
			e.As(&m)
			ch <- m
		}
	default:
		// Unknown types are ignored (forward compatibility).
	}
	return nil
}

func (s *Session) handleHello(e protocol.Envelope) error {
	var h protocol.Hello
	if err := e.As(&h); err != nil {
		s.replyErr(nil, "bad_hello", err.Error())
		return nil
	}
	if h.V != protocol.Version {
		s.replyErr(nil, "version", fmt.Sprintf("host speaks v%d", protocol.Version))
		return errVersion
	}
	if s.opt.Token != "" && h.Token != s.opt.Token {
		s.replyErr(nil, "auth", "wrong or missing token")
		return errAuth
	}
	if h.Dev == "" {
		s.replyErr(nil, "bad_hello", "dev is required")
		return nil
	}
	// A new hello starts a fresh session state (the device rebooted or reconnected).
	s.deactivate("")
	h.Token = ""
	ctx, cancel := context.WithCancel(context.Background())
	s.mu.Lock()
	s.active = true
	s.hello = h
	s.connectedAt = time.Now()
	s.sensor = nil
	s.subCtx, s.subCancel = ctx, cancel
	s.mu.Unlock()
	s.opt.Hub.Add(s)
	s.sendHostHello(h.Dev)
	slog.Info("device connected", "dev", h.Dev, "via", s.opt.Transport, "addr", s.opt.Addr, "fw", h.FW)
	s.opt.Hub.Bus.Publish("connected", h.Dev, map[string]any{
		"transport": s.opt.Transport, "addr": s.opt.Addr, "fw": h.FW, "board": h.Board,
	})
	return nil
}

func (s *Session) sendHostHello(dev string) {
	s.Send(protocol.Hello{
		T: "hello", V: protocol.Version, Dev: dev,
		Host: s.opt.HostName, Ver: s.opt.Version,
		Net: new(s.opt.Hub.Net()), Srcs: s.opt.Registry.Names(),
	})
}

// deactivate ends the device session state (subscriptions, hub registration).
func (s *Session) deactivate(reason string) {
	s.mu.Lock()
	if !s.active {
		s.mu.Unlock()
		return
	}
	s.active = false
	dev := s.hello.Dev
	if s.subCancel != nil {
		s.subCancel()
	}
	s.subs = map[string]*sub{}
	for id, ch := range s.pending {
		close(ch)
		delete(s.pending, id)
	}
	s.mu.Unlock()
	s.opt.Hub.Remove(s)
	if reason != "" {
		slog.Info("device disconnected", "dev", dev, "reason", reason)
		s.opt.Hub.Bus.Publish("disconnected", dev, map[string]string{"reason": reason})
	}
}

func (s *Session) replyErr(id *int, code, msg string) {
	s.Send(protocol.Err{T: "err", ID: id, Code: code, Msg: msg})
}

func (s *Session) handleSub(e protocol.Envelope) {
	var m protocol.Sub
	if err := e.As(&m); err != nil {
		s.replyErr(e.ID, "bad_params", err.Error())
		return
	}
	if !sidRe.MatchString(m.SID) {
		s.replyErr(m.ID, "bad_params", "sid must match [a-z0-9_]{1,16}")
		return
	}
	src, err := s.opt.Registry.Get(m.Src)
	if err != nil {
		s.replyErr(m.ID, "unknown_src", err.Error())
		return
	}
	if err := src.Validate(m.P); err != nil {
		s.replyErr(m.ID, "bad_params", err.Error())
		return
	}
	every := time.Duration(m.Every) * time.Second
	if every <= 0 {
		every = src.DefaultInterval()
	}
	if every < src.MinInterval() {
		every = src.MinInterval()
	}

	s.mu.Lock()
	if old, ok := s.subs[m.SID]; ok {
		old.cancel()
		delete(s.subs, m.SID)
	}
	if len(s.subs) >= MaxSubs {
		s.mu.Unlock()
		s.replyErr(m.ID, "too_many", fmt.Sprintf("at most %d subscriptions", MaxSubs))
		return
	}
	ctx, cancel := context.WithCancel(s.subCtx)
	sb := &sub{sid: m.SID, src: src, params: m.P, every: every, cancel: cancel, state: "pending"}
	s.subs[m.SID] = sb
	dev := s.hello.Dev
	s.mu.Unlock()

	if m.ID != nil {
		s.Send(protocol.Ack{T: "ack", ID: *m.ID, Every: int(every / time.Second)})
	}
	slog.Info("sub", "dev", dev, "sid", m.SID, "src", m.Src, "every", every, "p", protocol.MaskJSON(m.P))
	go s.runSub(ctx, sb)
}

func (s *Session) stopSub(sid string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if sb, ok := s.subs[sid]; ok {
		sb.cancel()
		delete(s.subs, sid)
	}
}

func (s *Session) runSub(ctx context.Context, sb *sub) {
	fails := 0
	for {
		fctx, cancel := context.WithTimeout(ctx, 30*time.Second)
		res, err := s.opt.Registry.Fetch(fctx, sb.src.Name(), sb.params)
		cancel()
		if ctx.Err() != nil {
			return
		}
		wait := jitter(sb.every)
		if err != nil {
			fails++
			wait = retryDelay(fails, sb.every)
			sb.setState("error", err.Error())
			slog.Warn("sub failed", "dev", s.ID(), "sid", sb.sid, "src", sb.src.Name(), "err", err, "retry_in", wait)
			s.Send(protocol.SubState{T: "sub_state", SID: sb.sid, State: "error", Msg: err.Error(), Retry: int(wait / time.Second)})
			s.opt.Hub.Bus.Publish("sub_state", s.ID(), map[string]any{"sid": sb.sid, "state": "error", "msg": err.Error()})
		} else {
			if fails > 0 {
				s.Send(protocol.SubState{T: "sub_state", SID: sb.sid, State: "ok"})
				s.opt.Hub.Bus.Publish("sub_state", s.ID(), map[string]any{"sid": sb.sid, "state": "ok"})
			}
			fails = 0
			sb.setState("ok", "")
			s.sendResult(sb, res)
		}
		select {
		case <-ctx.Done():
			return
		case <-time.After(wait):
		}
	}
}

func retryDelay(fails int, every time.Duration) time.Duration {
	if fails > 7 {
		fails = 7
	}
	d := min(15*time.Second<<(fails-1), every)
	return d
}

func jitter(d time.Duration) time.Duration {
	j := time.Duration(rand.Int63n(int64(d)/10+1)) - d/20
	return d + j
}

func (sb *sub) setState(state, msg string) {
	sb.mu.Lock()
	defer sb.mu.Unlock()
	sb.state, sb.msg = state, msg
	if state == "ok" {
		sb.lastOK = time.Now()
	}
}

func (s *Session) sendResult(sb *sub, res source.Result) {
	ttl := max(int(3*sb.every/time.Second), 60)
	if res.Time != nil {
		s.Send(res.Time)
	}
	if res.Weather != nil {
		w := *res.Weather
		w.TTL = ttl
		s.Send(w)
	}
	if len(res.Data) > 0 {
		kv := make(map[string]any, len(res.Data))
		for k, v := range res.Data {
			kv[sb.sid+"."+k] = v
		}
		s.sendData(kv, ttl)
	}
}

// sendData sends key/values, split into several messages if one line would be too long.
func (s *Session) sendData(kv map[string]any, ttl int) {
	err := s.Send(protocol.Data{T: "data", TTL: ttl, KV: kv})
	if !errors.Is(err, protocol.ErrTooLong) || len(kv) < 2 {
		return
	}
	keys := make([]string, 0, len(kv))
	for k := range kv {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	a, b := map[string]any{}, map[string]any{}
	for i, k := range keys {
		if i < len(keys)/2 {
			a[k] = kv[k]
		} else {
			b[k] = kv[k]
		}
	}
	s.sendData(a, ttl)
	s.sendData(b, ttl)
}

// Send encodes and writes one message.
func (s *Session) Send(v any) error {
	line, err := s.w.Write(v)
	if err != nil {
		if errors.Is(err, protocol.ErrTooLong) {
			slog.Warn("dropping message", "addr", s.opt.Addr, "err", err)
		}
		return err
	}
	s.lastSend.Store(time.Now().UnixNano())
	slog.Debug("tx", "addr", s.opt.Addr, "line", protocol.MaskJSON(line))
	return nil
}

// Request sends msg with a fresh id and waits for the device's ack/err/config reply.
func (s *Session) Request(ctx context.Context, msg map[string]any) (map[string]any, error) {
	s.mu.Lock()
	if !s.active {
		s.mu.Unlock()
		return nil, errors.New("device not connected")
	}
	id := s.nextID
	s.nextID++
	ch := make(chan map[string]any, 1)
	s.pending[id] = ch
	s.mu.Unlock()
	msg["id"] = id
	if err := s.Send(msg); err != nil {
		s.mu.Lock()
		delete(s.pending, id)
		s.mu.Unlock()
		return nil, err
	}
	select {
	case reply, ok := <-ch:
		if !ok {
			return nil, errors.New("device disconnected")
		}
		return reply, nil
	case <-ctx.Done():
		s.mu.Lock()
		delete(s.pending, id)
		s.mu.Unlock()
		return nil, fmt.Errorf("no reply from device: %w", ctx.Err())
	}
}

// ID returns the device id from hello.
func (s *Session) ID() string {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.hello.Dev
}

// Info describes the session for the control API (secrets masked).
func (s *Session) Info() hub.Info {
	s.mu.Lock()
	defer s.mu.Unlock()
	info := hub.Info{
		Dev: s.hello.Dev, FW: s.hello.FW, Board: s.hello.Board, Caps: s.hello.Caps,
		Transport: s.opt.Transport, Addr: s.opt.Addr, ConnectedAt: s.connectedAt,
		LastSeen: time.Unix(0, s.lastRecv.Load()), Sensor: s.sensor, Subs: []hub.SubInfo{},
	}
	for _, sb := range s.subs {
		sb.mu.Lock()
		si := hub.SubInfo{SID: sb.sid, Src: sb.src.Name(), Every: int(sb.every / time.Second),
			State: sb.state, Msg: sb.msg, LastOK: sb.lastOK}
		sb.mu.Unlock()
		var p any
		if json.Unmarshal(sb.params, &p) == nil {
			si.Params = protocol.MaskValue(p)
		}
		info.Subs = append(info.Subs, si)
	}
	sort.Slice(info.Subs, func(i, j int) bool { return info.Subs[i].SID < info.Subs[j].SID })
	return info
}
