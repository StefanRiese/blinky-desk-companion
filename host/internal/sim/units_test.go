package sim

import (
	"testing"
	"time"
)

func TestUnits(t *testing.T) {
	d := newTestDevice(t)
	d.nvs.SetMany(map[string]any{"mood.ambient": true}) // the mood's reason shows the indoor temperature
	if u, _ := d.nvs.Units(); u != (Units{Temp: "C", Wind: "kmh", Rain: "mm"}) {
		t.Fatalf("defaults: %+v", u)
	}
	if err := d.nvs.SetMany(map[string]any{"units.temp": "K"}); err == nil {
		t.Fatal("accepted units.temp=K")
	}
	if err := d.nvs.SetMany(map[string]any{"units.temp": "F", "units.wind": "mph"}); err != nil {
		t.Fatal(err)
	}
	u, _ := d.nvs.Units()
	if got := u.FmtTemp(26); got != "78.8 °F" {
		t.Fatalf("FmtTemp(26) = %q", got)
	}
	// Thresholds stay °C; only the mood text uses the display unit.
	d.store.Put("indoor.temp", 27.0, "local", time.Minute)
	if m, reason := d.moodLocked(time.Now()); m != "hot" || reason != "indoor 80.6 °F" {
		t.Fatalf("mood %q (%s)", m, reason)
	}
}

func TestBacklight(t *testing.T) {
	d := newTestDevice(t)
	now := time.Now()
	d.lastActive = now
	if b := d.backlightLocked(now); b != (BacklightView{200, "day"}) {
		t.Fatalf("day: %+v", b)
	}
	if b := d.backlightLocked(now.Add(61 * time.Second)); b != (BacklightView{80, "dim_day"}) {
		t.Fatalf("dimmed by day: %+v", b)
	}
	d.InputAt("tap", -1) // touch wakes the screen
	if b := d.backlightLocked(time.Now()); b.Mode != "day" {
		t.Fatalf("after touch: %+v", b)
	}

	// Make the current hour a night hour.
	d.clockSet = true
	h := d.localTimeLocked(now).Hour()
	d.nvs.SetMany(map[string]any{"ui.night_from": float64(h), "ui.night_to": float64((h + 1) % 24)})
	d.lastActive = now
	if b := d.backlightLocked(now); b != (BacklightView{40, "night"}) {
		t.Fatalf("night: %+v", b)
	}
	if b := d.backlightLocked(now.Add(61 * time.Second)); b.Mode != "dim_night" && b.Mode != "dim_day" {
		t.Fatalf("dimmed at night: %+v", b)
	}

	d.nvs.SetMany(map[string]any{"ui.dim_after": 0.0})
	if b := d.backlightLocked(now.Add(time.Hour)); b.Mode == "dim_day" || b.Mode == "dim_night" {
		t.Fatalf("dim_after=0 must never dim: %+v", b)
	}
	if err := d.nvs.SetMany(map[string]any{"ui.dim_night": 300.0}); err == nil {
		t.Fatal("accepted ui.dim_night=300")
	}
	// Older settings with only ui.brightness keep working.
	d.nvs.SetMany(map[string]any{"ui.brightness": 150.0})
	delete(d.nvs.tree["ui"].(map[string]any), "brightness_day")
	d.clockSet = false
	if b := d.backlightLocked(now); b != (BacklightView{150, "day"}) {
		t.Fatalf("legacy ui.brightness: %+v", b)
	}
}
