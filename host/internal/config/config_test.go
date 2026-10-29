package config

import (
	"os"
	"path/filepath"
	"testing"
)

func TestLoadLayers(t *testing.T) {
	dir := t.TempDir()
	secret := filepath.Join(dir, "tok")
	os.WriteFile(secret, []byte("s3cret\n"), 0o600)
	path := filepath.Join(dir, "config.yaml")
	os.WriteFile(path, []byte("tcp:\n  enabled: true\n  token: ${file:"+secret+"}\n  tokens: {desk: \"${file:"+secret+"}\"}\nsources:\n  deny: [sys]\n"), 0o600)
	t.Setenv("BUDDY_LINK_TCP_LISTEN", ":9999")
	t.Setenv("BUDDY_LINK_SERIAL_VID_PID", "303a, 1a86:7523")

	cfg, found, err := Load(path)
	if err != nil || !found {
		t.Fatalf("load: %v found=%v", err, found)
	}
	if !cfg.TCP.Enabled || cfg.TCP.Listen != ":9999" || cfg.TCP.Token != "s3cret" {
		t.Fatalf("tcp layering wrong: %+v", cfg.TCP)
	}
	if len(cfg.Sources.Deny) != 1 || len(cfg.Serial.VIDPID) != 2 || cfg.Control.Listen != "127.0.0.1:7532" {
		t.Fatalf("unexpected cfg %+v", cfg)
	}
	if cfg.Masked().TCP.Token != "***" || cfg.Masked().TCP.Tokens["desk"] != "***" || cfg.TCP.Tokens["desk"] != "s3cret" {
		t.Fatal("token not masked")
	}
}

func TestLoadErrors(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "config.yaml")
	os.WriteFile(path, []byte("tcp:\n  enabledd: true\n"), 0o600)
	if _, _, err := Load(path); err == nil {
		t.Fatal("unknown key must be rejected")
	}
	os.WriteFile(path, []byte("serial:\n  vid_pid: [xyz]\n"), 0o600)
	if _, _, err := Load(path); err == nil {
		t.Fatal("bad vid_pid must be rejected")
	}
	if _, found, err := Load(filepath.Join(dir, "missing.yaml")); err != nil || found {
		t.Fatalf("missing file should give defaults: %v", err)
	}
	os.WriteFile(path, nil, 0o600)
	if _, _, err := Load(path); err != nil {
		t.Fatalf("empty file should be fine: %v", err)
	}
}

func TestControlTokenCreated(t *testing.T) {
	t.Setenv("BUDDY_LINK_CONFIG_DIR", t.TempDir())
	t.Setenv("BUDDY_LINK_CONTROL_TOKEN", "")
	a, err := ControlToken(Default(), true)
	if err != nil || len(a) != 32 {
		t.Fatalf("token %q err %v", a, err)
	}
	b, _ := ControlToken(Default(), false)
	if a != b {
		t.Fatal("token not persisted")
	}
}
