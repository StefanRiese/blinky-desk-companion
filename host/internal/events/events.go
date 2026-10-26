// Package events is a small fan-out bus for device events (logs, sensors, links),
// consumed by the control API's event stream.
package events

import (
	"sync"
	"time"
)

// Event is one bus message.
type Event struct {
	Type string    `json:"type"` // connected, disconnected, log, event, sensor, sub_state, host_state
	Dev  string    `json:"dev,omitempty"`
	Time time.Time `json:"time"`
	Data any       `json:"data,omitempty"`
}

// Bus delivers events to subscribers. Slow subscribers lose events instead of blocking.
type Bus struct {
	mu   sync.Mutex
	subs map[chan Event]struct{}
}

// NewBus returns an empty bus.
func NewBus() *Bus { return &Bus{subs: map[chan Event]struct{}{}} }

// Publish sends an event to all subscribers.
func (b *Bus) Publish(typ, dev string, data any) {
	if b == nil {
		return
	}
	ev := Event{Type: typ, Dev: dev, Time: time.Now(), Data: data}
	b.mu.Lock()
	defer b.mu.Unlock()
	for ch := range b.subs {
		select {
		case ch <- ev:
		default:
		}
	}
}

// Subscribe returns a channel of events and a cancel function.
func (b *Bus) Subscribe() (<-chan Event, func()) {
	ch := make(chan Event, 64)
	b.mu.Lock()
	b.subs[ch] = struct{}{}
	b.mu.Unlock()
	return ch, func() {
		b.mu.Lock()
		delete(b.subs, ch)
		b.mu.Unlock()
	}
}
