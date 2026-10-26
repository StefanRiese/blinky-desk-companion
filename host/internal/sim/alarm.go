package sim

import (
	"encoding/json"
	"fmt"
	"slices"
	"strings"
	"time"

	"github.com/StefanRiese/esp32_desk_buddy/host/internal/protocol"
)

// Alarm is one entry of alarm.list in NVS. No days means every day.
type Alarm struct {
	At   string   `json:"at"` // HH:MM, device local time
	Days []string `json:"days,omitempty"`
	On   bool     `json:"on"`
}

const (
	maxAlarms     = 4
	alarmRingFor  = 5 * time.Minute
	alarmMinStep  = 5
	defaultSnooze = 9
)

var weekdays = []string{"sun", "mon", "tue", "wed", "thu", "fri", "sat"} // time.Weekday order

func (a Alarm) hm() (int, int) {
	var h, m int
	fmt.Sscanf(a.At, "%d:%d", &h, &m)
	return h, m
}

func (a Alarm) onDay(d time.Weekday) bool {
	return len(a.Days) == 0 || slices.Contains(a.Days, weekdays[d])
}

// Alarms parses and validates alarm.list.
func (n *NVS) Alarms() ([]Alarm, error) {
	raw, ok := n.Get("alarm.list")
	if !ok {
		return nil, nil
	}
	b, _ := json.Marshal(raw)
	var out []Alarm
	if err := json.Unmarshal(b, &out); err != nil {
		return nil, fmt.Errorf("alarm.list: %w", err)
	}
	if len(out) > maxAlarms {
		return nil, fmt.Errorf("alarm.list: at most %d alarms", maxAlarms)
	}
	for i, a := range out {
		var h, m int
		if n, err := fmt.Sscanf(a.At, "%d:%d", &h, &m); err != nil || n != 2 || h < 0 || h > 23 || m < 0 || m > 59 {
			return nil, fmt.Errorf("alarm.list[%d]: at must be HH:MM", i)
		}
		for _, d := range a.Days {
			if !slices.Contains(weekdays, d) {
				return nil, fmt.Errorf("alarm.list[%d]: unknown day %q (use %s)", i, d, strings.Join(weekdays, ", "))
			}
		}
	}
	return out, nil
}

// alarmState is the runtime state of the alarm page and the ringing alarm.
type alarmState struct {
	sel         int       // alarm shown on the page
	ringing     time.Time // zero when quiet
	ringIdx     int
	ringAt      string    // time of the ringing alarm, kept even if the list changes
	snoozeUntil time.Time // zero when not snoozed
	lastFired   string    // "YYYY-MM-DD HH:MM #i", so an alarm fires once per minute
}

// AlarmView is the alarm state for the web UI.
type AlarmView struct {
	List    []Alarm `json:"list"`
	Sel     int     `json:"sel"`
	Ringing bool    `json:"ringing"`
	RingAt  string  `json:"ring_at,omitempty"`
	Snoozed string  `json:"snoozed,omitempty"` // HH:MM when it rings again
	Next    string  `json:"next,omitempty"`    // e.g. "Mon 07:00"
	Snooze  int     `json:"snooze_min"`
}

func (d *Device) alarmViewLocked(now time.Time) AlarmView {
	list, _ := d.nvs.Alarms()
	v := AlarmView{List: list, Sel: d.al.sel, Ringing: !d.al.ringing.IsZero(),
		Snooze: int(d.nvs.Float("alarm.snooze_min", defaultSnooze))}
	if v.List == nil {
		v.List = []Alarm{}
	}
	if v.Ringing {
		v.RingAt = d.al.ringAt
	}
	if !d.al.snoozeUntil.IsZero() {
		v.Snoozed = d.localTimeLocked(d.al.snoozeUntil).Format("15:04")
	}
	if d.clockSet {
		v.Next = nextAlarm(list, d.localTimeLocked(now))
	}
	return v
}

// nextAlarm returns the next enabled alarm after now, like "Mon 07:00".
func nextAlarm(list []Alarm, now time.Time) string {
	var best time.Time
	for _, a := range list {
		if !a.On {
			continue
		}
		h, m := a.hm()
		for day := range 8 {
			t := time.Date(now.Year(), now.Month(), now.Day()+day, h, m, 0, 0, now.Location())
			if t.After(now) && a.onDay(t.Weekday()) {
				if best.IsZero() || t.Before(best) {
					best = t
				}
				break
			}
		}
	}
	if best.IsZero() {
		return ""
	}
	return best.Format("Mon 15:04")
}

func (d *Device) saveAlarmsLocked(list []Alarm) error {
	b, _ := json.Marshal(list)
	var raw []any
	json.Unmarshal(b, &raw)
	return d.nvs.SetMany(map[string]any{"alarm.list": raw})
}

// alarmInputLocked handles a gesture on the alarm page or while an alarm rings.
// It returns the event to report (nil for none) and whether the gesture was used.
func (d *Device) alarmInputLocked(action string, x float64, now time.Time) (*protocol.Event, bool) {
	list, _ := d.nvs.Alarms()
	if !d.al.ringing.IsZero() {
		at := d.al.ringAt
		switch action {
		case "shake":
			d.al.ringing, d.al.snoozeUntil = time.Time{}, time.Time{}
			return &protocol.Event{T: "event", Kind: "alarm", Name: at, State: "stopped"}, true
		case "tap":
			d.al.ringing = time.Time{}
			d.al.snoozeUntil = now.Add(time.Duration(d.nvs.Float("alarm.snooze_min", defaultSnooze)) * time.Minute)
			return &protocol.Event{T: "event", Kind: "alarm", Name: at, State: "snoozed"}, true
		}
		return nil, true // nothing else while ringing
	}
	if d.page != "alarm" || len(list) == 0 {
		return nil, false
	}
	d.al.sel = min(d.al.sel, len(list)-1)
	a := &list[d.al.sel]
	switch action {
	case "swipe_up", "swipe_down":
		step := 1
		if action == "swipe_down" {
			step = -1
		}
		h, m := a.hm()
		if x >= 0.5 {
			m = ((m/alarmMinStep+step)*alarmMinStep + 60) % 60
		} else {
			h = (h + step + 24) % 24
		}
		a.At = fmt.Sprintf("%02d:%02d", h, m)
	case "tap":
		a.On = !a.On
	case "long_press":
		d.al.sel = (d.al.sel + 1) % len(list)
		return nil, true
	default:
		return nil, false
	}
	if err := d.saveAlarmsLocked(list); err != nil {
		d.addLogLocked("sys", "alarm: "+err.Error())
	}
	return nil, true
}

// alarmTickLocked starts, re-rings after snooze, and stops ringing alarms. It runs
// every second and returns the events to send.
func (d *Device) alarmTickLocked(now time.Time) []protocol.Event {
	if !d.clockSet {
		return nil
	}
	list, _ := d.nvs.Alarms()
	ring := func(i int, state string) []protocol.Event {
		d.al.ringing, d.al.ringIdx, d.al.ringAt, d.al.snoozeUntil = now, i, list[i].At, time.Time{}
		ev := []protocol.Event{{T: "event", Kind: "alarm", Name: list[i].At, State: state}}
		if d.page != "alarm" {
			d.page = "alarm"
			ev = append(ev, protocol.Event{T: "event", Kind: "page", Name: "alarm"})
		}
		d.al.sel = i
		return ev
	}
	switch {
	case !d.al.ringing.IsZero():
		if now.Sub(d.al.ringing) >= alarmRingFor {
			d.al.ringing = time.Time{}
			return []protocol.Event{{T: "event", Kind: "alarm", Name: d.al.ringAt, State: "stopped"}}
		}
		return nil
	case !d.al.snoozeUntil.IsZero() && !now.Before(d.al.snoozeUntil) && d.al.ringIdx < len(list):
		return ring(d.al.ringIdx, "ringing")
	}
	local := d.localTimeLocked(now)
	for i, a := range list {
		h, m := a.hm()
		key := fmt.Sprintf("%s #%d", local.Format("2006-01-02 15:04"), i)
		if a.On && local.Hour() == h && local.Minute() == m && a.onDay(local.Weekday()) && d.al.lastFired != key {
			d.al.lastFired = key
			return ring(i, "ringing")
		}
	}
	return nil
}
