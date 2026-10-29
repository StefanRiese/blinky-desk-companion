// Package setupclient talks to a Blinky on a serial port the way the setup page does: as a
// "setup" peer, which may read and change settings but gets no data. buddy-ctl --direct
// uses it when no buddy-link is running.
package setupclient

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"sync"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/protocol"
)

// Client is a connection to one device.
type Client struct {
	Hello protocol.Hello // the device's hello: dev, fw, board

	w       *protocol.Writer
	mu      sync.Mutex
	nextID  int
	pending map[int]chan map[string]any
	closed  chan struct{}
}

// Dial asks the device on rw to introduce itself and answers as a setup peer. It gives up
// when ctx ends (no Blinky firmware on the port).
func Dial(ctx context.Context, rw io.ReadWriter, host string) (*Client, error) {
	c := &Client{w: protocol.NewWriter(rw), nextID: 1, pending: map[int]chan map[string]any{}, closed: make(chan struct{})}
	hello := make(chan protocol.Hello, 1)
	go c.read(rw, hello)
	ask := protocol.Hello{T: "hello", V: protocol.Version, Host: host, Role: "setup"}
	tick := time.NewTicker(2 * time.Second)
	defer tick.Stop()
	for {
		if _, err := c.w.Write(ask); err != nil {
			return nil, err
		}
		select {
		case h := <-hello:
			c.Hello = h
			reply := protocol.Hello{T: "hello", V: protocol.Version, Dev: h.Dev, Host: host, Role: "setup"}
			if _, err := c.w.Write(reply); err != nil {
				return nil, err
			}
			return c, nil
		case <-c.closed:
			return nil, errors.New("the port closed")
		case <-ctx.Done():
			return nil, errors.New("no answer: is the Blinky firmware on this port?")
		case <-tick.C:
		}
	}
}

func (c *Client) read(r io.Reader, hello chan<- protocol.Hello) {
	defer close(c.closed)
	rd := protocol.NewReader(r)
	for {
		line, err := rd.ReadLine()
		if err != nil {
			return
		}
		e, err := protocol.Decode(line)
		if err != nil {
			continue
		}
		if e.T == "hello" {
			var h protocol.Hello
			if e.As(&h) == nil && h.Dev != "" {
				select {
				case hello <- h:
				default:
				}
			}
			continue
		}
		if e.ID == nil {
			continue
		}
		var m map[string]any
		if json.Unmarshal(line, &m) != nil {
			continue
		}
		c.mu.Lock()
		ch := c.pending[*e.ID]
		delete(c.pending, *e.ID)
		c.mu.Unlock()
		if ch != nil {
			ch <- m
		}
	}
}

// Request sends msg with a fresh id and returns the device's reply (config, ack or err).
func (c *Client) Request(ctx context.Context, msg map[string]any) (map[string]any, error) {
	c.mu.Lock()
	id := c.nextID
	c.nextID++
	ch := make(chan map[string]any, 1)
	c.pending[id] = ch
	c.mu.Unlock()
	msg["id"] = id
	if _, err := c.w.Write(msg); err != nil {
		return nil, err
	}
	select {
	case m := <-ch:
		if m["t"] == "err" {
			return m, fmt.Errorf("%v: %v", m["code"], m["msg"])
		}
		return m, nil
	case <-c.closed:
		return nil, errors.New("the port closed")
	case <-ctx.Done():
		c.mu.Lock()
		delete(c.pending, id)
		c.mu.Unlock()
		return nil, errors.New("no reply from the device")
	}
}
