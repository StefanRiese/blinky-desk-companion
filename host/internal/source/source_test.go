package source

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/StefanRiese/esp32_desk_buddy/host/internal/fetch"
)

func client(t *testing.T) *fetch.Client {
	c, err := fetch.New(fetch.Options{Timeout: 5 * time.Second})
	if err != nil {
		t.Fatal(err)
	}
	return c
}

func TestPointer(t *testing.T) {
	var doc any
	json.Unmarshal([]byte(`{"a":{"b":[10,{"c~d":"x","e/f":true}]}}`), &doc)
	cases := map[string]any{"/a/b/0": 10.0, "/a/b/1/c~0d": "x", "/a/b/1/e~1f": true}
	for ptr, want := range cases {
		got, err := Pointer(doc, ptr)
		if err != nil || got != want {
			t.Errorf("%s: got %v, %v; want %v", ptr, got, err, want)
		}
	}
	if _, err := Pointer(doc, "/a/zz"); err == nil {
		t.Error("expected error for missing member")
	}
}

func TestHTTPSource(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Header.Get("X-Key") != "k" {
			w.WriteHeader(http.StatusUnauthorized)
			return
		}
		w.Write([]byte(`{"bpi":{"EUR":{"rate_float":61234.5}},"name":"bitcoin"}`))
	}))
	defer srv.Close()
	h := &HTTP{Client: client(t)}
	p := json.RawMessage(`{"url":"` + srv.URL + `","headers":{"X-Key":"k"},"pick":{"eur":"/bpi/EUR/rate_float","name":"/name","nope":"/missing"}}`)
	if err := h.Validate(p); err != nil {
		t.Fatal(err)
	}
	res, err := h.Fetch(context.Background(), p)
	if err != nil {
		t.Fatal(err)
	}
	if res.Data["eur"] != 61234.5 || res.Data["name"] != "bitcoin" || res.Data["nope"] != nil {
		t.Fatalf("unexpected data %v", res.Data)
	}
	bad := json.RawMessage(`{"url":"ftp://x","pick":{"a":"/a"}}`)
	if h.Validate(bad) == nil {
		t.Error("expected validation error for ftp url")
	}
	unauth := json.RawMessage(`{"url":"` + srv.URL + `","pick":{"a":"/a"}}`)
	if _, err := h.Fetch(context.Background(), unauth); err == nil || !strings.Contains(err.Error(), "401") {
		t.Fatalf("want HTTP 401, got %v", err)
	}
}

func TestWeatherSource(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Query().Get("latitude") != "51.2" || r.URL.Query().Get("forecast_days") != "2" {
			t.Errorf("unexpected query %s", r.URL.RawQuery)
		}
		w.Write([]byte(`{
		  "current":{"temperature_2m":11.46,"apparent_temperature":9.8,"relative_humidity_2m":78,"weather_code":61,"wind_speed_10m":14.04,"is_day":1},
		  "minutely_15":{"time":["a","b"],"precipitation":[0.0,null]},
		  "daily":{"time":["2026-10-25","2026-10-26"],"weather_code":[61,3],"temperature_2m_max":[13.1,14],"temperature_2m_min":[6,7],
		    "precipitation_probability_max":[80,null],"sunrise":["2026-10-25T07:12","2026-10-26T07:14"],"sunset":["2026-10-25T17:21","2026-10-26T17:19"]}}`))
	}))
	defer srv.Close()
	w := &Weather{Client: client(t), BaseURL: srv.URL}
	if w.Validate(json.RawMessage(`{"lat":51.2}`)) == nil {
		t.Error("expected error without lon")
	}
	res, err := w.Fetch(context.Background(), json.RawMessage(`{"lat":51.2,"lon":6.8,"days":2}`))
	if err != nil {
		t.Fatal(err)
	}
	m := res.Weather
	if m.Now.Temp != 11.5 || m.Now.Code != 61 || len(m.Days) != 2 || m.Days[0].Sunrise != "07:12" || m.Days[1].Pop != 0 || len(m.RainNext2h) != 2 {
		t.Fatalf("unexpected weather %+v", m)
	}
}

type countSrc struct{ n atomic.Int32 }

func (*countSrc) Name() string                   { return "count" }
func (*countSrc) NeedsInternet() bool            { return false }
func (*countSrc) MinInterval() time.Duration     { return 10 * time.Second }
func (*countSrc) DefaultInterval() time.Duration { return time.Minute }
func (*countSrc) Validate(json.RawMessage) error { return nil }
func (c *countSrc) Fetch(context.Context, json.RawMessage) (Result, error) {
	c.n.Add(1)
	return Result{Data: map[string]any{"n": c.n.Load()}}, nil
}

func TestRegistryDedupe(t *testing.T) {
	c := &countSrc{}
	r := NewRegistryWith(c)
	ctx := context.Background()
	r.Fetch(ctx, "count", json.RawMessage(`{"a":1,"b":2}`))
	r.Fetch(ctx, "count", json.RawMessage(`{ "b":2, "a":1 }`)) // same params, other order
	if c.n.Load() != 1 {
		t.Fatalf("identical fetches not shared: %d calls", c.n.Load())
	}
	r.Fetch(ctx, "count", json.RawMessage(`{"a":2}`))
	if c.n.Load() != 2 {
		t.Fatalf("different params shared: %d calls", c.n.Load())
	}
	if _, err := r.Fetch(ctx, "nope", nil); err == nil {
		t.Fatal("expected unknown source error")
	}
}
