package sim

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/protocol"
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

func TestLocationAuto(t *testing.T) {
	n, err := LoadNVS(filepath.Join(t.TempDir(), "nvs.json"), "buddy-t")
	if err != nil {
		t.Fatal(err)
	}
	d := New(n, nil)
	if !n.Bool("location.auto") {
		t.Fatal("location.auto should be on by default")
	}
	d.mu.Lock()
	d.rerouteLocked()
	if d.assign["geo"] == nil {
		t.Fatal("no geo source while location.auto is on")
	}
	d.mu.Unlock()

	// A found location becomes the device's location.
	d.applyResult(protocol.Envelope{T: "data", Raw: []byte(
		`{"t":"data","kv":{"geo.lat":53.5507,"geo.lon":9.993,"geo.city":"Hamburg","geo.tz":"Europe/London"}}`)}, "host")
	if n.Float("location.lat", 0) != 53.5507 || n.String("location.name") != "Hamburg" ||
		n.String("location.tz") != "Europe/London" || !n.Bool("location.auto") {
		t.Fatalf("location not applied: %v", n.Tree()["location"])
	}

	// Unchanged values keep it on; a location set by hand turns it off and drops the lookup.
	if err := n.SetMany(map[string]any{"location.lat": 53.5507, "units.temp": "F"}); err != nil || !n.Bool("location.auto") {
		t.Fatal("saving the same location turned location.auto off")
	}
	if err := n.SetMany(map[string]any{"location.lat": 48.1}); err != nil || n.Bool("location.auto") {
		t.Fatal("a location set by hand kept location.auto on")
	}
	d.mu.Lock()
	d.rerouteLocked()
	if d.assign["geo"] != nil {
		t.Fatal("geo source still there with location.auto off")
	}
	d.mu.Unlock()
	d.applyGeo()
	if n.Float("location.lat", 0) != 48.1 {
		t.Fatal("found location applied while location.auto is off")
	}
}

func TestNVSColor(t *testing.T) {
	path := filepath.Join(t.TempDir(), "nvs.json")
	// A file saved before ui.color existed still loads.
	if err := os.WriteFile(path, []byte(`{"dev":"buddy-t","ui":{"rotation":"0"}}`), 0o644); err != nil {
		t.Fatal(err)
	}
	n, err := LoadNVS(path, "buddy-t")
	if err != nil {
		t.Fatalf("old file: %v", err)
	}
	if err := n.SetMany(map[string]any{"ui.color": "green"}); err == nil {
		t.Fatal("ui.color must be #RRGGBB")
	}
	if err := n.SetMany(map[string]any{"ui.color": "#4ade80"}); err != nil {
		t.Fatal(err)
	}
}
