// Updating buddy-link itself from the web UI: GET /v1/host/update shows the running
// version and the newer ones in the releases (compared as for the firmware,
// firmware.OffersForFile); POST /v1/host/update downloads one as a job, puts it in place
// (package selfupdate) and restarts buddy-link.
package control

import (
	"context"
	"errors"
	"fmt"
	"net/http"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/firmware"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/jobs"
)

// SelfUpdate is how buddy-link updates itself.
type SelfUpdate struct {
	File    string // the release file for this platform, e.g. buddy-link-windows-amd64.exe
	Reason  string // why it can't update itself ("" = it can)
	Install func(ctx context.Context, bin []byte, want string) error
	Restart func() // stops buddy-link and starts the new program
}

// ReleasesPage is where the releases are for people (macOS app, containers).
const ReleasesPage = "https://github.com/StefanRiese/blinky-desk-companion/releases"

// selfOffers returns the newer buddy-link versions (nil while the release list is unknown
// or buddy-link can't update itself).
func (s *Server) selfOffers() *firmware.Offers {
	if s.Self == nil || s.Self.Reason != "" {
		return nil
	}
	rels := s.cachedReleases()
	if rels == nil {
		return nil
	}
	o := firmware.OffersForFile(rels, s.Self.File, s.Version)
	return &o
}

// selfUpdate handles GET (state) and POST {"version": V} (start) on /v1/host/update.
func (s *Server) selfUpdate(w http.ResponseWriter, r *http.Request) {
	if s.Self == nil || s.Flasher == nil || s.Flasher.Source == nil {
		writeErr(w, http.StatusNotFound, "updating buddy-link is not offered here")
		return
	}
	if r.Method == http.MethodGet {
		reason := s.Self.Reason
		writeJSON(w, http.StatusOK, map[string]any{"version": s.Version, "can": reason == "", "reason": reason,
			"offers": s.selfOffers(), "releases_page": ReleasesPage})
		return
	}
	if s.Self.Reason != "" {
		writeErr(w, http.StatusConflict, s.Self.Reason)
		return
	}
	var body struct {
		Version string `json:"version"` // "0.3.0", "main" (the latest build from main); "" = newest offer
	}
	if err := readBody(r, &body); err != nil {
		writeErr(w, http.StatusBadRequest, err.Error())
		return
	}
	if body.Version == "" {
		if o := s.selfOffers(); o != nil && o.Release != "" {
			body.Version = o.Release
		} else if o != nil && o.Pre != "" {
			body.Version = o.Pre
		} else {
			writeErr(w, http.StatusConflict, "no newer release (yet)")
			return
		}
	}
	job, err := s.Jobs.Start("self-update", "buddy-link", func(rep jobs.Report) error {
		return s.runSelfUpdate(rep, body.Version)
	})
	if err != nil {
		writeErr(w, http.StatusConflict, err.Error())
		return
	}
	writeJSON(w, http.StatusAccepted, job)
}

// runSelfUpdate is the job: download, check, put in place, then restart (a moment after the
// job ended, so the web UI still reads it as done).
func (s *Server) runSelfUpdate(rep jobs.Report, version string) error {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Minute)
	defer cancel()
	rep.Step("Reading the releases")
	rels, err := s.Flasher.Source.Releases(ctx)
	if err != nil {
		return err
	}
	rel, asset, err := firmware.Pick(rels, version, s.Self.File)
	if err != nil {
		return err
	}
	want := rel.Version()
	if rel.Rolling() {
		want = rel.BuildVersion()
	}
	if want == s.Version {
		return errors.New("buddy-link runs v" + want + " already")
	}
	rep.Step(fmt.Sprintf("Downloading %s (v%s, %.1f MB)", asset.Name, want, float64(asset.Size)/1e6))
	bin, err := s.Flasher.Source.Download(ctx, rel, asset)
	if err != nil {
		return err
	}
	rep.Step("Checking the new program and putting it in place")
	if err := s.Self.Install(ctx, bin, want); err != nil {
		return err
	}
	rep.Step("Restarting as v" + want)
	time.AfterFunc(time.Second, s.Self.Restart)
	return nil
}
