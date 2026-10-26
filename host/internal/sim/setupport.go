package sim

import (
	"bufio"
	"fmt"
	"io"
	"net/http"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/protocol"
)

// The emulated USB port for the setup page. On the real device this is the USB serial
// port: the setup page (Web Serial) says hello with role "setup" and reads and writes
// settings with config messages. It does not touch the host link or its subscriptions.
//
// Over HTTP: GET /api/serial streams the device's lines as Server-Sent Events, and
// POST /api/serial sends lines (NDJSON) to the device. One page at a time; a new one
// takes the port over.

// setupHello introduces the device on the setup port (no link token: the page is not a host).
func (d *Device) setupHello() protocol.Hello {
	return protocol.Hello{T: "hello", V: protocol.Version, Dev: d.nvs.String("dev"), FW: d.nvs.String("fw"),
		Board: d.nvs.String("board"), Caps: []string{"eyes", "touch", "imu", "sht40", "sim"}}
}

// sendSetup sends a message to the setup page, if one is connected.
func (d *Device) sendSetup(v any) {
	line, err := protocol.Encode(v)
	if err != nil {
		return
	}
	d.mu.Lock()
	defer d.mu.Unlock()
	if d.setup == nil {
		return
	}
	select {
	case d.setup <- line:
		d.addLogLocked("out", "usb: "+string(protocol.MaskJSON(line)))
	default: // page too slow; like a full serial buffer, drop the line
	}
}

// handleSetup handles one line from the setup page.
func (d *Device) handleSetup(line []byte) {
	e, err := protocol.Decode(line)
	if err != nil {
		return
	}
	d.logf("in", "usb: %s", protocol.MaskJSON(line))
	defer d.notifyChange()
	switch e.T {
	case "hello":
		var h protocol.Hello
		if e.As(&h) == nil && h.Dev == "" { // the page opened the port: introduce ourselves
			d.mu.Lock()
			hello := d.setupHello()
			d.mu.Unlock()
			d.sendSetup(hello)
		}
	case "config":
		d.handleConfig(e, d.sendSetup)
	}
}

func (d *Device) serveSetupStream(w http.ResponseWriter, r *http.Request) {
	fl, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "streaming unsupported", http.StatusInternalServerError)
		return
	}
	ch := make(chan []byte, 64)
	d.mu.Lock()
	d.setup = ch
	d.addLogLocked("sys", "usb: setup page connected")
	d.mu.Unlock()
	defer func() {
		d.mu.Lock()
		if d.setup == ch {
			d.setup = nil
			d.addLogLocked("sys", "usb: setup page disconnected")
		}
		d.mu.Unlock()
		d.notifyChange()
	}()
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	fmt.Fprint(w, ": usb port open\n\n")
	fl.Flush()
	keep := time.NewTicker(15 * time.Second)
	defer keep.Stop()
	for {
		d.mu.Lock()
		taken := d.setup != ch
		d.mu.Unlock()
		if taken {
			return
		}
		select {
		case <-r.Context().Done():
			return
		case <-keep.C:
			fmt.Fprint(w, ": keepalive\n\n")
			fl.Flush()
		case line := <-ch:
			fmt.Fprintf(w, "data: %s\n\n", line)
			fl.Flush()
		}
	}
}

func (d *Device) serveSetupWrite(w http.ResponseWriter, r *http.Request) {
	sc := bufio.NewScanner(io.LimitReader(r.Body, 64<<10))
	sc.Buffer(make([]byte, protocol.MaxLine+1), protocol.MaxLine+1)
	for sc.Scan() {
		if line := sc.Bytes(); len(line) > 0 && line[0] == '{' {
			d.handleSetup(append([]byte(nil), line...))
		}
	}
	w.WriteHeader(http.StatusNoContent)
}
