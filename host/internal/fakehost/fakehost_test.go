package fakehost

import (
	"context"
	"testing"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/source"
)

func TestSources(t *testing.T) {
	at := time.Date(2026, 10, 26, 7, 16, 0, 0, time.UTC)
	reg := source.NewRegistryWith(Sources(func() time.Time { return at })...)
	want := []string{"aqi", "geo", "http", "sys", "time", "weather"}
	if got := reg.Names(); len(got) != len(want) {
		t.Fatalf("names %v, want %v", got, want)
	}
	ctx := context.Background()
	r, err := reg.Fetch(ctx, "time", nil)
	if err != nil || r.Time.Epoch != at.Unix() {
		t.Fatalf("time: %+v %v", r.Time, err)
	}
	r, err = reg.Fetch(ctx, "weather", []byte(`{"lat":1,"lon":2}`))
	if err != nil || r.Weather.Now.Temp != 11.5 || r.Weather.Days[0].D != "2026-10-26" {
		t.Fatalf("weather: %+v %v", r.Weather, err)
	}
	if s, _ := reg.Get("weather"); s.NeedsInternet() {
		t.Error("fake sources must work offline")
	}
}
