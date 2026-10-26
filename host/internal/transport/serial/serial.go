// Package serial finds Blinky devices on USB serial ports and runs a session per port.
// It works the same on Windows (COMx), macOS (/dev/cu.*) and Linux (/dev/ttyACM*, /dev/ttyUSB*).
package serial

import (
	"context"
	"log/slog"
	"runtime"
	"strings"
	"sync"
	"time"

	"go.bug.st/serial"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/config"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/session"
)

const scanEvery = 2 * time.Second

// Run scans for ports and serves every matching one until ctx ends.
func Run(ctx context.Context, cfg config.Serial, opt session.Options) {
	var mu sync.Mutex
	active := map[string]bool{}
	var wg sync.WaitGroup
	defer wg.Wait()

	warned := &warnSet{m: map[string]bool{}}
	ticker := time.NewTicker(scanEvery)
	defer ticker.Stop()
	for {
		for _, name := range candidates(cfg, warned) {
			mu.Lock()
			busy := active[name]
			if !busy {
				active[name] = true
			}
			mu.Unlock()
			if busy {
				continue
			}
			wg.Add(1)
			go func(name string) {
				defer wg.Done()
				defer func() {
					mu.Lock()
					delete(active, name)
					mu.Unlock()
				}()
				servePort(ctx, name, cfg, opt, warned)
			}(name)
		}
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
		}
	}
}

// candidates returns the port names to open: the configured port, or USB ports
// whose VID/PID match the filter.
func candidates(cfg config.Serial, warned *warnSet) []string {
	if cfg.Port != "" && cfg.Port != "auto" {
		return []string{cfg.Port}
	}
	ports, err := listUSBPorts()
	if err != nil {
		if warned.first("enum") {
			slog.Warn("serial: cannot list ports", "err", err)
		}
		return nil
	}
	var out []string
	for _, p := range ports {
		if p.VID != "" && !matches(cfg.VIDPID, p.VID, p.PID) {
			continue
		}
		// On macOS every device has a tty.* and a cu.* node. cu.* doesn't wait for carrier.
		if runtime.GOOS == "darwin" && strings.HasPrefix(p.Name, "/dev/tty.") {
			continue
		}
		out = append(out, p.Name)
	}
	return out
}

// usbPort is a USB serial port. VID/PID are empty when the platform can't tell.
type usbPort struct {
	Name, VID, PID string
}

func matches(filter []string, vid, pid string) bool {
	vid, pid = strings.ToLower(vid), strings.ToLower(pid)
	for _, f := range filter {
		fv, fp, hasPID := strings.Cut(strings.ToLower(f), ":")
		if fv == vid && (!hasPID || fp == pid) {
			return true
		}
	}
	return false
}

func servePort(ctx context.Context, name string, cfg config.Serial, opt session.Options, warned *warnSet) {
	baud := cfg.Baud
	if baud == 0 {
		baud = 115200
	}
	port, err := serial.Open(name, &serial.Mode{BaudRate: baud})
	if err != nil {
		if warned.first("open:" + name) {
			slog.Warn("serial: cannot open port", "port", name, "err", err)
		}
		return
	}
	warned.clear("open:" + name)
	// DTR/RTS low: the ESP32 auto-reset circuit must not reset the board.
	port.SetDTR(false)
	port.SetRTS(false)
	slog.Info("serial: opened", "port", name)

	done := make(chan struct{})
	defer close(done)
	go func() {
		select {
		case <-ctx.Done():
		case <-done:
		}
		port.Close()
	}()
	o := opt
	o.Transport = "serial"
	o.Addr = name
	o.Token = "" // a USB connection is local; no token check
	err = session.New(port, o).Serve(ctx)
	if ctx.Err() == nil {
		slog.Info("serial: closed", "port", name, "err", err)
	}
}

// warnSet logs repeated problems only once until they are cleared.
type warnSet struct {
	mu sync.Mutex
	m  map[string]bool
}

func (w *warnSet) first(key string) bool {
	w.mu.Lock()
	defer w.mu.Unlock()
	if w.m[key] {
		return false
	}
	w.m[key] = true
	return true
}

func (w *warnSet) clear(key string) {
	w.mu.Lock()
	delete(w.m, key)
	w.mu.Unlock()
}
