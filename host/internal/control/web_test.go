package control

import (
	"context"
	"encoding/json"
	"errors"
	"net/http"
	"net/http/cookiejar"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/events"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/hub"
)

// fakeDevice answers config requests like the firmware: the schema in pages of two
// entries, values by group.
type fakeDevice struct {
	schema []map[string]any
	values map[string]any
	silent bool // an old firmware: no answer to schema_from

	mu    sync.Mutex
	asked []map[string]any
}

func (f *fakeDevice) ID() string     { return "desk" }
func (f *fakeDevice) Send(any) error { return nil }
func (f *fakeDevice) Info() hub.Info { return hub.Info{Dev: "desk"} }
func (f *fakeDevice) Request(ctx context.Context, msg map[string]any) (map[string]any, error) {
	f.mu.Lock()
	f.asked = append(f.asked, msg)
	silent := f.silent
	f.mu.Unlock()
	if from, ok := msg["schema_from"].(int); ok {
		if silent {
			<-ctx.Done()
			return nil, ctx.Err()
		}
		end := min(from+2, len(f.schema))
		reply := map[string]any{"t": "config", "schema": toAny(f.schema[from:end])}
		if end < len(f.schema) {
			reply["next"] = float64(end)
		}
		return reply, nil
	}
	if get, ok := msg["get"].([]string); ok {
		vals := map[string]any{}
		for k, v := range f.values {
			if k == get[0] || strings.HasPrefix(k, get[0]+".") {
				vals[k] = v
			}
		}
		return map[string]any{"t": "config", "values": vals}, nil
	}
	return map[string]any{"t": "ack"}, nil
}

func toAny(in []map[string]any) []any {
	out := make([]any, len(in))
	for i, v := range in {
		out[i] = v
	}
	return out
}

func newWebServer(t *testing.T, dev hub.Device) (*httptest.Server, *http.Client) {
	t.Helper()
	h := hub.New(events.NewBus())
	if dev != nil {
		h.Add(dev)
	}
	srv := httptest.NewServer((&Server{Hub: h, Token: "tok", Version: "1"}).Handler())
	t.Cleanup(srv.Close)
	jar, _ := cookiejar.New(nil)
	return srv, &http.Client{Jar: jar}
}

func TestLoginAndCookie(t *testing.T) {
	srv, c := newWebServer(t, nil)
	resp, err := c.Get(srv.URL + "/")
	if err != nil {
		t.Fatal(err)
	}
	resp.Body.Close()
	if resp.StatusCode != http.StatusUnauthorized || !strings.HasPrefix(resp.Header.Get("Content-Type"), "text/html") {
		t.Fatalf("without login: %d %s, want the HTML hint", resp.StatusCode, resp.Header.Get("Content-Type"))
	}
	if resp, _ := c.Get(srv.URL + "/login?token=bad"); resp.StatusCode != http.StatusUnauthorized {
		t.Fatalf("bad token: %d", resp.StatusCode)
	}

	resp, err = c.Get(LoginURL(strings.TrimPrefix(srv.URL, "http://"), "tok"))
	if err != nil {
		t.Fatal(err)
	}
	resp.Body.Close()
	if resp.StatusCode != http.StatusOK || resp.Request.URL.Path != "/" {
		t.Fatalf("login: %d at %s", resp.StatusCode, resp.Request.URL)
	}
	if resp, _ := c.Get(srv.URL + "/v1/devices"); resp.StatusCode != http.StatusOK {
		t.Fatalf("API with the cookie: %d", resp.StatusCode)
	}

	// A POST with the cookie must come from the page itself.
	post := func(origin string) int {
		req, _ := http.NewRequest("POST", srv.URL+"/v1/notify", strings.NewReader(`{"text":"hi"}`))
		if origin != "" {
			req.Header.Set("Origin", origin)
		}
		resp, err := c.Do(req)
		if err != nil {
			t.Fatal(err)
		}
		resp.Body.Close()
		return resp.StatusCode
	}
	if got := post("http://evil.example"); got != http.StatusForbidden {
		t.Errorf("other origin: %d, want 403", got)
	}
	if got := post(""); got != http.StatusForbidden {
		t.Errorf("no origin: %d, want 403", got)
	}
	if got := post(srv.URL); got != http.StatusNotFound { // allowed; no device connected
		t.Errorf("same origin: %d, want 404", got)
	}
}

func TestLoginURL(t *testing.T) {
	cases := map[string]string{
		"127.0.0.1:7532": "http://127.0.0.1:7532/login?token=a%2Bb",
		":7532":          "http://127.0.0.1:7532/login?token=a%2Bb",
		"0.0.0.0:9000":   "http://127.0.0.1:9000/login?token=a%2Bb",
		"[::1]:7532":     "http://[::1]:7532/login?token=a%2Bb",
	}
	for listen, want := range cases {
		if got := LoginURL(listen, "a+b"); got != want {
			t.Errorf("%s: %s, want %s", listen, got, want)
		}
	}
}

func TestDeviceSettings(t *testing.T) {
	dev := &fakeDevice{
		schema: []map[string]any{
			{"key": "dev", "type": "string", "def": "buddy"},
			{"key": "wifi.ssid", "type": "string", "def": ""},
			{"key": "wifi.pass", "type": "string", "def": "", "secret": true},
			{"key": "ui.pages", "type": "json", "def": "[]"},
			{"key": "ui.dim_after", "type": "int", "def": "60", "min": 0, "max": 3600},
		},
		values: map[string]any{"dev": "desk", "wifi.ssid": "Home", "wifi.pass": "***",
			"ui.pages": []any{"eyes"}, "ui.dim_after": 60.0},
	}
	srv, _ := newWebServer(t, dev)
	get := func() (int, map[string]any) {
		req, _ := http.NewRequest("GET", srv.URL+"/v1/devices/desk/settings", nil)
		req.Header.Set("Authorization", "Bearer tok")
		resp, err := http.DefaultClient.Do(req)
		if err != nil {
			t.Fatal(err)
		}
		defer resp.Body.Close()
		var out map[string]any
		json.NewDecoder(resp.Body).Decode(&out)
		return resp.StatusCode, out
	}
	code, out := get()
	if code != http.StatusOK {
		t.Fatalf("settings: %d %v", code, out)
	}
	if n := len(out["schema"].([]any)); n != 5 {
		t.Errorf("schema: %d entries, want 5", n)
	}
	vals := out["values"].(map[string]any)
	if len(vals) != 5 || vals["wifi.pass"] != "***" || vals["wifi.ssid"] != "Home" {
		t.Errorf("values: %v", vals)
	}
	var gets []string
	dev.mu.Lock()
	for _, m := range dev.asked {
		if g, ok := m["get"].([]string); ok {
			gets = append(gets, g...)
		}
	}
	if strings.Join(gets, ",") != "dev,wifi,ui" {
		t.Errorf("values asked by group: %v", gets)
	}

	dev.silent = true
	dev.mu.Unlock()
	if code, out := get(); code != http.StatusNotImplemented {
		t.Errorf("old firmware: %d %v, want 501", code, out)
	}
}

type fakeHost struct{ set map[string]any }

func (f *fakeHost) Settings() map[string]any { return map[string]any{"values": f.set} }
func (f *fakeHost) Set(set map[string]any) error {
	if _, ok := set["bad"]; ok {
		return errors.New("bad cannot be changed here")
	}
	f.set = set
	return nil
}

func TestHostSettings(t *testing.T) {
	h := &fakeHost{}
	srv := httptest.NewServer((&Server{Hub: hub.New(events.NewBus()), Token: "tok", HostConfig: h}).Handler())
	defer srv.Close()
	put := func(body string) int {
		req, _ := http.NewRequest("PUT", srv.URL+"/v1/host/settings", strings.NewReader(body))
		req.Header.Set("Authorization", "Bearer tok")
		resp, err := http.DefaultClient.Do(req)
		if err != nil {
			t.Fatal(err)
		}
		resp.Body.Close()
		return resp.StatusCode
	}
	if code := put(`{"set":{"tcp.enabled":true}}`); code != http.StatusOK || h.set["tcp.enabled"] != true {
		t.Errorf("put: %d %v", code, h.set)
	}
	if code := put(`{"set":{"bad":1}}`); code != http.StatusUnprocessableEntity {
		t.Errorf("refused change: %d, want 422", code)
	}

	none := httptest.NewServer((&Server{Hub: hub.New(events.NewBus()), Token: "tok"}).Handler())
	defer none.Close()
	req, _ := http.NewRequest("GET", none.URL+"/v1/host/settings", nil)
	req.Header.Set("Authorization", "Bearer tok")
	if resp, _ := http.DefaultClient.Do(req); resp.StatusCode != http.StatusNotFound {
		t.Errorf("without HostConfig: %d, want 404", resp.StatusCode)
	}
}
