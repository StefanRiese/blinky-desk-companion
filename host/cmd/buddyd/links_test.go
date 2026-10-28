package main

import (
	"context"
	"log/slog"
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/config"
)

func newLinks(t *testing.T, file string, byFlag map[string]string) *links {
	t.Helper()
	path := filepath.Join(t.TempDir(), "config.yaml")
	if file != "" {
		os.WriteFile(path, []byte(file), 0o600)
	}
	cfg, _, err := config.Load(path)
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	l := &links{ctx: ctx, path: path, flags: func(*config.Config) {}, byFlag: byFlag, level: new(slog.LevelVar)}
	if err := l.start(cfg); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { cancel(); l.wait() })
	return l
}

func freeAddr(t *testing.T) string {
	t.Helper()
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	return ln.Addr().String()
}

func TestLinksTurnTCPOnAndSave(t *testing.T) {
	l := newLinks(t, "# mine\nserial:\n  enabled: false\n", nil)
	addr := freeAddr(t)
	if err := l.Set(map[string]any{"tcp.enabled": true, "tcp.listen": addr, "tcp.mdns": false, "log.level": "debug"}); err != nil {
		t.Fatal(err)
	}
	conn, err := net.Dial("tcp", addr)
	if err != nil {
		t.Fatalf("TCP is not listening after Set: %v", err)
	}
	conn.Close()
	if l.level.Level() != slog.LevelDebug {
		t.Error("log level not applied")
	}
	b, _ := os.ReadFile(l.path)
	if !strings.Contains(string(b), "# mine") || !strings.Contains(string(b), "enabled: true") {
		t.Errorf("config file:\n%s", b)
	}

	// Off again: the port is free.
	if err := l.Set(map[string]any{"tcp.enabled": false}); err != nil {
		t.Fatal(err)
	}
	ln, err := net.Listen("tcp", addr)
	if err != nil {
		t.Fatalf("port still in use after turning TCP off: %v", err)
	}
	ln.Close()
}

func TestLinksKeepOldSettingsOnError(t *testing.T) {
	l := newLinks(t, "serial: {enabled: false}\n", map[string]string{"log.level": "--log-level"})
	busy, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer busy.Close()
	before, _ := os.ReadFile(l.path)

	if err := l.Set(map[string]any{"tcp.enabled": true, "tcp.listen": busy.Addr().String()}); err == nil {
		t.Error("a port in use must be an error")
	}
	// A busy address while TCP runs elsewhere: the running listener stays.
	addr := freeAddr(t)
	if err := l.Set(map[string]any{"tcp.enabled": true, "tcp.listen": addr, "tcp.mdns": false}); err != nil {
		t.Fatal(err)
	}
	before, _ = os.ReadFile(l.path)
	if err := l.Set(map[string]any{"tcp.listen": busy.Addr().String()}); err == nil {
		t.Error("a port in use must be an error")
	}
	if c, err := net.Dial("tcp", addr); err != nil {
		t.Errorf("the running listener was stopped: %v", err)
	} else {
		c.Close()
	}
	if err := l.Set(map[string]any{"tcp.enabled": "yes"}); err == nil {
		t.Error("a wrong type must be an error")
	}
	if err := l.Set(map[string]any{"log.level": "debug"}); err == nil || !strings.Contains(err.Error(), "--log-level") {
		t.Errorf("a setting from a flag must be locked: %v", err)
	}
	after, _ := os.ReadFile(l.path)
	if string(after) != string(before) || l.Config().TCP.Listen != addr {
		t.Errorf("failed changes were saved:\n%s", after)
	}
}

func TestLinksSecretStaysMasked(t *testing.T) {
	l := newLinks(t, "serial: {enabled: false}\ntcp:\n  token: s3cret\n", nil)
	if v := l.Settings()["values"].(map[string]any)["tcp.token"]; v != "***" {
		t.Errorf("token shown as %v", v)
	}
	if err := l.Set(map[string]any{"tcp.token": "***", "log.level": "warn"}); err != nil {
		t.Fatal(err)
	}
	if l.Config().TCP.Token != "s3cret" {
		t.Error("the masked value replaced the token")
	}
}
