// Tests for updating buddy-link itself through the control API.
package control

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/events"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/fetch"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/firmware"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/flasher"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/hub"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/jobs"
)

func TestSelfUpdate(t *testing.T) {
	const file = "buddy-link-linux-amd64"
	bin := []byte("new buddy-link")
	sum := sha256.Sum256(bin)
	var gh *httptest.Server
	gh = httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/bin" {
			w.Write(bin)
			return
		}
		a := []firmware.Asset{{Name: file, URL: gh.URL + "/bin", Size: len(bin), Digest: "sha256:" + hex.EncodeToString(sum[:])}}
		json.NewEncoder(w).Encode([]firmware.Release{
			{Tag: "v0.3.0", Published: time.Now(), Assets: a},
			{Tag: "v0.2.0", Published: time.Now().Add(-time.Hour), Assets: a},
		})
	}))
	defer gh.Close()
	client, _ := fetch.New(fetch.Options{})
	var installed []byte
	var want string
	var restarted atomic.Bool
	js := &jobs.Jobs{}
	s := &Server{Hub: hub.New(events.NewBus()), Token: "tok", Version: "0.2.0", Jobs: js,
		Flasher: &flasher.Flasher{Source: &firmware.Source{Client: client, URL: gh.URL}},
		Self: &SelfUpdate{File: file, Install: func(_ context.Context, b []byte, v string) error {
			installed, want = b, v
			return nil
		}, Restart: func() { restarted.Store(true) }}}
	s.refreshReleases(context.Background())
	srv := httptest.NewServer(s.Handler())
	defer srv.Close()
	call := func(method, path, body string) (int, map[string]any) {
		req, _ := http.NewRequest(method, srv.URL+path, strings.NewReader(body))
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

	_, st := call("GET", "/v1/host/update", "")
	if st["can"] != true || st["offers"].(map[string]any)["release"] != "0.3.0" {
		t.Fatalf("state: %v", st)
	}
	if _, st := call("GET", "/v1/status", ""); st["update_waiting"] != true {
		t.Errorf("status: %v", st)
	}
	code, job := call("POST", "/v1/host/update", `{"version":"0.3.0"}`)
	if code != http.StatusAccepted {
		t.Fatalf("start: %d %v", code, job)
	}
	for i := 0; i < 100; i++ {
		if j, _ := js.Get(job["id"].(string)); j.State != "running" {
			if j.State != "done" {
				t.Fatalf("job: %+v", j)
			}
			break
		}
		time.Sleep(20 * time.Millisecond)
	}
	if string(installed) != string(bin) || want != "0.3.0" {
		t.Errorf("installed %q as %q", installed, want)
	}
	time.Sleep(1200 * time.Millisecond)
	if !restarted.Load() {
		t.Error("no restart after the update")
	}

	// Can't update itself: no offers, and a start is refused.
	s.Self.Reason = "Buddy Link runs in a container: update its image instead."
	if _, st := call("GET", "/v1/host/update", ""); st["can"] != false || st["offers"] != nil {
		t.Errorf("state: %v", st)
	}
	if code, _ := call("POST", "/v1/host/update", `{"version":"0.3.0"}`); code != http.StatusConflict {
		t.Errorf("start: %d, want 409", code)
	}
}
