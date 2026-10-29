// Package hub keeps track of connected device sessions and the host's internet state.
package hub

import (
	"context"
	"sort"
	"sync"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/events"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/protocol"
)

// Device is what the hub needs from a session.
type Device interface {
	ID() string
	Send(v any) error
	Request(ctx context.Context, msg map[string]any) (map[string]any, error)
	Info() Info
}

// Info describes a connected device for the control API.
type Info struct {
	Dev         string         `json:"dev"`
	FW          string         `json:"fw,omitempty"`
	Board       string         `json:"board,omitempty"`
	Caps        []string       `json:"caps,omitempty"`
	Transport   string         `json:"transport"`
	Addr        string         `json:"addr"`
	ConnectedAt time.Time      `json:"connected_at"`
	LastSeen    time.Time      `json:"last_seen"`
	Sensor      map[string]any `json:"sensor,omitempty"`
	Web         string         `json:"web,omitempty"` // the device's own web page
	Online      bool           `json:"online"`
	GoneAt      *time.Time     `json:"gone_at,omitempty"` // when an offline device disconnected
	Subs        []SubInfo      `json:"subs"`
}

// SubInfo describes one subscription (params masked).
type SubInfo struct {
	SID    string    `json:"sid"`
	Src    string    `json:"src"`
	Every  int       `json:"every"`
	State  string    `json:"state"`
	Msg    string    `json:"msg,omitempty"`
	LastOK time.Time `json:"last_ok"`
	Params any       `json:"params,omitempty"`
}

// Hub is the registry of active devices.
type Hub struct {
	Bus *events.Bus

	mu       sync.Mutex
	devices  map[Device]struct{}
	gone     map[string]Info // disconnected devices by id, kept for KeepGone
	net      bool
	netKnown bool
}

// KeepGone is how long a disconnected device stays in the Offline list (in memory only:
// buddy-link stores nothing).
const KeepGone = 24 * time.Hour

// New returns an empty hub. The internet state starts as "online" until checked.
func New(bus *events.Bus) *Hub {
	return &Hub{Bus: bus, devices: map[Device]struct{}{}, gone: map[string]Info{}, net: true}
}

// Add registers a device after its hello.
func (h *Hub) Add(d Device) {
	id := d.ID()
	h.mu.Lock()
	h.devices[d] = struct{}{}
	delete(h.gone, id)
	h.mu.Unlock()
}

// Remove unregisters a device and remembers it as offline, unless it is still connected
// another way.
func (h *Hub) Remove(d Device) {
	info := d.Info()
	h.mu.Lock()
	defer h.mu.Unlock()
	delete(h.devices, d)
	if info.Dev == "" {
		return
	}
	for other := range h.devices {
		if other.ID() == info.Dev {
			return
		}
	}
	now := time.Now()
	info.Online, info.GoneAt, info.Subs, info.Web = false, &now, nil, ""
	h.gone[info.Dev] = info
}

// Offline returns the devices that disconnected within KeepGone, newest first.
func (h *Hub) Offline() []Info {
	h.mu.Lock()
	defer h.mu.Unlock()
	var out []Info
	for id, info := range h.gone {
		if time.Since(*info.GoneAt) > KeepGone {
			delete(h.gone, id)
			continue
		}
		out = append(out, info)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].GoneAt.After(*out[j].GoneAt) })
	return out
}

// Devices returns all devices, or those with id dev (several may share an id).
func (h *Hub) Devices(dev string) []Device {
	h.mu.Lock()
	defer h.mu.Unlock()
	var out []Device
	for d := range h.devices {
		if dev == "" || d.ID() == dev {
			out = append(out, d)
		}
	}
	sort.Slice(out, func(i, j int) bool { return out[i].ID() < out[j].ID() })
	return out
}

// Send sends a message to all devices or to devices with id dev. It returns the count.
func (h *Hub) Send(dev string, v any) int {
	n := 0
	for _, d := range h.Devices(dev) {
		if d.Send(v) == nil {
			n++
		}
	}
	return n
}

// Net returns the host's internet state.
func (h *Hub) Net() bool {
	h.mu.Lock()
	defer h.mu.Unlock()
	return h.net
}

// SetNet updates the internet state and tells all devices when it changed.
func (h *Hub) SetNet(net bool) {
	h.mu.Lock()
	changed := h.net != net || !h.netKnown
	h.net, h.netKnown = net, true
	h.mu.Unlock()
	if !changed {
		return
	}
	h.Send("", protocol.HostState{T: "host_state", Net: net})
	h.Bus.Publish("host_state", "", map[string]bool{"net": net})
}
