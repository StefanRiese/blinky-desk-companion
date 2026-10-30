package control

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/events"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/fetch"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/firmware"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/flasher"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/hub"
)

// oldDevice is a connected Blinky with firmware 0.2.0.
type oldDevice struct{ fakeDevice }

func (*oldDevice) Info() hub.Info { return hub.Info{Dev: "desk", Board: "amoled175", FW: "0.2.0"} }

func TestUpdateOffers(t *testing.T) {
	gh := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
		app := []firmware.Asset{{Name: "blinky-app-amoled175.bin"}}
		json.NewEncoder(w).Encode([]firmware.Release{
			{Tag: firmware.MainTag, Name: "Latest build from main (0.3.1-dev.2+abc1234)", Prerelease: true, Assets: app},
			{Tag: "v0.3.0", Published: time.Now(), Assets: app},
			{Tag: "v0.2.0", Published: time.Now().Add(-time.Hour), Assets: app},
		})
	}))
	defer gh.Close()
	client, _ := fetch.New(fetch.Options{})
	h := hub.New(events.NewBus())
	h.Add(&oldDevice{})
	s := &Server{Hub: h, Token: "tok", Flasher: &flasher.Flasher{Source: &firmware.Source{Client: client, URL: gh.URL}}}
	if s.UpdatesWaiting() != 0 {
		t.Fatal("nothing known before the first fetch")
	}
	s.refreshReleases(context.Background())
	if n := s.UpdatesWaiting(); n != 1 {
		t.Fatalf("updates waiting: %d", n)
	}

	srv := httptest.NewServer(s.Handler())
	defer srv.Close()
	req, _ := http.NewRequest(http.MethodGet, srv.URL+"/v1/devices", nil)
	req.Header.Set("Authorization", "Bearer tok")
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	var list []struct {
		Dev     string           `json:"dev"`
		Updates *firmware.Offers `json:"updates"`
	}
	json.NewDecoder(resp.Body).Decode(&list)
	if len(list) != 1 || list[0].Updates == nil || list[0].Updates.Release != "0.3.0" || !list[0].Updates.Waiting ||
		list[0].Updates.Main != "0.3.1-dev.2+abc1234" {
		t.Fatalf("devices: %+v", list)
	}
}
