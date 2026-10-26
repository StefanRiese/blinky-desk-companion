package sim

import (
	"fmt"
	"slices"
	"strings"
)

// Units are the display units (units.* in NVS). Values on the wire, in the DataStore and in
// thresholds like mood.hot stay metric (°C, km/h, mm); the device converts only for display.
type Units struct {
	Temp string `json:"temp"` // C | F
	Wind string `json:"wind"` // kmh | mph | ms
	Rain string `json:"rain"` // mm | in
}

var unitChoices = map[string][]string{"temp": {"C", "F"}, "wind": {"kmh", "mph", "ms"}, "rain": {"mm", "in"}}

// Units returns the display units; missing ones are metric. It fails on unknown values.
func (n *NVS) Units() (Units, error) {
	u := Units{Temp: "C", Wind: "kmh", Rain: "mm"}
	for key, choices := range unitChoices {
		v, ok := n.Get("units." + key)
		if !ok {
			continue
		}
		s, _ := v.(string)
		if !slices.Contains(choices, s) {
			return u, fmt.Errorf("units.%s must be one of %s", key, strings.Join(choices, ", "))
		}
		switch key {
		case "temp":
			u.Temp = s
		case "wind":
			u.Wind = s
		case "rain":
			u.Rain = s
		}
	}
	return u, nil
}

// FmtTemp formats a °C value in the display unit, e.g. "78.8 °F".
func (u Units) FmtTemp(c float64) string {
	if u.Temp == "F" {
		return fmt.Sprintf("%.1f °F", c*9/5+32)
	}
	return fmt.Sprintf("%.1f °C", c)
}
