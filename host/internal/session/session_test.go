package session

import (
	"context"
	"encoding/json"
	"net"
	"testing"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/events"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/hub"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/protocol"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/source"
)

type fakeSrc struct{}

func (fakeSrc) Name() string                   { return "fake" }
func (fakeSrc) NeedsInternet() bool            { return false }
func (fakeSrc) MinInterval() time.Duration     { return 2 * time.Second }
func (fakeSrc) DefaultInterval() time.Duration { return time.Minute }
func (fakeSrc) Validate(p json.RawMessage) error {
	var v struct{ X int }
	return json.Unmarshal(append([]byte(nil), orEmpty(p)...), &v)
}
func (fakeSrc) Fetch(context.Context, json.RawMessage) (source.Result, error) {
	return source.Result{Data: map[string]any{"v": 42.0}}, nil
}

func orEmpty(p json.RawMessage) []byte {
	if len(p) == 0 {
		return []byte("{}")
	}
	return p
}

// device is the test side of the pipe.
type device struct {
	t     *testing.T
	conn  net.Conn
	w     *protocol.Writer
	lines chan protocol.Envelope
}

func newDevice(t *testing.T, conn net.Conn) *device {
	d := &device{t: t, conn: conn, w: protocol.NewWriter(conn), lines: make(chan protocol.Envelope, 64)}
	go func() {
		r := protocol.NewReader(conn)
		for {
			line, err := r.ReadLine()
			if err != nil {
				close(d.lines)
				return
			}
			e, err := protocol.Decode(line)
			if err == nil && e.T != "ping" {
				d.lines <- e
			}
		}
	}()
	return d
}

func (d *device) send(v any) {
	d.t.Helper()
	if _, err := d.w.Write(v); err != nil {
		d.t.Fatal(err)
	}
}

func (d *device) expect(typ string) protocol.Envelope {
	d.t.Helper()
	select {
	case e, ok := <-d.lines:
		if !ok {
			d.t.Fatalf("connection closed, wanted %s", typ)
		}
		if e.T != typ {
			d.t.Fatalf("got %s (%s), want %s", e.T, e.Raw, typ)
		}
		return e
	case <-time.After(3 * time.Second):
		d.t.Fatalf("timeout waiting for %s", typ)
	}
	return protocol.Envelope{}
}

func setup(t *testing.T, transport, token string) (*device, *hub.Hub, chan error) {
	t.Helper()
	a, b := net.Pipe()
	h := hub.New(events.NewBus())
	s := New(a, Options{Transport: transport, Addr: "pipe", Token: token, HostName: "buddy-link@test",
		Version: "test", Registry: source.NewRegistryWith(fakeSrc{}, source.Time{}), Hub: h})
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan error, 1)
	go func() { done <- s.Serve(ctx); a.Close() }()
	t.Cleanup(func() { cancel(); b.Close() })
	return newDevice(t, b), h, done
}

func hello(token string) protocol.Hello {
	return protocol.Hello{T: "hello", V: 1, Dev: "buddy-test", FW: "1.0", Token: token}
}

func TestHandshakeAndSubscriptions(t *testing.T) {
	d, h, _ := setup(t, "tcp", "")
	d.send(hello(""))
	var hh protocol.Hello
	d.expect("hello").As(&hh)
	if hh.Dev != "buddy-test" || len(hh.Srcs) != 2 || hh.Net == nil {
		t.Fatalf("bad host hello %+v", hh)
	}
	if len(h.Devices("buddy-test")) != 1 {
		t.Fatal("device not registered in hub")
	}

	d.send(protocol.Sub{T: "sub", ID: new(1), SID: "f", Src: "fake", Every: 1})
	var ack protocol.Ack
	d.expect("ack").As(&ack)
	if ack.ID != 1 || ack.Every != 2 {
		t.Fatalf("want ack id 1 every 2 (clamped), got %+v", ack)
	}
	var data protocol.Data
	d.expect("data").As(&data)
	if data.KV["f.v"] != 42.0 || data.TTL < 60 {
		t.Fatalf("unexpected data %+v", data)
	}

	d.send(protocol.Sub{T: "sub", ID: new(2), SID: "x", Src: "gitlab"})
	var e protocol.Err
	d.expect("err").As(&e)
	if e.Code != "unknown_src" || *e.ID != 2 {
		t.Fatalf("want unknown_src, got %+v", e)
	}
	d.send(protocol.Sub{T: "sub", ID: new(3), SID: "Bad-SID", Src: "fake"})
	d.expect("err").As(&e)
	if e.Code != "bad_params" {
		t.Fatalf("want bad_params, got %+v", e)
	}

	d.send(protocol.Sub{T: "sub", ID: new(4), SID: "t", Src: "time"})
	d.expect("ack")
	d.expect("time")

	d.send(protocol.Unsub{T: "unsub", SID: "f"})
	time.Sleep(50 * time.Millisecond)
	info := h.Devices("buddy-test")[0].Info()
	if len(info.Subs) != 1 || info.Subs[0].SID != "t" {
		t.Fatalf("want only sub t left, got %+v", info.Subs)
	}
}

func TestTokenRequired(t *testing.T) {
	d, h, done := setup(t, "tcp", "secret")
	d.send(hello("wrong"))
	var e protocol.Err
	d.expect("err").As(&e)
	if e.Code != "auth" {
		t.Fatalf("want auth error, got %+v", e)
	}
	select {
	case err := <-done:
		if err != errAuth {
			t.Fatalf("want errAuth, got %v", err)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("session did not close")
	}
	if len(h.Devices("")) != 0 {
		t.Fatal("device registered despite bad token")
	}
}

func TestSerialSendsHelloFirstAndForwardsConfig(t *testing.T) {
	d, h, _ := setup(t, "serial", "")
	var first protocol.Hello
	d.expect("hello").As(&first)
	if first.Dev != "" {
		t.Fatalf("proactive hello must not carry dev, got %q", first.Dev)
	}
	d.send(hello(""))
	d.expect("hello")

	reply := make(chan map[string]any, 1)
	go func() {
		r, err := h.Devices("buddy-test")[0].Request(context.Background(), map[string]any{"t": "config", "get": []string{"ui"}})
		if err != nil {
			t.Error(err)
		}
		reply <- r
	}()
	req := d.expect("config")
	d.send(map[string]any{"t": "config", "id": *req.ID, "values": map[string]any{"ui.brightness": 180}})
	select {
	case r := <-reply:
		if r["t"] != "config" {
			t.Fatalf("unexpected reply %v", r)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("no reply")
	}
}

func TestRetryDelay(t *testing.T) {
	cases := []struct {
		fails int
		every time.Duration
		want  time.Duration
	}{{1, time.Hour, 15 * time.Second}, {3, time.Hour, time.Minute}, {20, time.Hour, 16 * time.Minute}, {2, 10 * time.Second, 10 * time.Second}}
	for _, c := range cases {
		if got := retryDelay(c.fails, c.every); got != c.want {
			t.Errorf("retryDelay(%d, %s) = %s, want %s", c.fails, c.every, got, c.want)
		}
	}
}

func TestWebPage(t *testing.T) {
	d, h, _ := setup(t, "serial", "")
	d.send(hello(""))
	d.expect("hello")
	web := func() string {
		t.Helper()
		// The session reads in the background; give it a moment for the last line.
		for range 50 {
			if devs := h.Devices("buddy-test"); len(devs) == 1 && devs[0].Info().Web != "" {
				return devs[0].Info().Web
			}
			time.Sleep(10 * time.Millisecond)
		}
		return ""
	}
	d.send(protocol.Web{T: "web", URL: "http://192.168.1.23/"})
	if got := web(); got != "http://192.168.1.23/" {
		t.Fatalf("web = %q", got)
	}
	d.send(protocol.Web{T: "web", URL: "javascript:alert(1)"})
	d.send(protocol.Ping{T: "ping"})
	time.Sleep(50 * time.Millisecond)
	if got := h.Devices("buddy-test")[0].Info().Web; got != "" {
		t.Fatalf("a non-http address was kept: %q", got)
	}
}
