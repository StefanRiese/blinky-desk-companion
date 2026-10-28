package config

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestEditKeepsCommentsAndOtherSettings(t *testing.T) {
	in := `# Buddy Link
serial:
  port: auto # the first USB port
tcp: {listen: ":7531"}
log:
  level: info
sources:
  deny: [sys]
`
	out, err := Edit([]byte(in), map[string]any{"tcp.enabled": true, "serial.enabled": false, "proxy.url": "http://proxy:3128"})
	if err != nil {
		t.Fatal(err)
	}
	s := string(out)
	for _, want := range []string{"# Buddy Link", "# the first USB port", "deny: [sys]", "level: info"} {
		if !strings.Contains(s, want) {
			t.Errorf("lost %q:\n%s", want, s)
		}
	}
	cfg, err := FromYAML(out)
	if err != nil {
		t.Fatalf("%v\n%s", err, s)
	}
	if !cfg.TCP.Enabled || cfg.TCP.Listen != ":7531" || cfg.Serial.Enabled || cfg.Serial.Port != "auto" ||
		cfg.Proxy.URL != "http://proxy:3128" || len(cfg.Sources.Deny) != 1 {
		t.Fatalf("edited config: %+v\n%s", cfg, s)
	}

	if _, err := Edit([]byte(in), map[string]any{"tcp.enabled": "yes"}); err == nil {
		t.Error("a string for a bool must be an error")
	}
	if _, err := Edit([]byte(in), map[string]any{"control.listen": ":1"}); err == nil {
		t.Error("control.listen must not be editable")
	}
	if out, err := Edit(nil, map[string]any{"tcp.enabled": true}); err != nil || !strings.Contains(string(out), "enabled: true") {
		t.Errorf("empty file: %v %q", err, out)
	}
}

func TestValueAndReferences(t *testing.T) {
	cfg := Default()
	if v, ok := cfg.Value("tcp.listen"); !ok || v != ":7531" {
		t.Errorf("tcp.listen = %v %v", v, ok)
	}
	if _, ok := cfg.Value("tcp.nope"); ok {
		t.Error("unknown path found")
	}
	refs := References([]byte("tcp:\n  token: ${file:/run/secrets/tok}\n  listen: \":1\"\n"))
	if len(refs) != 1 || refs[0] != "tcp.token" {
		t.Errorf("references: %v", refs)
	}
	if EnvName("serial.port") != "BUDDY_LINK_SERIAL_PORT" {
		t.Error(EnvName("serial.port"))
	}
}

func TestWriteFile(t *testing.T) {
	path := filepath.Join(t.TempDir(), "sub", "config.yaml")
	if err := WriteFile(path, []byte("a: 1\n")); err != nil {
		t.Fatal(err)
	}
	if err := WriteFile(path, []byte("a: 2\n")); err != nil {
		t.Fatal(err)
	}
	b, _ := os.ReadFile(path)
	entries, _ := os.ReadDir(filepath.Dir(path))
	if string(b) != "a: 2\n" || len(entries) != 1 {
		t.Fatalf("got %q, %d files", b, len(entries))
	}
}
