// The device links of buddy-link (TCP with mDNS, USB serial) and its own settings. The web UI
// changes the settings through here: they are written to the config file, and the links
// they affect restart at once.
package main

import (
	"context"
	"errors"
	"fmt"
	"log/slog"
	"net"
	"os"
	"reflect"
	"slices"
	"sync"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/config"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/protocol"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/session"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/transport/serial"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/transport/tcp"
)

// links runs the transports and applies changed host settings.
type links struct {
	ctx    context.Context
	path   string               // the config file
	flags  func(*config.Config) // applies the command-line flags, which win over the file
	byFlag map[string]string    // setting → the flag that sets it
	opt    session.Options
	level  *slog.LevelVar

	mu      sync.Mutex
	cfg     config.Config
	tcp     *task
	serial  *task
	restart []string // saved settings that apply after a restart of buddy-link
}

// task is a goroutine that runs until it is stopped.
type task struct {
	cancel context.CancelFunc
	done   chan struct{}
}

func (l *links) goTask(f func(ctx context.Context)) *task {
	ctx, cancel := context.WithCancel(l.ctx)
	t := &task{cancel: cancel, done: make(chan struct{})}
	go func() {
		defer close(t.done)
		f(ctx)
	}()
	return t
}

func (t *task) stop() {
	if t != nil {
		t.cancel()
		<-t.done
	}
}

// start runs the transports of cfg. An address in use is an error.
func (l *links) start(cfg config.Config) error {
	l.mu.Lock()
	defer l.mu.Unlock()
	l.cfg = cfg
	var err error
	if l.tcp, err = l.startTCP(cfg, nil); err != nil {
		return err
	}
	l.serial = l.startSerial(cfg)
	return nil
}

// wait returns when the context is done and the transports have ended.
func (l *links) wait() {
	<-l.ctx.Done()
	l.mu.Lock()
	defer l.mu.Unlock()
	l.tcp.stop()
	l.serial.stop()
}

// startTCP serves cfg's TCP transport, on ln if it is open already.
func (l *links) startTCP(cfg config.Config, ln net.Listener) (*task, error) {
	if !cfg.TCP.Enabled {
		return nil, nil
	}
	if ln == nil {
		var err error
		if ln, err = net.Listen("tcp", cfg.TCP.Listen); err != nil {
			return nil, fmt.Errorf("tcp: %w", err)
		}
	}
	slog.Info("tcp: listening", "addr", ln.Addr().String())
	opt := l.opt
	opt.Token, opt.Tokens = cfg.TCP.Token, cfg.TCP.Tokens
	return l.goTask(func(ctx context.Context) {
		var wg sync.WaitGroup
		if cfg.TCP.MDNS {
			wg.Go(func() { announce(ctx, cfg, opt.HostName) })
		}
		tcp.ServeListener(ctx, ln, opt)
		wg.Wait()
	}), nil
}

func (l *links) startSerial(cfg config.Config) *task {
	if !cfg.Serial.Enabled {
		return nil
	}
	return l.goTask(func(ctx context.Context) { serial.Run(ctx, cfg.Serial, l.opt) })
}

// Config returns the settings in use.
func (l *links) Config() config.Config {
	l.mu.Lock()
	defer l.mu.Unlock()
	return l.cfg
}

// apply restarts what changed between the settings in use and next. If the new TCP
// address can't be used, the old one stays.
func (l *links) apply(next config.Config) error {
	var level slog.Level
	if err := level.UnmarshalText([]byte(next.Log.Level)); err != nil {
		return fmt.Errorf("log.level: %w", err)
	}
	cur := l.cfg
	if !reflect.DeepEqual(next.TCP, cur.TCP) {
		// Open a new address first, so a busy one leaves the devices connected. If that
		// fails, the old listener may be in the way (":7531" and "127.0.0.1:7531").
		var ln net.Listener
		if next.TCP.Enabled && next.TCP.Listen != cur.TCP.Listen {
			var err error
			if ln, err = net.Listen("tcp", next.TCP.Listen); err != nil && (!cur.TCP.Enabled || port(next.TCP.Listen) != port(cur.TCP.Listen)) {
				return fmt.Errorf("tcp: %w", err)
			}
		}
		l.tcp.stop()
		t, err := l.startTCP(next, ln)
		if err != nil {
			l.tcp, _ = l.startTCP(cur, nil)
			return err
		}
		l.tcp = t
	}
	if !reflect.DeepEqual(next.Serial, cur.Serial) {
		l.serial.stop()
		l.serial = l.startSerial(next)
	}
	l.level.Set(level)
	if next.Proxy != cur.Proxy && !slices.Contains(l.restart, "proxy.url") {
		l.restart = append(l.restart, "proxy.url")
	}
	if next.GUI != cur.GUI && !slices.Contains(l.restart, "gui.listen") {
		l.restart = append(l.restart, "gui.listen")
	}
	l.cfg = next
	return nil
}

// Settings returns the editable settings for the web UI: the values in use (secrets
// masked), the ones set elsewhere (flag, env var, reference in the file) and the ones that
// apply after a restart.
func (l *links) Settings() map[string]any {
	l.mu.Lock()
	defer l.mu.Unlock()
	values := map[string]any{}
	for _, k := range config.Editable {
		v, _ := l.cfg.Value(k)
		if s, ok := v.(string); ok && config.Secret(k) && s != "" {
			v = protocol.Masked
		}
		values[k] = v
	}
	return map[string]any{"path": l.path, "values": values, "locked": l.locked(), "restart": slices.Clone(l.restart)}
}

// locked returns the settings that the config file doesn't decide, and why.
func (l *links) locked() map[string]string {
	out := map[string]string{}
	b, _ := os.ReadFile(l.path)
	for _, k := range config.References(b) {
		out[k] = "a reference in the config file"
	}
	for _, k := range config.Editable {
		if _, ok := os.LookupEnv(config.EnvName(k)); ok {
			out[k] = "the environment variable " + config.EnvName(k)
		}
		if f, ok := l.byFlag[k]; ok {
			out[k] = "the command-line flag " + f
		}
	}
	return out
}

// Set writes set (setting → value) to the config file and applies it. Nothing is written
// if a value is invalid or a link can't start with it.
func (l *links) Set(set map[string]any) error {
	l.mu.Lock()
	defer l.mu.Unlock()
	locked := l.locked()
	for k, v := range set {
		if why, ok := locked[k]; ok {
			return fmt.Errorf("%s is set by %s", k, why)
		}
		if v == protocol.Masked && config.Secret(k) {
			delete(set, k) // the masked value keeps the stored secret
		}
	}
	if len(set) == 0 {
		return nil
	}
	old, err := os.ReadFile(l.path)
	if err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}
	b, err := config.Edit(old, set)
	if err != nil {
		return err
	}
	next, err := config.FromYAML(b)
	if err != nil {
		return err
	}
	l.flags(&next)
	cur := l.cfg
	if err := l.apply(next); err != nil {
		return err
	}
	if err := config.WriteFile(l.path, b); err != nil {
		l.apply(cur)
		return fmt.Errorf("cannot save %s: %w", l.path, err)
	}
	slog.Info("host settings saved", "file", l.path, "keys", len(set))
	return nil
}

// port returns the port of a listen address.
func port(addr string) string {
	_, p, _ := net.SplitHostPort(addr)
	return p
}
