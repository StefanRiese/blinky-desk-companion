package control

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/events"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/flasher"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/hub"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/jobs"
)

func TestFlashEndpoints(t *testing.T) {
	js := &jobs.Jobs{}
	srv := httptest.NewServer((&Server{Hub: hub.New(events.NewBus()), Token: "tok", Jobs: js, Flasher: &flasher.Flasher{Jobs: js}}).Handler())
	defer srv.Close()
	do := func(method, path, ctype string, body []byte) (int, map[string]any) {
		req, _ := http.NewRequest(method, srv.URL+path, bytes.NewReader(body))
		req.Header.Set("Authorization", "Bearer tok")
		req.Header.Set("Content-Type", ctype)
		resp, err := http.DefaultClient.Do(req)
		if err != nil {
			t.Fatal(err)
		}
		defer resp.Body.Close()
		var m map[string]any
		json.NewDecoder(resp.Body).Decode(&m)
		return resp.StatusCode, m
	}
	if code, _ := do("POST", "/v1/flash", "application/json", []byte(`{"port":"/dev/x","board":"nope"}`)); code != http.StatusConflict {
		t.Errorf("unknown board: %d", code)
	}
	// An app image can't be a first flash: the job ends with that error before it opens the port.
	app := make([]byte, 4096)
	app[0] = 0xE9
	binary.LittleEndian.PutUint32(app[0x20:], 0xABCD5432)
	code, job := do("POST", "/v1/flash?port=/dev/none&board=ws146", "application/octet-stream", app)
	if code != http.StatusAccepted {
		t.Fatalf("upload: %d %v", code, job)
	}
	id := job["id"].(string)
	for range 100 {
		_, job = do("GET", "/v1/jobs/"+id, "", nil)
		if job["state"] != "running" {
			break
		}
		time.Sleep(10 * time.Millisecond)
	}
	if job["state"] != "error" || !strings.Contains(job["error"].(string), "app image") {
		t.Fatalf("job %v", job)
	}
	if code, _ := do("GET", "/v1/jobs/nope", "", nil); code != http.StatusNotFound {
		t.Errorf("unknown job: %d", code)
	}
}
