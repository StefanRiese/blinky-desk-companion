package sim

import (
	"path/filepath"
	"testing"
)

func TestNVSSecretsAndSet(t *testing.T) {
	n, err := LoadNVS(filepath.Join(t.TempDir(), "nvs.json"), "buddy-t")
	if err != nil {
		t.Fatal(err)
	}
	if err := n.SetMany(map[string]any{"link.token": "tok", "wifi.pass": "pw"}); err != nil {
		t.Fatal(err)
	}
	m := n.Masked()
	if m["link"].(map[string]any)["token"] != "***" {
		t.Fatal("token not masked")
	}
	// Saving the masked tree back keeps the secrets.
	if err := n.Replace(m); err != nil {
		t.Fatal(err)
	}
	if n.String("link.token") != "tok" || n.String("wifi.pass") != "pw" {
		t.Fatal("secrets lost on save of masked tree")
	}
	// Sources are matched by sid when restoring secrets.
	n.SetMany(map[string]any{"sources": []any{
		map[string]any{"sid": "a", "src": "http", "p": map[string]any{"token": "A"}},
		map[string]any{"sid": "b", "src": "http", "p": map[string]any{"token": "B"}},
	}})
	m = n.Masked()
	srcs := m["sources"].([]any)
	m["sources"] = []any{srcs[1], srcs[0]}
	if err := n.Replace(m); err != nil {
		t.Fatal(err)
	}
	cfgs, _ := n.Sources()
	if cfgs[0].SID != "b" || cfgs[0].P["token"] != "B" || cfgs[1].P["token"] != "A" {
		t.Fatalf("secrets not matched by sid: %+v", cfgs)
	}
	if err := n.SetMany(map[string]any{"sources": []any{map[string]any{"sid": "x"}}}); err == nil {
		t.Fatal("invalid sources accepted")
	}
	if n.String("dev") != "buddy-t" {
		t.Fatal("failed SetMany changed settings")
	}
	flat := n.Flat([]string{"link"})
	if flat["link.token"] != "***" || len(flat) != 3 {
		t.Fatalf("unexpected flat %v", flat)
	}
}
