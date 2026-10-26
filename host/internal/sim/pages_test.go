package sim

import (
	"slices"
	"testing"
	"time"
)

func TestPagesSetting(t *testing.T) {
	d := newTestDevice(t)
	for _, bad := range []any{[]any{}, []any{"eyes", "eyes"}, []any{"weather"}, "eyes"} {
		if err := d.nvs.SetMany(map[string]any{"ui.pages": bad}); err == nil {
			t.Errorf("accepted ui.pages=%v", bad)
		}
	}
	if err := d.nvs.SetMany(map[string]any{"ui.pages": []any{"dashboard", "alarm", "forecast"}}); err != nil {
		t.Fatal(err)
	}
	d.custom["ci"] = &customPage{Name: "ci"}
	if got := d.Snapshot().Pages; !slices.Equal(got, []string{"dashboard", "alarm", "forecast", "custom:ci"}) {
		t.Fatalf("pages = %v", got)
	}
	// Swipes follow the configured order; swipe up goes to the first page without eyes.
	d.page = "dashboard"
	d.InputAt("swipe_left", -1)
	if d.page != "alarm" {
		t.Fatalf("next page = %q", d.page)
	}
	d.InputAt("swipe_left", -1) // forecast (on the alarm page, swipe up sets the hour)
	d.InputAt("swipe_up", -1)
	if d.page != "dashboard" {
		t.Fatalf("swipe up without eyes = %q", d.page)
	}
}

func TestHiddenAlarmPageStillRings(t *testing.T) {
	d := newTestDevice(t)
	d.nvs.SetMany(map[string]any{"ui.pages": []any{"eyes", "dashboard"}})
	d.clockSet = true
	now := time.Now()
	local := d.localTimeLocked(now)
	d.nvs.SetMany(map[string]any{"alarm.list": []any{map[string]any{"at": local.Format("15:04"), "on": true}}})
	d.mu.Lock()
	d.alarmTickLocked(now)
	d.mu.Unlock()
	if st := d.Snapshot(); st.Page != "alarm" || !slices.Contains(st.Pages, "alarm") {
		t.Fatalf("ringing with hidden alarm page: page %q, pages %v", st.Page, st.Pages)
	}
	d.InputAt("shake", -1)
	if slices.Contains(d.Snapshot().Pages, "alarm") {
		t.Fatal("alarm page should be hidden again after stopping")
	}
}
