// Flashing over the control API: USB ports, firmware releases, flash jobs and their
// progress. The web UI's "Flash a new device" and buddy-ctl flash use these.
package control

import (
	"io"
	"net/http"
	"strings"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/firmware"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/flasher"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/transport/serial"
)

// ports handles GET /v1/ports: the USB serial ports, with the Blinky on each if one is
// connected there.
func (s *Server) ports(w http.ResponseWriter, r *http.Request) {
	list, err := serial.List()
	if err != nil {
		writeErr(w, http.StatusInternalServerError, err.Error())
		return
	}
	type port struct {
		serial.PortInfo
		Dev string `json:"dev,omitempty"`
		FW  string `json:"fw,omitempty"`
	}
	out := []port{}
	for _, p := range list {
		e := port{PortInfo: p}
		for _, d := range s.Hub.Devices("") {
			if info := d.Info(); info.Transport == "serial" && info.Addr == p.Name {
				e.Dev, e.FW = info.Dev, info.FW
			}
		}
		out = append(out, e)
	}
	writeJSON(w, http.StatusOK, out)
}

// releases handles GET /v1/firmware/releases?board=…: the releases with a full image
// (first flash) and an app image (update) for that board.
func (s *Server) releases(w http.ResponseWriter, r *http.Request) {
	if s.Flasher == nil {
		writeErr(w, http.StatusNotFound, "flashing is not available")
		return
	}
	board := r.URL.Query().Get("board")
	rels, err := s.Flasher.Source.Releases(r.Context())
	if err != nil {
		writeErr(w, http.StatusBadGateway, err.Error())
		return
	}
	type release struct {
		Tag        string    `json:"tag"`
		Version    string    `json:"version"`
		Prerelease bool      `json:"prerelease"`
		Published  time.Time `json:"published"`
		Full       bool      `json:"full"`
		App        bool      `json:"app"`
	}
	out := []release{}
	for _, rel := range rels {
		_, full := rel.Asset(firmware.AssetName(firmware.Full, board))
		_, app := rel.Asset(firmware.AssetName(firmware.App, board))
		out = append(out, release{rel.Tag, rel.Version(), rel.Prerelease, rel.Published, full, app})
	}
	writeJSON(w, http.StatusOK, out)
}

// flash handles POST /v1/flash: {"port","board","version?"} as JSON, or the image itself
// (application/octet-stream) with ?port=…&board=…. It answers with the job.
func (s *Server) flash(w http.ResponseWriter, r *http.Request) {
	if s.Flasher == nil {
		writeErr(w, http.StatusNotFound, "flashing is not available")
		return
	}
	var req flasher.Request
	if strings.HasPrefix(r.Header.Get("Content-Type"), "application/json") {
		if err := readBody(r, &req); err != nil {
			writeErr(w, http.StatusBadRequest, err.Error())
			return
		}
	} else {
		q := r.URL.Query()
		req.Port, req.Board = q.Get("port"), q.Get("board")
		img, err := io.ReadAll(io.LimitReader(r.Body, firmware.MaxImage+1))
		if err != nil || len(img) == 0 || len(img) > firmware.MaxImage {
			writeErr(w, http.StatusBadRequest, "send the image (up to 16 MiB) as the request body")
			return
		}
		req.Image = img
	}
	job, err := s.Flasher.Start(req)
	if err != nil {
		writeErr(w, http.StatusConflict, err.Error())
		return
	}
	writeJSON(w, http.StatusAccepted, job)
}

// job handles GET /v1/jobs/{id}.
func (s *Server) job(w http.ResponseWriter, r *http.Request) {
	if s.Jobs == nil {
		writeErr(w, http.StatusNotFound, "no jobs")
		return
	}
	j, ok := s.Jobs.Get(r.PathValue("id"))
	if !ok {
		writeErr(w, http.StatusNotFound, "no job "+r.PathValue("id"))
		return
	}
	writeJSON(w, http.StatusOK, j)
}
