package sim

import (
	"fmt"
	"time"
)

// Backlight: ui.brightness_day / ui.brightness_night (night = ui.night_from..ui.night_to),
// and after ui.dim_after seconds without activity ui.dim_day / ui.dim_night. Touch,
// gestures, notifications and ringing alarms wake the screen. ui.dim_after = 0 never dims.

// BacklightView is the current backlight for the web UI.
type BacklightView struct {
	Level int    `json:"level"` // 0..255
	Mode  string `json:"mode"`  // day | night | dim_day | dim_night
}

var brightnessKeys = map[string][2]float64{ // allowed ranges
	"ui.brightness_day": {0, 255}, "ui.brightness_night": {0, 255},
	"ui.dim_day": {0, 255}, "ui.dim_night": {0, 255}, "ui.dim_after": {0, 3600},
	"ui.home_after": {0, 3600}, // not the backlight, but checked the same way (pages.go)
}

// validateBrightness checks the ranges of the backlight settings that are set.
func (n *NVS) validateBrightness() error {
	for key, r := range brightnessKeys {
		v, ok := n.Get(key)
		if !ok {
			continue
		}
		f, isNum := v.(float64)
		if i, isInt := v.(int); isInt {
			f, isNum = float64(i), true
		}
		if !isNum || f < r[0] || f > r[1] {
			return fmt.Errorf("%s must be a number from %g to %g", key, r[0], r[1])
		}
	}
	return nil
}

// isNightLocked reports whether now is within the night hours (needs the clock).
func (d *Device) isNightLocked(now time.Time) bool {
	if !d.clockSet {
		return false
	}
	h := d.localTimeLocked(now).Hour()
	from, to := int(d.nvs.Float("ui.night_from", 22)), int(d.nvs.Float("ui.night_to", 7))
	return (from > to && (h >= from || h < to)) || (from < to && h >= from && h < to)
}

func (d *Device) backlightLocked(now time.Time) BacklightView {
	night := d.isNightLocked(now)
	dimAfter := time.Duration(d.nvs.Float("ui.dim_after", 60)) * time.Second
	dim := dimAfter > 0 && now.Sub(d.lastActive) >= dimAfter
	switch {
	case night && dim:
		return BacklightView{int(d.nvs.Float("ui.dim_night", 10)), "dim_night"}
	case night:
		return BacklightView{int(d.nvs.Float("ui.brightness_night", 40)), "night"}
	case dim:
		return BacklightView{int(d.nvs.Float("ui.dim_day", 80)), "dim_day"}
	}
	// ui.brightness is the setting of older firmware.
	return BacklightView{int(d.nvs.Float("ui.brightness_day", d.nvs.Float("ui.brightness", 200))), "day"}
}
