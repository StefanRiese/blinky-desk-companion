package source

import (
	"context"
	"encoding/json"
	"errors"
	"strconv"
	"strings"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/fetch"
)

// Geo finds the location by the public IP address (ipinfo.io, no API key needed). The
// device uses it as its location while location.auto is on. No params.
type Geo struct {
	Client  *fetch.Client
	BaseURL string // default https://ipinfo.io
}

func (*Geo) Name() string                     { return "geo" }
func (*Geo) NeedsInternet() bool              { return true }
func (*Geo) MinInterval() time.Duration       { return time.Hour }
func (*Geo) DefaultInterval() time.Duration   { return 6 * time.Hour }
func (*Geo) Validate(p json.RawMessage) error { return decodeParams(p, &struct{}{}) }

func (g *Geo) Fetch(ctx context.Context, raw json.RawMessage) (Result, error) {
	if err := g.Validate(raw); err != nil {
		return Result{}, err
	}
	base := g.BaseURL
	if base == "" {
		base = "https://ipinfo.io"
	}
	var r struct {
		City     string `json:"city"`
		Loc      string `json:"loc"` // "51.2217,6.7762"
		Timezone string `json:"timezone"`
	}
	if err := g.Client.GetJSON(ctx, base+"/json", nil, &r); err != nil {
		return Result{}, err
	}
	latS, lonS, ok := strings.Cut(r.Loc, ",")
	lat, err1 := strconv.ParseFloat(latS, 64)
	lon, err2 := strconv.ParseFloat(lonS, 64)
	if !ok || err1 != nil || err2 != nil || lat < -90 || lat > 90 || lon < -180 || lon > 180 {
		return Result{}, errors.New("unexpected answer from ipinfo.io")
	}
	data := map[string]any{"lat": lat, "lon": lon}
	if r.City != "" {
		data["city"] = r.City
	}
	if r.Timezone != "" {
		data["tz"] = r.Timezone
	}
	return Result{Data: data}, nil
}
