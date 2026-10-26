package control

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"github.com/StefanRiese/esp32_desk_buddy/host/internal/events"
	"github.com/StefanRiese/esp32_desk_buddy/host/internal/hub"
)

func TestAuthAndValidation(t *testing.T) {
	srv := httptest.NewServer((&Server{Hub: hub.New(events.NewBus()), Token: "tok"}).Handler())
	defer srv.Close()
	do := func(method, path, token, body string) int {
		req, _ := http.NewRequest(method, srv.URL+path, strings.NewReader(body))
		if token != "" {
			req.Header.Set("Authorization", "Bearer "+token)
		}
		resp, err := http.DefaultClient.Do(req)
		if err != nil {
			t.Fatal(err)
		}
		resp.Body.Close()
		return resp.StatusCode
	}
	cases := []struct {
		method, path, token, body string
		want                      int
	}{
		{"GET", "/v1/status", "", "", 401},
		{"GET", "/v1/status", "bad", "", 401},
		{"GET", "/v1/status", "tok", "", 200},
		{"GET", "/v1/devices", "tok", "", 200},
		{"POST", "/v1/notify", "tok", `{"text":""}`, 400},
		{"POST", "/v1/notify", "tok", `{"text":"hi"}`, 404}, // no device connected
		{"GET", "/v1/notify", "tok", "", 405},
		{"PUT", "/v1/pages/ci", "tok", `{"title":"x"}`, 400},
		{"POST", "/v1/devices/x/config", "tok", `{"get":["*"]}`, 404},
	}
	for _, c := range cases {
		if got := do(c.method, c.path, c.token, c.body); got != c.want {
			t.Errorf("%s %s: got %d, want %d", c.method, c.path, got, c.want)
		}
	}
}
