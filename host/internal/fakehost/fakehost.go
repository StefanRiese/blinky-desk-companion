// Package fakehost provides the data sources of "buddy-sim host": a stand-in for buddy-link
// that answers like the real one but needs no internet. Its data is fixed (Düsseldorf, a
// rainy autumn day), so firmware tests and screenshots on a real board are repeatable.
package fakehost

import (
	"context"
	"encoding/json"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/protocol"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/source"
)

// fixed is a source that always returns the same kind of result.
type fixed struct {
	name  string
	every time.Duration
	fetch func() source.Result
}

func (f fixed) Name() string                   { return f.name }
func (f fixed) NeedsInternet() bool            { return false }
func (f fixed) MinInterval() time.Duration     { return time.Second }
func (f fixed) DefaultInterval() time.Duration { return f.every }
func (f fixed) Validate(json.RawMessage) error { return nil } // any params: the data doesn't depend on them
func (f fixed) Fetch(context.Context, json.RawMessage) (source.Result, error) {
	return f.fetch(), nil
}

// Sources returns the fake sources: time from now(), the rest fixed. They have the names
// of the real ones (time, weather, aqi, geo, sys, http), so devices subscribe as usual.
func Sources(now func() time.Time) []source.Source {
	return []source.Source{
		fixed{"time", time.Hour, func() source.Result {
			return source.Result{Time: &protocol.Time{T: "time", Epoch: now().Unix()}}
		}},
		fixed{"weather", 15 * time.Minute, func() source.Result {
			day := now().Format("2006-01-02")
			return source.Result{Weather: &protocol.Weather{
				T:          "weather",
				TTL:        2700,
				Now:        protocol.WeatherNow{Temp: 11.5, Feels: 9.8, Hum: 78, Code: 61, Wind: 14, IsDay: 1},
				RainNext2h: []float64{0, 0, 0.2, 0.8, 1.1, 0.4, 0, 0},
				Days: []protocol.WeatherDay{
					{D: day, Min: 6, Max: 13, Code: 61, Pop: 80, Sunrise: "07:12", Sunset: "17:21"},
					{D: now().AddDate(0, 0, 1).Format("2006-01-02"), Min: 5, Max: 12, Code: 3, Pop: 30, Sunrise: "07:14", Sunset: "17:19"},
					{D: now().AddDate(0, 0, 2).Format("2006-01-02"), Min: 4, Max: 14, Code: 1, Pop: 10, Sunrise: "07:16", Sunset: "17:17"},
				},
			}}
		}},
		fixed{"aqi", 30 * time.Minute, func() source.Result {
			return source.Result{Data: map[string]any{"index": 32, "pm25": 8.4, "pm10": 14.1}}
		}},
		fixed{"geo", 6 * time.Hour, func() source.Result {
			return source.Result{Data: map[string]any{"lat": 51.2217, "lon": 6.7762, "city": "Düsseldorf", "tz": "Europe/Berlin"}}
		}},
		fixed{"sys", 10 * time.Second, func() source.Result {
			return source.Result{Data: map[string]any{"cpu": 23, "ram": 41.5, "disk": 63, "uptime_h": 52}}
		}},
		fixed{"http", 5 * time.Minute, func() source.Result {
			return source.Result{Data: map[string]any{"value": 42}}
		}},
	}
}
