package sim

import (
	"encoding/json"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/protocol"
)

// schema is the settings registry of the firmware (firmware/components/app_core/src/settings.cpp),
// sent to editors such as the buddy-link web UI. Keep the keys, types, defaults and ranges in sync.
var schema = func() []protocol.Setting {
	rng := func(lo, hi float64) (*float64, *float64) { return &lo, &hi }
	s := func(key, typ, def string) protocol.Setting { return protocol.Setting{Key: key, Type: typ, Def: def} }
	num := func(key, typ, def string, lo, hi float64) protocol.Setting {
		e := s(key, typ, def)
		e.Min, e.Max = rng(lo, hi)
		return e
	}
	secret := func(key string) protocol.Setting { e := s(key, "string", ""); e.Secret = true; return e }
	enum := func(key, def string, choices ...string) protocol.Setting {
		e := s(key, "enum", def)
		e.Choices = choices
		return e
	}
	color := func(key, def string, presets ...string) protocol.Setting { // "#RRGGBB"; choices are presets
		e := s(key, "color", def)
		e.Choices = presets
		return e
	}
	return []protocol.Setting{
		s("dev", "string", "buddy"),
		s("wifi.ssid", "string", ""),
		secret("wifi.pass"),
		s("wifi.enabled", "bool", "true"),
		s("location.name", "string", "Home"),
		num("location.lat", "float", "51.23", -90, 90),
		num("location.lon", "float", "6.78", -180, 180),
		s("location.tz", "string", "Europe/Berlin"),
		s("location.auto", "bool", "true"),
		s("link.tcp_host", "string", ""),
		secret("link.token"),
		s("sources", "json", `[{"sid":"time","src":"time","every":3600},{"sid":"weather","src":"weather","every":900},{"sid":"aqi","src":"aqi","every":1800}]`),
		num("ui.brightness_day", "int", "200", 0, 255),
		num("ui.brightness_night", "int", "40", 0, 255),
		num("ui.dim_day", "int", "80", 0, 255),
		num("ui.dim_night", "int", "10", 0, 255),
		num("ui.dim_after", "int", "60", 0, 3600),
		num("ui.night_from", "int", "22", 0, 23),
		num("ui.night_to", "int", "7", 0, 23),
		enum("ui.rotation", "0", "0", "90", "180", "270"),
		color("ui.color", "#3EE8FF", "#3EE8FF", "#4ADE80", "#FBBF24", "#FB7185", "#A78BFA", "#F1F5F9"),
		s("ui.fps", "bool", "false"),
		s("ui.pages", "json", `["eyes","dashboard","clock","forecast","indoor","host","countdown","alarm"]`),
		enum("units.temp", "C", "C", "F"),
		enum("units.wind", "kmh", "kmh", "mph", "ms"),
		enum("units.rain", "mm", "mm", "in"),
		s("mood.ambient", "bool", "false"),
		num("mood.hot", "float", "26", -20, 50),
		num("mood.cold", "float", "18", -20, 50),
		num("mood.dry", "float", "30", 0, 100),
		s("alarm.list", "json", `[{"at":"07:00","days":["mon","tue","wed","thu","fri"],"on":false},{"at":"09:00","days":["sat","sun"],"on":false}]`),
		num("alarm.snooze_min", "int", "9", 1, 30),
		s("sound.enabled", "bool", "true"),
		num("sound.loud", "int", "80", 50, 110),
		num("sound.too_loud", "int", "70", 40, 100),
		num("sensor.temp_offset", "float", "0", -10, 10),
		num("sensor.hum_offset", "float", "0", -20, 20),
	}
}()

// schemaPage returns the schema entries from index from on that fit in one message line,
// and the index to ask for next (0 after the last page), as the firmware's
// settings_schema_page does.
func schemaPage(from int) ([]protocol.Setting, int) {
	out := []protocol.Setting{}
	used := 2 // []
	for i := max(from, 0); i < len(schema); i++ {
		b, _ := json.Marshal(schema[i])
		n := len(b) + 1
		if used+n > protocol.MaxLine-64 && len(out) > 0 {
			return out, i
		}
		out = append(out, schema[i])
		used += n
	}
	return out, 0
}
