package sim

import (
	"fmt"
	"path/filepath"
	"testing"
	"time"
)

func newTestDevice(t *testing.T) *Device {
	t.Helper()
	nvs, err := LoadNVS(filepath.Join(t.TempDir(), "nvs.json"), "buddy-t")
	if err != nil {
		t.Fatal(err)
	}
	return New(nvs, nil)
}

func TestAlarmValidation(t *testing.T) {
	d := newTestDevice(t)
	for _, bad := range []any{
		[]any{map[string]any{"at": "25:00", "on": true}},
		[]any{map[string]any{"at": "07:00", "days": []any{"monday"}}},
		[]any{map[string]any{"at": "1"}, map[string]any{"at": "1"}, map[string]any{"at": "1"}, map[string]any{"at": "1"}, map[string]any{"at": "1"}},
	} {
		if err := d.nvs.SetMany(map[string]any{"alarm.list": bad}); err == nil {
			t.Errorf("accepted %v", bad)
		}
	}
	if list, _ := d.nvs.Alarms(); len(list) != 2 {
		t.Fatalf("factory alarms changed by rejected sets: %v", list)
	}
}

func TestAlarmEditOnPage(t *testing.T) {
	d := newTestDevice(t)
	d.page = "alarm"
	for _, a := range []struct {
		action string
		x      float64
	}{{"swipe_up", .25}, {"swipe_up", .75}, {"swipe_down", .75}, {"swipe_down", .75}, {"tap", -1}} {
		if err := d.InputAt(a.action, a.x); err != nil {
			t.Fatal(err)
		}
	}
	list, _ := d.nvs.Alarms()
	if got := list[0]; got.At != "08:55" || !got.On { // 07:00 → 08:00 → 08:05 → 08:00 → 08:55 (minutes wrap without carry)
		t.Fatalf("alarm 0 = %+v, want 08:55 on", got)
	}
	d.InputAt("long_press", -1)
	if d.Snapshot().Alarm.Sel != 1 {
		t.Fatal("long press should select the next alarm")
	}
}

func TestAlarmRingSnoozeStop(t *testing.T) {
	d := newTestDevice(t)
	d.clockSet = true
	now := time.Now()
	local := d.localTimeLocked(now)
	at := fmt.Sprintf("%02d:%02d", local.Hour(), local.Minute())
	if err := d.nvs.SetMany(map[string]any{"alarm.list": []any{map[string]any{"at": at, "on": true}}}); err != nil {
		t.Fatal(err)
	}
	d.page = "eyes"
	tick := func(at time.Time) []string {
		d.mu.Lock()
		defer d.mu.Unlock()
		var states []string
		for _, e := range d.alarmTickLocked(at) {
			states = append(states, e.Kind+":"+e.State)
		}
		return states
	}
	if got := tick(now); len(got) == 0 || got[0] != "alarm:ringing" {
		t.Fatalf("alarm did not ring: %v", got)
	}
	if st := d.Snapshot(); st.Page != "alarm" || st.Mood != "surprised" || !st.Alarm.Ringing {
		t.Fatalf("ringing: page %q mood %q %+v", st.Page, st.Mood, st.Alarm)
	}
	d.InputAt("tap", -1) // snooze
	if st := d.Snapshot().Alarm; st.Ringing || st.Snoozed == "" {
		t.Fatalf("after tap: %+v", st)
	}
	if got := tick(now.Add(time.Second)); len(got) != 0 {
		t.Fatalf("rang again in the same minute: %v", got)
	}
	if got := tick(now.Add(defaultSnooze*time.Minute + time.Second)); len(got) == 0 || got[0] != "alarm:ringing" {
		t.Fatalf("did not ring after snooze: %v", got)
	}
	d.InputAt("shake", -1) // a shake doesn't stop it
	if !d.Snapshot().Alarm.Ringing {
		t.Fatal("a shake stopped the alarm")
	}
	d.InputAt("long_press", -1) // stop
	if st := d.Snapshot(); st.Alarm.Ringing || st.Alarm.Snoozed != "" || st.Mood == "surprised" && st.MoodReason == "alarm" {
		t.Fatalf("after holding: %+v", st.Alarm)
	}
	// Rings at most alarmRingFor.
	tick(now.Add(24 * time.Hour))
	if got := tick(now.Add(24*time.Hour + alarmRingFor)); len(got) == 0 || got[0] != "alarm:stopped" {
		t.Fatalf("did not stop after %v: %v", alarmRingFor, got)
	}
}

func TestNextAlarm(t *testing.T) {
	loc := time.FixedZone("x", 0)
	sat := time.Date(2026, 10, 24, 8, 0, 0, 0, loc) // a Saturday
	list := []Alarm{{At: "07:00", Days: []string{"mon", "tue", "wed", "thu", "fri"}, On: true}, {At: "09:00", Days: []string{"sat", "sun"}, On: true}}
	if got := nextAlarm(list, sat); got != "Sat 09:00" {
		t.Fatalf("got %q", got)
	}
	list[1].On = false
	if got := nextAlarm(list, sat); got != "Mon 07:00" {
		t.Fatalf("got %q", got)
	}
}
