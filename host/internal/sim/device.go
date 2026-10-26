// Package sim emulates a Desk Buddy device for testing buddyd without hardware.
//
// It mirrors the firmware design: settings in NVS, a DataStore, a SourceRouter that
// decides per source whether the device fetches it itself (WiFi with internet) or
// subscribes to it on a host, a HostLink speaking the protocol over TCP or a serial
// port, a MoodEngine and a PageManager. A web UI renders the round display.
package sim

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"maps"
	"math"
	"math/rand"
	"net"
	"os"
	"slices"
	"sort"
	"strings"
	"sync"
	"time"

	"github.com/StefanRiese/esp32_desk_buddy/host/internal/fetch"
	"github.com/StefanRiese/esp32_desk_buddy/host/internal/protocol"
	"github.com/StefanRiese/esp32_desk_buddy/host/internal/source"
)

// Sources the firmware can fetch itself over WiFi (StandaloneFetcher).
var deviceNative = map[string]bool{"time": true, "weather": true, "aqi": true, "http": true}

// Sources that need internet on the fetching side (proxmox/ha are LAN services).
var needsInternet = map[string]bool{"weather": true, "aqi": true, "http": true, "gitlab": true, "calendar": true}

var builtinPages = []string{"eyes", "dashboard", "forecast", "indoor", "host", "countdown"}

// Link states.
const (
	linkOff        = "off"
	linkConnecting = "connecting"
	linkHandshake  = "handshake"
	linkReady      = "ready"
)

// Device is the emulated ESP32.
type Device struct {
	nvs      *NVS
	store    *Store
	localReg *source.Registry
	rnd      *rand.Rand

	watchMu  sync.Mutex
	watchers map[chan struct{}]struct{}
	relink   chan struct{}

	mu sync.Mutex
	// link
	linkEnabled bool
	linkState   string
	linkErr     string
	linkTarget  string
	w           *protocol.Writer
	helloSentAt time.Time
	lastSend    time.Time
	host        protocol.Hello
	hostNet     bool
	lastReadyAt time.Time
	msgID       int
	pendingSubs map[int]string
	// router
	assign map[string]*assignment
	// clock
	clockSet    bool
	clockOffset time.Duration
	clockSrc    string
	// data
	weather    *protocol.Weather
	weatherAt  time.Time
	weatherTTL time.Duration
	// sensors
	temp, hum, lux float64
	hist           []float64
	// ui
	page     string
	custom   map[string]*customPage
	notify   *notifyState
	notifs   []notifyState
	override timedMood
	reaction timedMood
	cd       countdown
	logLines []logLine
}

type assignment struct {
	sid, src string
	via      string // host | device | none
	reason   string
	key      string // via + params, to detect changes
	params   json.RawMessage
	every    int
	state    string // pending | ok | error | unavailable
	msg      string
	cancel   context.CancelFunc
}

type customPage struct {
	Name  string          `json:"name"`
	Title string          `json:"title"`
	Items json.RawMessage `json:"items"`
	Until time.Time       `json:"-"`
}

type notifyState struct {
	Text  string    `json:"text"`
	Icon  string    `json:"icon,omitempty"`
	Mood  string    `json:"mood,omitempty"`
	At    time.Time `json:"at"`
	Until time.Time `json:"-"`
}

type timedMood struct {
	name, reason string
	until        time.Time
}

type logLine struct {
	TS   time.Time `json:"ts"`
	Dir  string    `json:"dir"` // in | out | sys
	Line string    `json:"line"`
}

// New creates a device from its NVS. Device-native sources use client.
func New(nvs *NVS, client *fetch.Client) *Device {
	d := &Device{
		nvs:   nvs,
		store: NewStore(),
		localReg: source.NewRegistryWith(source.Time{}, &source.Weather{Client: client},
			&source.AQI{Client: client}, &source.HTTP{Client: client}),
		rnd:         rand.New(rand.NewSource(time.Now().UnixNano())),
		watchers:    map[chan struct{}]struct{}{},
		relink:      make(chan struct{}, 1),
		linkEnabled: true,
		linkState:   linkOff,
		hostNet:     true,
		pendingSubs: map[int]string{},
		assign:      map[string]*assignment{},
		temp:        22.3, hum: 44, lux: 180,
		page:   "eyes",
		custom: map[string]*customPage{},
	}
	return d
}

// Watch returns a channel that signals state changes (coalescing) and a cancel func.
func (d *Device) Watch() (<-chan struct{}, func()) {
	ch := make(chan struct{}, 1)
	d.watchMu.Lock()
	d.watchers[ch] = struct{}{}
	d.watchMu.Unlock()
	return ch, func() {
		d.watchMu.Lock()
		delete(d.watchers, ch)
		d.watchMu.Unlock()
	}
}

func (d *Device) notifyChange() {
	d.watchMu.Lock()
	defer d.watchMu.Unlock()
	for ch := range d.watchers {
		select {
		case ch <- struct{}{}:
		default:
		}
	}
}

func (d *Device) logf(dir, format string, args ...any) {
	d.mu.Lock()
	d.addLogLocked(dir, fmt.Sprintf(format, args...))
	d.mu.Unlock()
}

func (d *Device) addLogLocked(dir, line string) {
	d.logLines = append(d.logLines, logLine{TS: time.Now(), Dir: dir, Line: line})
	if len(d.logLines) > 300 {
		d.logLines = d.logLines[len(d.logLines)-300:]
	}
}

// Run starts the device until ctx ends.
func (d *Device) Run(ctx context.Context) {
	d.logf("sys", "boot: %s fw %s, NVS %s", d.nvs.String("dev"), d.nvs.String("fw"), d.nvs.Path())
	d.mu.Lock()
	d.rerouteLocked()
	d.mu.Unlock()
	go d.sensorLoop(ctx)
	go d.housekeeping(ctx)
	d.linkLoop(ctx)
}

// ---------------------------------------------------------------- link

func (d *Device) linkLoop(ctx context.Context) {
	backoff := time.Second
	for ctx.Err() == nil {
		d.mu.Lock()
		enabled := d.linkEnabled
		target, transport := d.linkTargetLocked()
		d.linkTarget = transport + " " + target
		d.mu.Unlock()
		if !enabled || target == "" {
			d.setLink(linkOff, "")
			if !d.waitRelink(ctx, 0) {
				return
			}
			continue
		}
		d.setLink(linkConnecting, "")
		conn, err := dial(transport, target)
		if err != nil {
			d.setLink(linkConnecting, err.Error())
			if !d.waitRelink(ctx, backoff) {
				return
			}
			if backoff < 30*time.Second {
				backoff *= 2
			}
			continue
		}
		backoff = time.Second
		lctx, cancel := context.WithCancel(ctx)
		go func() {
			select {
			case <-lctx.Done():
			case <-d.relink:
				cancel()
			}
		}()
		err = d.runLink(lctx, conn)
		cancel()
		conn.Close()
		d.linkDown(err)
		if ctx.Err() == nil {
			select {
			case <-ctx.Done():
			case <-time.After(time.Second):
			}
		}
	}
}

func (d *Device) waitRelink(ctx context.Context, timeout time.Duration) bool {
	var tc <-chan time.Time
	if timeout > 0 {
		tc = time.After(timeout)
	}
	select {
	case <-ctx.Done():
		return false
	case <-d.relink:
	case <-tc:
	}
	return true
}

func (d *Device) triggerRelink() {
	select {
	case d.relink <- struct{}{}:
	default:
	}
}

func (d *Device) linkTargetLocked() (target, transport string) {
	if p := d.nvs.String("link.serial"); p != "" {
		return p, "serial"
	}
	return d.nvs.String("link.tcp_host"), "tcp"
}

func dial(transport, target string) (io.ReadWriteCloser, error) {
	if transport == "serial" {
		// Works for PTYs (socat) and, on Linux/macOS, real ports in raw mode.
		return os.OpenFile(target, os.O_RDWR, 0)
	}
	return net.DialTimeout("tcp", target, 3*time.Second)
}

func (d *Device) setLink(state, errMsg string) {
	d.mu.Lock()
	changed := d.linkState != state || d.linkErr != errMsg
	d.linkState, d.linkErr = state, errMsg
	d.mu.Unlock()
	if changed {
		if errMsg != "" {
			d.logf("sys", "link %s: %s", state, errMsg)
		}
		d.notifyChange()
	}
}

func (d *Device) runLink(ctx context.Context, conn io.ReadWriteCloser) error {
	w := protocol.NewWriter(conn)
	d.mu.Lock()
	d.w = w
	d.linkState, d.linkErr = linkHandshake, ""
	d.mu.Unlock()
	d.logf("sys", "link connected (%s)", d.linkTarget)
	d.sendHello()

	lines := make(chan []byte)
	errc := make(chan error, 1)
	go func() {
		r := protocol.NewReader(conn)
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
	lastRecv := time.Now()
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return errors.New("link disabled")
		case err := <-errc:
			return err
		case line := <-lines:
			lastRecv = time.Now()
			d.handleHost(line)
		case <-ticker.C:
			d.mu.Lock()
			state, helloAt, sendAt := d.linkState, d.helloSentAt, d.lastSend
			d.mu.Unlock()
			if time.Since(lastRecv) > 30*time.Second {
				return errors.New("host silent for 30 s")
			}
			if state == linkHandshake && time.Since(helloAt) > 5*time.Second {
				d.sendHello()
			} else if time.Since(sendAt) > 10*time.Second {
				d.send(protocol.Ping{T: "ping"})
			}
		}
	}
}

func (d *Device) linkDown(err error) {
	d.mu.Lock()
	wasReady := d.linkState == linkReady
	d.w = nil
	d.linkState = linkConnecting
	if err != nil && !errors.Is(err, io.EOF) {
		d.linkErr = err.Error()
	} else {
		d.linkErr = "closed by host"
	}
	if !d.linkEnabled {
		d.linkState, d.linkErr = linkOff, ""
	}
	d.host = protocol.Hello{}
	d.dropHostAssignmentsLocked()
	d.rerouteLocked()
	d.mu.Unlock()
	if wasReady {
		d.logf("sys", "link lost: %v", err)
	}
	d.notifyChange()
}

func (d *Device) sendHello() {
	d.mu.Lock()
	h := protocol.Hello{
		T: "hello", V: protocol.Version, Dev: d.nvs.String("dev"), FW: d.nvs.String("fw"),
		Board: d.nvs.String("board"), Caps: []string{"eyes", "touch", "imu", "sht40", "sim"},
		Token: d.nvs.String("link.token"),
	}
	d.helloSentAt = time.Now()
	d.linkState = linkHandshake
	d.mu.Unlock()
	d.send(h)
}

// send writes a message if a link is up. Lines are logged with secrets masked.
func (d *Device) send(v any) {
	d.mu.Lock()
	w := d.w
	d.mu.Unlock()
	if w == nil {
		return
	}
	line, err := w.Write(v)
	d.mu.Lock()
	defer d.mu.Unlock()
	if err != nil {
		d.addLogLocked("sys", "send failed: "+err.Error())
		return
	}
	d.lastSend = time.Now()
	if !strings.Contains(string(line), `"t":"ping"`) {
		d.addLogLocked("out", string(protocol.MaskJSON(line)))
	}
}

func (d *Device) handleHost(line []byte) {
	e, err := protocol.Decode(line)
	if err != nil {
		return
	}
	if e.T != "ping" {
		d.logf("in", "%s", protocol.MaskJSON(line))
	}
	defer d.notifyChange()
	switch e.T {
	case "hello":
		var h protocol.Hello
		if e.As(&h) != nil {
			return
		}
		if h.Dev == "" {
			// Host opened the port and asks us to introduce ourselves. If our own hello
			// is already on its way, its answer will come; the resend timer covers loss.
			d.mu.Lock()
			waiting := d.linkState == linkHandshake && time.Since(d.helloSentAt) < 5*time.Second
			d.mu.Unlock()
			if !waiting {
				d.sendHello()
			}
			return
		}
		d.mu.Lock()
		if h.Dev != d.nvs.String("dev") {
			d.mu.Unlock()
			return
		}
		d.host = h
		d.hostNet = h.Net == nil || *h.Net
		d.linkState, d.linkErr = linkReady, ""
		d.lastReadyAt = time.Now()
		d.dropHostAssignmentsLocked() // a new hello means the host has no subscriptions
		d.rerouteLocked()
		d.mu.Unlock()
	case "host_state":
		var m protocol.HostState
		if e.As(&m) == nil {
			d.mu.Lock()
			d.hostNet = m.Net
			d.rerouteLocked()
			d.mu.Unlock()
		}
	case "ack":
		var m protocol.Ack
		if e.As(&m) == nil {
			d.mu.Lock()
			if sid, ok := d.pendingSubs[m.ID]; ok {
				delete(d.pendingSubs, m.ID)
				if a := d.assign[sid]; a != nil && a.via == "host" && m.Every > 0 {
					a.every = m.Every
				}
			}
			d.mu.Unlock()
		}
	case "err":
		var m protocol.Err
		if e.As(&m) != nil {
			return
		}
		d.mu.Lock()
		if m.ID != nil {
			if sid, ok := d.pendingSubs[*m.ID]; ok {
				delete(d.pendingSubs, *m.ID)
				if a := d.assign[sid]; a != nil {
					a.state, a.msg = "error", m.Code+": "+m.Msg
				}
			}
		}
		if m.Code == "auth" || m.Code == "version" {
			d.linkErr = m.Code + ": " + m.Msg
		}
		d.mu.Unlock()
	case "sub_state":
		var m protocol.SubState
		if e.As(&m) == nil {
			d.mu.Lock()
			if a := d.assign[m.SID]; a != nil && a.via == "host" {
				a.state, a.msg = m.State, m.Msg
			}
			d.mu.Unlock()
		}
	case "time", "weather", "data":
		d.applyResult(e, "host")
	case "mood":
		var m protocol.Mood
		if e.As(&m) == nil {
			ttl := m.TTL
			if ttl <= 0 {
				ttl = 30
			}
			d.mu.Lock()
			d.override = timedMood{name: m.Mood, reason: "host override", until: time.Now().Add(time.Duration(ttl) * time.Second)}
			d.mu.Unlock()
		}
	case "notify":
		var m protocol.Notify
		if e.As(&m) == nil {
			dur := m.Dur
			if dur <= 0 {
				dur = 8
			}
			n := notifyState{Text: m.Text, Icon: m.Icon, Mood: m.Mood, At: time.Now(),
				Until: time.Now().Add(time.Duration(dur) * time.Second)}
			d.mu.Lock()
			d.notify = &n
			d.notifs = append([]notifyState{n}, d.notifs...)
			if len(d.notifs) > 10 {
				d.notifs = d.notifs[:10]
			}
			d.mu.Unlock()
		}
	case "page":
		var m struct {
			customPage
			Del bool `json:"del"`
			TTL int  `json:"ttl"`
		}
		if e.As(&m) != nil || m.Name == "" {
			return
		}
		d.mu.Lock()
		if m.Del {
			delete(d.custom, m.Name)
		} else if len(d.custom) < 8 || d.custom[m.Name] != nil {
			p := m.customPage
			if m.TTL > 0 {
				p.Until = time.Now().Add(time.Duration(m.TTL) * time.Second)
			}
			d.custom[m.Name] = &p
		}
		d.mu.Unlock()
	case "config":
		d.handleConfig(e)
	}
}

func (d *Device) handleConfig(e protocol.Envelope) {
	var m protocol.Config
	if e.As(&m) != nil {
		return
	}
	reply := func(v any) { d.send(v) }
	if len(m.Get) > 0 {
		d.mu.Lock()
		vals := d.nvs.Flat(m.Get)
		d.mu.Unlock()
		reply(protocol.Config{T: "config", ID: m.ID, Values: vals})
		return
	}
	if len(m.Set) == 0 {
		return
	}
	d.mu.Lock()
	err := d.nvs.SetMany(m.Set)
	if err == nil {
		d.rerouteLocked()
	}
	d.mu.Unlock()
	if err != nil {
		reply(protocol.Err{T: "err", ID: m.ID, Code: "bad_value", Msg: err.Error()})
		return
	}
	if m.ID != nil {
		reply(protocol.Ack{T: "ack", ID: *m.ID})
	}
	for k := range m.Set {
		if strings.HasPrefix(k, "link.") || k == "dev" {
			d.triggerRelink()
			break
		}
	}
}

// applyResult stores time/weather/data from the host or the local fetcher.
func (d *Device) applyResult(e protocol.Envelope, src string) {
	switch e.T {
	case "time":
		var m protocol.Time
		if e.As(&m) == nil {
			d.setClock(m.Epoch, src)
			d.markSrcOK("time", src)
		}
	case "weather":
		var m protocol.Weather
		if e.As(&m) == nil {
			d.applyWeather(&m, src)
			d.markSrcOK("weather", src)
		}
	case "data":
		var m protocol.Data
		if e.As(&m) != nil {
			return
		}
		ttl := time.Duration(m.TTL) * time.Second
		sids := map[string]bool{}
		for k, v := range m.KV {
			s := src
			if strings.HasPrefix(k, "user.") {
				s = "push"
			}
			d.store.Put(k, v, s, ttl)
			if i := strings.IndexByte(k, '.'); i > 0 {
				sids[k[:i]] = true
			}
		}
		for _, k := range m.Del {
			d.store.Del(k)
		}
		for sid := range sids {
			d.markOK(sid, src)
		}
	}
}

func (d *Device) setClock(epoch int64, src string) {
	d.mu.Lock()
	d.clockOffset = time.Until(time.Unix(epoch, 0))
	if d.clockOffset > -2*time.Second && d.clockOffset < 2*time.Second {
		d.clockOffset = 0
	}
	d.clockSet, d.clockSrc = true, src
	d.mu.Unlock()
}

func (d *Device) applyWeather(w *protocol.Weather, src string) {
	ttl := time.Duration(w.TTL) * time.Second
	if ttl == 0 {
		ttl = time.Hour
	}
	d.mu.Lock()
	d.weather, d.weatherAt, d.weatherTTL = w, time.Now(), ttl
	d.mu.Unlock()
	d.store.Put("weather.now.temp", w.Now.Temp, src, ttl)
	d.store.Put("weather.now.feels", w.Now.Feels, src, ttl)
	d.store.Put("weather.now.hum", w.Now.Hum, src, ttl)
	d.store.Put("weather.now.code", w.Now.Code, src, ttl)
	d.store.Put("weather.now.wind", w.Now.Wind, src, ttl)
	rain := 0.0
	for _, r := range w.RainNext2h {
		rain += r
	}
	d.store.Put("weather.rain_next_2h", math.Round(rain*10)/10, src, ttl)
}

// markOK marks the source with this sid as delivering, if it is assigned to src.
func (d *Device) markOK(sid, src string) {
	d.mu.Lock()
	defer d.mu.Unlock()
	if a := d.assign[sid]; a != nil && a.via == src {
		a.state, a.msg = "ok", ""
	}
}

// markSrcOK marks all sources of a kind (time, weather) assigned to src as delivering.
func (d *Device) markSrcOK(srcName, src string) {
	d.mu.Lock()
	defer d.mu.Unlock()
	for _, a := range d.assign {
		if a.src == srcName && a.via == src {
			a.state, a.msg = "ok", ""
		}
	}
}

// ---------------------------------------------------------------- SourceRouter

func (d *Device) dropHostAssignmentsLocked() {
	for sid, a := range d.assign {
		if a.via == "host" {
			delete(d.assign, sid)
		}
	}
	d.pendingSubs = map[int]string{}
}

// rerouteLocked decides for every configured source who fetches it, and starts,
// moves or stops subscriptions. Messages are sent asynchronously.
func (d *Device) rerouteLocked() {
	cfgs, err := d.nvs.Sources()
	if err != nil {
		d.addLogLocked("sys", "sources: "+err.Error())
		return
	}
	var out []any
	seen := map[string]bool{}
	for _, c := range cfgs {
		seen[c.SID] = true
		via, reason := d.decideLocked(c)
		params := d.paramsLocked(c)
		key := fmt.Sprintf("%s|%d|%s", via, c.Every, params)
		cur := d.assign[c.SID]
		if cur != nil && cur.key == key {
			cur.reason = reason
			continue
		}
		if cur != nil {
			out = append(out, d.stopLocked(cur)...)
		}
		a := &assignment{sid: c.SID, src: c.Src, via: via, reason: reason, key: key,
			params: params, every: c.Every, state: "pending"}
		d.assign[c.SID] = a
		switch via {
		case "host":
			d.msgID++
			id := d.msgID
			d.pendingSubs[id] = c.SID
			out = append(out, protocol.Sub{T: "sub", ID: new(id), SID: c.SID, Src: c.Src, Every: c.Every, P: params})
		case "device":
			d.startLocalLocked(a)
		default:
			a.state = "unavailable"
		}
	}
	for sid, a := range d.assign {
		if !seen[sid] {
			out = append(out, d.stopLocked(a)...)
			delete(d.assign, sid)
		}
	}
	if len(out) > 0 {
		go func() {
			for _, m := range out {
				d.send(m)
			}
		}()
	}
}

func (d *Device) stopLocked(a *assignment) []any {
	if a.cancel != nil {
		a.cancel()
		a.cancel = nil
	}
	if a.via == "host" && d.linkState == linkReady {
		return []any{protocol.Unsub{T: "unsub", SID: a.sid}}
	}
	return nil
}

func (d *Device) decideLocked(c SourceCfg) (via, reason string) {
	wifiNet := d.nvs.Bool("wifi.internet")
	native := deviceNative[c.Src]
	hostHas := false
	if d.linkState == linkReady {
		for _, s := range d.host.Srcs {
			if s == c.Src {
				hostHas = true
			}
		}
	}
	hostOK := hostHas && (!needsInternet[c.Src] || d.hostNet)
	deviceOK := native && wifiNet

	hostWhyNot := "no host connected"
	if d.linkState == linkReady {
		switch {
		case !hostHas:
			hostWhyNot = "host doesn't offer " + c.Src
		case !hostOK:
			hostWhyNot = "host has no internet"
		}
	}
	deviceWhyNot := "WiFi has no internet"
	if !native {
		deviceWhyNot = c.Src + " is host-only"
	}

	switch c.Via {
	case "device":
		if deviceOK {
			return "device", "forced: device"
		}
		return "none", deviceWhyNot
	case "host":
		if hostOK {
			return "host", "forced: host"
		}
		return "none", hostWhyNot
	}
	if deviceOK {
		return "device", "WiFi has internet"
	}
	if hostOK {
		return "host", deviceWhyNot
	}
	return "none", deviceWhyNot + ", " + hostWhyNot
}

// paramsLocked fills defaults from the device settings (location for weather/aqi).
func (d *Device) paramsLocked(c SourceCfg) json.RawMessage {
	p := map[string]any{}
	maps.Copy(p, c.P)
	if c.Src == "weather" || c.Src == "aqi" {
		if _, ok := p["lat"]; !ok {
			p["lat"] = d.nvs.Float("location.lat", 0)
			p["lon"] = d.nvs.Float("location.lon", 0)
		}
	}
	if len(p) == 0 {
		return nil
	}
	b, _ := json.Marshal(p)
	return b
}

// startLocalLocked emulates the firmware's StandaloneFetcher for device-native sources.
func (d *Device) startLocalLocked(a *assignment) {
	src, err := d.localReg.Get(a.src)
	if err != nil {
		a.state, a.msg = "error", err.Error()
		return
	}
	if err := src.Validate(a.params); err != nil {
		a.state, a.msg = "error", err.Error()
		return
	}
	every := time.Duration(a.every) * time.Second
	if every <= 0 {
		every = src.DefaultInterval()
	}
	if every < src.MinInterval() {
		every = src.MinInterval()
	}
	a.every = int(every / time.Second)
	ctx, cancel := context.WithCancel(context.Background())
	a.cancel = cancel
	sid, srcName, params := a.sid, a.src, a.params
	ttl := int(3 * every / time.Second)
	go func() {
		for {
			fctx, fcancel := context.WithTimeout(ctx, 20*time.Second)
			res, err := d.localReg.Fetch(fctx, srcName, params)
			fcancel()
			if ctx.Err() != nil {
				return
			}
			wait := every
			if err != nil {
				d.mu.Lock()
				if a := d.assign[sid]; a != nil && a.via == "device" {
					a.state, a.msg = "error", err.Error()
				}
				d.mu.Unlock()
				if wait > 30*time.Second {
					wait = 30 * time.Second
				}
			} else {
				d.applyLocal(sid, res, ttl)
			}
			d.notifyChange()
			select {
			case <-ctx.Done():
				return
			case <-time.After(wait):
			}
		}
	}()
}

func (d *Device) applyLocal(sid string, res source.Result, ttl int) {
	if res.Time != nil {
		d.setClock(res.Time.Epoch, "device")
	}
	if res.Weather != nil {
		w := *res.Weather
		w.TTL = ttl
		d.applyWeather(&w, "device")
	}
	for k, v := range res.Data {
		d.store.Put(sid+"."+k, v, "device", time.Duration(ttl)*time.Second)
	}
	d.markOK(sid, "device")
}

// ---------------------------------------------------------------- sensors & housekeeping

func (d *Device) sensorLoop(ctx context.Context) {
	tick := time.NewTicker(5 * time.Second)
	defer tick.Stop()
	n := 0
	for {
		d.mu.Lock()
		d.temp = clamp(d.temp+d.rnd.NormFloat64()*0.08+(22.5-d.temp)*0.01, 15, 32)
		d.hum = clamp(d.hum+d.rnd.NormFloat64()*0.4+(45-d.hum)*0.02, 20, 80)
		d.lux = clamp(d.lux+d.rnd.NormFloat64()*8, 0, 1000)
		t, h, l := round1(d.temp), round1(d.hum), math.Round(d.lux)
		if n%2 == 0 {
			d.hist = append(d.hist, t)
			if len(d.hist) > 288 {
				d.hist = d.hist[1:]
			}
		}
		ready := d.linkState == linkReady
		d.mu.Unlock()
		d.store.Put("indoor.temp", t, "local", time.Minute)
		d.store.Put("indoor.hum", h, "local", time.Minute)
		d.store.Put("indoor.lux", l, "local", time.Minute)
		if ready && n%6 == 0 {
			d.send(map[string]any{"t": "sensor", "temp": t, "hum": h, "lux": l})
		}
		n++
		d.notifyChange()
		select {
		case <-ctx.Done():
			return
		case <-tick.C:
		}
	}
}

// housekeeping expires pages and notifications and refreshes the UI once a second.
func (d *Device) housekeeping(ctx context.Context) {
	tick := time.NewTicker(time.Second)
	defer tick.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-tick.C:
		}
		now := time.Now()
		d.mu.Lock()
		for name, p := range d.custom {
			if !p.Until.IsZero() && now.After(p.Until) {
				delete(d.custom, name)
			}
		}
		if d.notify != nil && now.After(d.notify.Until) {
			d.notify = nil
		}
		if !d.pageExistsLocked(d.page) {
			d.page = "eyes"
		}
		d.countdownTickLocked(now)
		d.mu.Unlock()
		d.notifyChange()
	}
}

func clamp(v, lo, hi float64) float64 { return math.Max(lo, math.Min(hi, v)) }
func round1(v float64) float64        { return math.Round(v*10) / 10 }

// ---------------------------------------------------------------- mood engine

var rainCodes = map[int]bool{51: true, 53: true, 55: true, 56: true, 57: true, 61: true, 63: true, 65: true,
	66: true, 67: true, 80: true, 81: true, 82: true, 95: true, 96: true, 99: true}

func isSnow(code int) bool { return (code >= 71 && code <= 77) || code == 85 || code == 86 }

// moodLocked emulates the firmware MoodEngine:
// reaction > countdown alarm > host override > notification > environment > time of day > neutral.
func (d *Device) moodLocked(now time.Time) (string, string) {
	if now.Before(d.reaction.until) {
		return d.reaction.name, d.reaction.reason
	}
	if !d.cd.ringing.IsZero() {
		return "surprised", "countdown done"
	}
	if now.Before(d.override.until) {
		return d.override.name, d.override.reason
	}
	if d.notify != nil {
		if d.notify.Mood != "" {
			return d.notify.Mood, "notification"
		}
		return "surprised", "notification"
	}
	if v, ok := d.store.Fresh("indoor.temp"); ok {
		t := v.(float64)
		if t > d.nvs.Float("mood.hot", 26) {
			return "hot", fmt.Sprintf("indoor %.1f °C", t)
		}
		if t < d.nvs.Float("mood.cold", 18) {
			return "cold", fmt.Sprintf("indoor %.1f °C", t)
		}
	}
	if v, ok := d.store.Fresh("indoor.hum"); ok && v.(float64) < d.nvs.Float("mood.dry", 30) {
		return "dry", fmt.Sprintf("humidity %.0f %%", v.(float64))
	}
	weatherFresh := d.weather != nil && now.Sub(d.weatherAt) < d.weatherTTL
	if weatherFresh {
		if rainCodes[d.weather.Now.Code] {
			return "sad", "rain outside"
		}
		if isSnow(d.weather.Now.Code) {
			return "cold", "snow outside"
		}
	}
	if d.clockSet {
		h := d.localTimeLocked(now).Hour()
		from, to := int(d.nvs.Float("ui.night_from", 22)), int(d.nvs.Float("ui.night_to", 7))
		if (from > to && (h >= from || h < to)) || (from < to && h >= from && h < to) {
			return "sleepy", "night"
		}
	}
	if d.linkEnabled && !d.lastReadyAt.IsZero() && d.linkState != linkReady {
		return "sad", "host link lost"
	}
	if weatherFresh && d.weather.Now.Code <= 1 && d.weather.Now.IsDay == 1 {
		return "happy", "sunny"
	}
	return "neutral", "idle"
}

func (d *Device) localTimeLocked(now time.Time) time.Time {
	t := now.Add(d.clockOffset)
	if loc, err := time.LoadLocation(d.nvs.String("location.tz")); err == nil {
		t = t.In(loc)
	}
	return t
}

// ---------------------------------------------------------------- pages & input

func (d *Device) pagesLocked() []string {
	pages := append([]string(nil), builtinPages...)
	var names []string
	for n := range d.custom {
		names = append(names, n)
	}
	sort.Strings(names)
	for _, n := range names {
		pages = append(pages, "custom:"+n)
	}
	return pages
}

func (d *Device) pageExistsLocked(p string) bool {
	return slices.Contains(d.pagesLocked(), p)
}

func (d *Device) movePageLocked(delta int) {
	pages := d.pagesLocked()
	idx := 0
	for i, p := range pages {
		if p == d.page {
			idx = i
		}
	}
	d.page = pages[(idx+delta+len(pages))%len(pages)]
}

// Input handles a UI action (touch, button, gesture) like the firmware would.
func (d *Device) Input(action string) error { return d.InputAt(action, -1) }

// InputAt is Input with the horizontal start position of a touch gesture
// (0..1 across the screen, < 0 if unknown). The countdown page uses it.
func (d *Device) InputAt(action string, x float64) error {
	var events []protocol.Event
	now := time.Now()
	d.mu.Lock()
	oldPage := d.page
	cdState, used := "", false
	if d.page == "countdown" {
		cdState, used = d.countdownInputLocked(action, x, now)
	}
	if !used {
		switch action {
		case "swipe_left", "flick_right", "tap_right":
			d.movePageLocked(1)
		case "swipe_right", "flick_left", "tap_left":
			d.movePageLocked(-1)
		case "swipe_up":
			d.page = "eyes"
		case "swipe_down", "long_press", "button_long", "double_tap":
		case "button_short":
			d.movePageLocked(1)
		case "tap":
			if d.page == "eyes" {
				d.reaction = timedMood{name: "happy", reason: "boop", until: now.Add(1500 * time.Millisecond)}
			}
		case "shake":
			d.reaction = timedMood{name: "dizzy", reason: "shaken", until: now.Add(3 * time.Second)}
			d.notify = nil
		default:
			d.mu.Unlock()
			return fmt.Errorf("unknown action %q", action)
		}
	}
	switch {
	case strings.HasPrefix(action, "swipe_") || action == "tap" || action == "long_press":
		events = append(events, protocol.Event{T: "event", Kind: "touch", Name: action})
	case strings.HasPrefix(action, "button_"):
		events = append(events, protocol.Event{T: "event", Kind: "button", Name: strings.TrimPrefix(action, "button_")})
	default:
		events = append(events, protocol.Event{T: "event", Kind: "gesture", Name: action})
	}
	if cdState != "" {
		events = append(events, protocol.Event{T: "event", Kind: "countdown", State: cdState})
	}
	if d.page != oldPage {
		events = append(events, protocol.Event{T: "event", Kind: "page", Name: strings.TrimPrefix(d.page, "custom:")})
	}
	ready := d.linkState == linkReady
	d.mu.Unlock()
	if ready {
		for _, e := range events {
			d.send(e)
		}
	}
	d.notifyChange()
	return nil
}

// SetLinkEnabled connects or disconnects the host link (like unplugging USB).
func (d *Device) SetLinkEnabled(on bool) {
	d.mu.Lock()
	d.linkEnabled = on
	d.mu.Unlock()
	d.triggerRelink()
	d.notifyChange()
}

// SetWifiInternet emulates whether the device's WiFi has internet.
func (d *Device) SetWifiInternet(on bool) error {
	d.mu.Lock()
	err := d.nvs.SetMany(map[string]any{"wifi.internet": on})
	if err == nil {
		d.rerouteLocked()
	}
	d.mu.Unlock()
	d.notifyChange()
	return err
}

// Config returns the settings with secrets masked.
func (d *Device) Config() map[string]any {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.nvs.Masked()
}

// SetConfig replaces all settings (as the web config UI would) and applies them.
func (d *Device) SetConfig(tree map[string]any) error {
	d.mu.Lock()
	oldTarget, oldTr := d.linkTargetLocked()
	oldTok, oldDev := d.nvs.String("link.token"), d.nvs.String("dev")
	err := d.nvs.Replace(tree)
	if err == nil {
		d.rerouteLocked()
	}
	newTarget, newTr := d.linkTargetLocked()
	relink := err == nil && (oldTarget != newTarget || oldTr != newTr ||
		oldTok != d.nvs.String("link.token") || oldDev != d.nvs.String("dev"))
	d.mu.Unlock()
	if err != nil {
		return err
	}
	d.logf("sys", "settings saved")
	if relink {
		d.triggerRelink()
	}
	d.notifyChange()
	return nil
}
