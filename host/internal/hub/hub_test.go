package hub

import (
	"context"
	"testing"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/events"
)

type dev struct{ id string }

func (d *dev) ID() string                                                      { return d.id }
func (d *dev) Send(any) error                                                  { return nil }
func (d *dev) Info() Info                                                      { return Info{Dev: d.id, Online: true, Web: "http://x/"} }
func (d *dev) Request(context.Context, map[string]any) (map[string]any, error) { return nil, nil }

func TestOffline(t *testing.T) {
	h := New(events.NewBus())
	a, a2, b := &dev{"a"}, &dev{"a"}, &dev{"b"}
	h.Add(a)
	h.Add(a2) // the same device over USB and TCP
	h.Add(b)
	h.Remove(a)
	if len(h.Offline()) != 0 {
		t.Fatal("a is still connected over its other link")
	}
	h.Remove(a2)
	h.Remove(b)
	off := h.Offline()
	if len(off) != 2 || off[0].Online || off[0].GoneAt == nil || off[0].Web != "" {
		t.Fatalf("offline %+v", off)
	}
	h.Add(b)
	if off := h.Offline(); len(off) != 1 || off[0].Dev != "a" {
		t.Fatalf("b came back: %+v", off)
	}
}
