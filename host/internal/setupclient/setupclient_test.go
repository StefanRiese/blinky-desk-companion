package setupclient

import (
	"context"
	"encoding/json"
	"net"
	"testing"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/protocol"
)

// fakeDevice answers like the firmware: its hello to a hello without dev, then config.
func fakeDevice(t *testing.T, conn net.Conn) {
	w, r := protocol.NewWriter(conn), protocol.NewReader(conn)
	go func() {
		for {
			line, err := r.ReadLine()
			if err != nil {
				return
			}
			var m map[string]any
			json.Unmarshal(line, &m)
			switch {
			case m["t"] == "hello" && m["dev"] == nil:
				if m["role"] != "setup" {
					t.Errorf("hello without role setup: %s", line)
				}
				w.Write(protocol.Hello{T: "hello", V: 1, Dev: "desk", FW: "1.2"})
			case m["t"] == "config" && m["get"] != nil:
				w.Write(map[string]any{"t": "config", "id": m["id"], "values": map[string]any{"dev": "desk"}})
			case m["t"] == "config":
				w.Write(map[string]any{"t": "err", "id": m["id"], "code": "bad_value", "msg": "nope"})
			}
		}
	}()
}

func TestDialAndRequest(t *testing.T) {
	a, b := net.Pipe()
	defer a.Close()
	defer b.Close()
	fakeDevice(t, b)
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	c, err := Dial(ctx, a, "test")
	if err != nil {
		t.Fatal(err)
	}
	if c.Hello.Dev != "desk" || c.Hello.FW != "1.2" {
		t.Fatalf("hello %+v", c.Hello)
	}
	m, err := c.Request(ctx, map[string]any{"t": "config", "get": []string{"dev"}})
	if err != nil || m["values"].(map[string]any)["dev"] != "desk" {
		t.Fatalf("get: %v %v", m, err)
	}
	if _, err := c.Request(ctx, map[string]any{"t": "config", "set": map[string]any{"x": 1}}); err == nil {
		t.Fatal("an err reply must be an error")
	}
}

func TestDialGivesUp(t *testing.T) {
	a, b := net.Pipe()
	defer a.Close()
	defer b.Close()
	go func() { // a port without Blinky firmware: reads, never answers
		buf := make([]byte, 256)
		for {
			if _, err := b.Read(buf); err != nil {
				return
			}
		}
	}()
	ctx, cancel := context.WithTimeout(context.Background(), 300*time.Millisecond)
	defer cancel()
	if _, err := Dial(ctx, a, "test"); err == nil {
		t.Fatal("want an error without an answer")
	}
}
