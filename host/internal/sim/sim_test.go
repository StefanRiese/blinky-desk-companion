package sim

import (
	"context"
	"encoding/json"
	"net"
	"path/filepath"
	"testing"
	"time"

	"github.com/StefanRiese/esp32_desk_buddy/host/internal/events"
	"github.com/StefanRiese/esp32_desk_buddy/host/internal/fetch"
	"github.com/StefanRiese/esp32_desk_buddy/host/internal/hub"
	"github.com/StefanRiese/esp32_desk_buddy/host/internal/protocol"
	"github.com/StefanRiese/esp32_desk_buddy/host/internal/session"
	"github.com/StefanRiese/esp32_desk_buddy/host/internal/source"
	"github.com/StefanRiese/esp32_desk_buddy/host/internal/transport/tcp"
)

type fakeSys struct{}

func (fakeSys) Name() string                   { return "sys" }
func (fakeSys) NeedsInternet() bool            { return false }
func (fakeSys) MinInterval() time.Duration     { return time.Second }
func (fakeSys) DefaultInterval() time.Duration { return 10 * time.Second }
func (fakeSys) Validate(json.RawMessage) error { return nil }
func (fakeSys) Fetch(context.Context, json.RawMessage) (source.Result, error) {
	return source.Result{Data: map[string]any{"cpu": 12.5}}, nil
}

// TestEmulatorAgainstHost runs the emulated device against a real buddyd session over TCP.
func TestEmulatorAgainstHost(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	ctx := t.Context()
	h := hub.New(events.NewBus())
	go tcp.ServeListener(ctx, ln, session.Options{HostName: "buddyd@test", Version: "test", Token: "tok",
		Registry: source.NewRegistryWith(source.Time{}, fakeSys{}), Hub: h})

	nvs, err := LoadNVS(filepath.Join(t.TempDir(), "nvs.json"), "buddy-e2e")
	if err != nil {
		t.Fatal(err)
	}
	if err := nvs.SetMany(map[string]any{
		"link.tcp_host": ln.Addr().String(), "link.token": "tok",
		"sources": []any{
			map[string]any{"sid": "time", "src": "time"},
			map[string]any{"sid": "sys", "src": "sys", "every": 1},
			map[string]any{"sid": "wx", "src": "weather"}, // host doesn't offer it, WiFi has no internet
		},
	}); err != nil {
		t.Fatal(err)
	}
	client, _ := fetch.New(fetch.Options{})
	dev := New(nvs, client)
	go dev.Run(ctx)

	deadline := time.Now().Add(5 * time.Second)
	for {
		st := dev.Snapshot()
		subs := map[string]SubView{}
		for _, s := range st.Subs {
			subs[s.SID] = s
		}
		_, haveCPU := dev.store.Fresh("sys.cpu")
		if st.Link.State == linkReady && st.ClockSet && haveCPU &&
			subs["time"].Via == "host" && subs["time"].State == "ok" &&
			subs["sys"].State == "ok" && subs["wx"].Via == "none" {
			break
		}
		if time.Now().After(deadline) {
			t.Fatalf("device not ready: link=%s clock=%v subs=%+v", st.Link.State, st.ClockSet, st.Subs)
		}
		time.Sleep(50 * time.Millisecond)
	}

	// The host can reach the device: notify + config get.
	if n := h.Send("buddy-e2e", protocol.Notify{T: "notify", Text: "hi", Mood: "love"}); n != 1 {
		t.Fatalf("notify sent to %d devices", n)
	}
	rctx, rcancel := context.WithTimeout(ctx, 2*time.Second)
	defer rcancel()
	reply, err := h.Devices("buddy-e2e")[0].Request(rctx, map[string]any{"t": "config", "get": []string{"link"}})
	if err != nil {
		t.Fatal(err)
	}
	vals, _ := reply["values"].(map[string]any)
	if vals["link.token"] != protocol.Masked {
		t.Fatalf("config get must mask secrets, got %v", reply)
	}
	time.Sleep(100 * time.Millisecond)
	if st := dev.Snapshot(); st.Notify == nil || st.Mood != "love" {
		t.Fatalf("notify not shown: %+v mood=%s", st.Notify, st.Mood)
	}
}
