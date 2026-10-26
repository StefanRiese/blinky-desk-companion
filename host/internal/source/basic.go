package source

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/url"
	"strings"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/fetch"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/protocol"
)

// Time sends the host clock (the OS keeps it NTP-synced). No params.
type Time struct{}

func (Time) Name() string                     { return "time" }
func (Time) NeedsInternet() bool              { return false }
func (Time) MinInterval() time.Duration       { return time.Minute }
func (Time) DefaultInterval() time.Duration   { return time.Hour }
func (Time) Validate(p json.RawMessage) error { return decodeParams(p, &struct{}{}) }
func (Time) Fetch(context.Context, json.RawMessage) (Result, error) {
	return Result{Time: &protocol.Time{T: "time", Epoch: time.Now().Unix()}}, nil
}

type latLon struct {
	Lat  *float64 `json:"lat"`
	Lon  *float64 `json:"lon"`
	Days int      `json:"days,omitempty"`
}

func (p latLon) validate() error {
	if p.Lat == nil || p.Lon == nil {
		return errors.New("params: lat and lon are required")
	}
	if *p.Lat < -90 || *p.Lat > 90 || *p.Lon < -180 || *p.Lon > 180 {
		return errors.New("params: lat/lon out of range")
	}
	return nil
}

// Weather fetches the forecast from Open-Meteo (no API key needed).
type Weather struct {
	Client  *fetch.Client
	BaseURL string // default https://api.open-meteo.com
}

func (*Weather) Name() string                   { return "weather" }
func (*Weather) NeedsInternet() bool            { return true }
func (*Weather) MinInterval() time.Duration     { return 5 * time.Minute }
func (*Weather) DefaultInterval() time.Duration { return 15 * time.Minute }

func (w *Weather) params(raw json.RawMessage) (latLon, error) {
	var p latLon
	if err := decodeParams(raw, &p); err != nil {
		return p, err
	}
	if err := p.validate(); err != nil {
		return p, err
	}
	if p.Days == 0 {
		p.Days = 3
	}
	if p.Days < 1 || p.Days > 7 {
		return p, errors.New("params: days must be 1..7")
	}
	return p, nil
}

func (w *Weather) Validate(raw json.RawMessage) error { _, err := w.params(raw); return err }

func (w *Weather) Fetch(ctx context.Context, raw json.RawMessage) (Result, error) {
	p, err := w.params(raw)
	if err != nil {
		return Result{}, err
	}
	base := w.BaseURL
	if base == "" {
		base = "https://api.open-meteo.com"
	}
	q := url.Values{}
	q.Set("latitude", fmt.Sprint(*p.Lat))
	q.Set("longitude", fmt.Sprint(*p.Lon))
	q.Set("current", "temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,is_day")
	q.Set("minutely_15", "precipitation")
	q.Set("forecast_minutely_15", "8")
	q.Set("daily", "weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset")
	q.Set("forecast_days", fmt.Sprint(p.Days))
	q.Set("timezone", "auto")
	var r struct {
		Current struct {
			Temp  float64 `json:"temperature_2m"`
			Feels float64 `json:"apparent_temperature"`
			Hum   float64 `json:"relative_humidity_2m"`
			Code  int     `json:"weather_code"`
			Wind  float64 `json:"wind_speed_10m"`
			IsDay int     `json:"is_day"`
		} `json:"current"`
		Minutely15 struct {
			Precipitation []*float64 `json:"precipitation"`
		} `json:"minutely_15"`
		Daily struct {
			Time    []string   `json:"time"`
			Code    []int      `json:"weather_code"`
			Max     []float64  `json:"temperature_2m_max"`
			Min     []float64  `json:"temperature_2m_min"`
			Pop     []*float64 `json:"precipitation_probability_max"`
			Sunrise []string   `json:"sunrise"`
			Sunset  []string   `json:"sunset"`
		} `json:"daily"`
	}
	if err := w.Client.GetJSON(ctx, base+"/v1/forecast?"+q.Encode(), nil, &r); err != nil {
		return Result{}, err
	}
	msg := &protocol.Weather{T: "weather", Now: protocol.WeatherNow{
		Temp: round1(r.Current.Temp), Feels: round1(r.Current.Feels), Hum: round1(r.Current.Hum),
		Code: r.Current.Code, Wind: round1(r.Current.Wind), IsDay: r.Current.IsDay,
	}}
	for _, v := range r.Minutely15.Precipitation {
		f := 0.0
		if v != nil {
			f = round1(*v)
		}
		msg.RainNext2h = append(msg.RainNext2h, f)
	}
	d := r.Daily
	for i := range d.Time {
		day := protocol.WeatherDay{D: d.Time[i]}
		if i < len(d.Code) {
			day.Code = d.Code[i]
		}
		if i < len(d.Min) {
			day.Min = round1(d.Min[i])
		}
		if i < len(d.Max) {
			day.Max = round1(d.Max[i])
		}
		if i < len(d.Pop) && d.Pop[i] != nil {
			day.Pop = int(*d.Pop[i])
		}
		if i < len(d.Sunrise) {
			day.Sunrise = clockPart(d.Sunrise[i])
		}
		if i < len(d.Sunset) {
			day.Sunset = clockPart(d.Sunset[i])
		}
		msg.Days = append(msg.Days, day)
	}
	return Result{Weather: msg}, nil
}

// clockPart turns "2026-10-25T07:12" into "07:12".
func clockPart(s string) string {
	if _, t, ok := strings.Cut(s, "T"); ok && len(t) >= 5 {
		return t[:5]
	}
	return s
}

// AQI fetches the European air quality index from Open-Meteo.
type AQI struct {
	Client  *fetch.Client
	BaseURL string // default https://air-quality-api.open-meteo.com
}

func (*AQI) Name() string                   { return "aqi" }
func (*AQI) NeedsInternet() bool            { return true }
func (*AQI) MinInterval() time.Duration     { return 15 * time.Minute }
func (*AQI) DefaultInterval() time.Duration { return 30 * time.Minute }

func (a *AQI) Validate(raw json.RawMessage) error {
	var p latLon
	if err := decodeParams(raw, &p); err != nil {
		return err
	}
	return p.validate()
}

func (a *AQI) Fetch(ctx context.Context, raw json.RawMessage) (Result, error) {
	var p latLon
	if err := decodeParams(raw, &p); err != nil {
		return Result{}, err
	}
	if err := p.validate(); err != nil {
		return Result{}, err
	}
	base := a.BaseURL
	if base == "" {
		base = "https://air-quality-api.open-meteo.com"
	}
	q := url.Values{}
	q.Set("latitude", fmt.Sprint(*p.Lat))
	q.Set("longitude", fmt.Sprint(*p.Lon))
	q.Set("current", "european_aqi,pm2_5,pm10")
	var r struct {
		Current struct {
			Index *float64 `json:"european_aqi"`
			PM25  *float64 `json:"pm2_5"`
			PM10  *float64 `json:"pm10"`
		} `json:"current"`
	}
	if err := a.Client.GetJSON(ctx, base+"/v1/air-quality?"+q.Encode(), nil, &r); err != nil {
		return Result{}, err
	}
	data := map[string]any{}
	if r.Current.Index != nil {
		data["index"] = int(*r.Current.Index)
	}
	if r.Current.PM25 != nil {
		data["pm25"] = round1(*r.Current.PM25)
	}
	if r.Current.PM10 != nil {
		data["pm10"] = round1(*r.Current.PM10)
	}
	return Result{Data: data}, nil
}
