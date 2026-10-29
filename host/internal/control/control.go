// Package control is the local HTTP API of buddy-link, used by buddy-ctl, the tray and scripts.
// It also serves the web UI (web.go). All requests need "Authorization: Bearer <token>",
// or the cookie the browser gets from the login link.
package control

import (
	"context"
	"crypto/subtle"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log/slog"
	"maps"
	"net"
	"net/http"
	"strings"
	"sync"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/flasher"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/hub"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/jobs"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/protocol"
)

// Server is the control API.
type Server struct {
	Hub        *hub.Hub
	Token      string
	Version    string
	Status     func() map[string]any // extra status fields (transports, sources)
	HostConfig HostConfig            // buddy-link's own settings; nil = not editable
	Password   string                // gui.password: login on the network address
	Flasher    *flasher.Flasher      // first flash over USB; nil = not offered
	Updater    *flasher.Updater      // updates over the link; nil = not offered
	Jobs       *jobs.Jobs            // flash and update jobs

	sessionOnce sync.Once
	session     string // the cookie of a password login, new at every start
}

// HostConfig reads and changes buddy-link's own settings (TCP, USB, proxy, log level).
type HostConfig interface {
	Settings() map[string]any
	Set(set map[string]any) error
}

// ListenAndServe runs the API and the web UI on addr (control.listen) until ctx ends.
func (s *Server) ListenAndServe(ctx context.Context, addr string) error {
	return serve(ctx, addr, s.Handler(), "control API listening")
}

// ListenAndServeLAN runs the web UI for the network on addr (gui.listen): there the
// browser logs in with Password instead of the token link.
func (s *Server) ListenAndServeLAN(ctx context.Context, addr string) error {
	if s.Password == "" {
		return errors.New("gui.listen needs gui.password")
	}
	return serve(ctx, addr, s.handler(true), "web UI for the network listening")
}

func serve(ctx context.Context, addr string, h http.Handler, msg string) error {
	ln, err := net.Listen("tcp", addr)
	if err != nil {
		return err
	}
	slog.Info(msg, "url", "http://"+ln.Addr().String())
	srv := &http.Server{Handler: h, ReadHeaderTimeout: 10 * time.Second}
	go func() {
		<-ctx.Done()
		sctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
		defer cancel()
		srv.Shutdown(sctx)
	}()
	if err := srv.Serve(ln); err != nil && !errors.Is(err, http.ErrServerClosed) {
		return err
	}
	return nil
}

// Handler returns the routed, authenticated handler for the control address.
func (s *Server) Handler() http.Handler { return s.handler(false) }

func (s *Server) handler(lan bool) http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /{$}", s.ui)
	mux.HandleFunc("GET /v1/status", s.status)
	mux.HandleFunc("GET /v1/devices", s.devices)
	mux.HandleFunc("POST /v1/devices/{id}/config", s.deviceConfig)
	mux.HandleFunc("GET /v1/devices/{id}/settings", s.deviceSettings)
	mux.HandleFunc("POST /v1/notify", s.notify)
	mux.HandleFunc("POST /v1/mood", s.mood)
	mux.HandleFunc("POST /v1/data", s.data)
	mux.HandleFunc("PUT /v1/pages/{name}", s.page)
	mux.HandleFunc("DELETE /v1/pages/{name}", s.page)
	mux.HandleFunc("GET /v1/events", s.events)
	mux.HandleFunc("GET /v1/ports", s.ports)
	mux.HandleFunc("GET /v1/firmware/releases", s.releases)
	mux.HandleFunc("POST /v1/flash", s.flash)
	mux.HandleFunc("POST /v1/devices/{id}/update", s.update)
	mux.HandleFunc("GET /v1/jobs/{id}", s.job)
	mux.HandleFunc("GET /v1/host/settings", s.hostSettings)
	mux.HandleFunc("PUT /v1/host/settings", s.hostSettings)
	return s.auth(mux, lan)
}

// auth lets requests through with the token (Authorization header or the login link's
// cookie) or, on the network address (lan), with the session cookie of a password login.
func (s *Server) auth(next http.Handler, lan bool) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/login" {
			s.login(w, r, lan)
			return
		}
		got, cookie := strings.TrimPrefix(r.Header.Get("Authorization"), "Bearer "), false
		if c, err := r.Cookie(cookieName); got == "" && err == nil {
			got, cookie = c.Value, true
		}
		ok := s.validToken(got)
		if c, err := r.Cookie(sessionCookie); !ok && err == nil && s.validSession(c.Value) {
			ok, cookie = true, true
		}
		if !ok {
			if r.URL.Path == "/" {
				if lan {
					loginForm(w, http.StatusUnauthorized, "")
				} else {
					loginHint(w)
				}
				return
			}
			writeErr(w, http.StatusUnauthorized, "missing or wrong token")
			return
		}
		if cookie && !sameOrigin(r) {
			writeErr(w, http.StatusForbidden, "request from another site")
			return
		}
		next.ServeHTTP(w, r)
	})
}

func (s *Server) validToken(got string) bool {
	return s.Token != "" && subtle.ConstantTimeCompare([]byte(got), []byte(s.Token)) == 1
}

func writeJSON(w http.ResponseWriter, code int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	enc := json.NewEncoder(w)
	enc.SetIndent("", "  ")
	enc.Encode(v)
}

func writeErr(w http.ResponseWriter, code int, msg string) {
	writeJSON(w, code, map[string]string{"error": msg})
}

func readBody(r *http.Request, v any) error {
	dec := json.NewDecoder(io.LimitReader(r.Body, 64<<10))
	if err := dec.Decode(v); err != nil {
		return fmt.Errorf("invalid JSON body: %w", err)
	}
	return nil
}

// sendResult reports how many devices got a message; 404 when none.
func sendResult(w http.ResponseWriter, n int, dev string) {
	if n == 0 {
		msg := "no device connected"
		if dev != "" {
			msg = "device " + dev + " not connected"
		}
		writeErr(w, http.StatusNotFound, msg)
		return
	}
	writeJSON(w, http.StatusOK, map[string]int{"sent": n})
}

func (s *Server) status(w http.ResponseWriter, r *http.Request) {
	st := map[string]any{"version": s.Version, "net": s.Hub.Net(), "devices": len(s.Hub.Devices(""))}
	if s.Status != nil {
		maps.Copy(st, s.Status())
	}
	writeJSON(w, http.StatusOK, st)
}

// devices handles GET /v1/devices: the connected devices; with ?all=1 also the ones that
// disconnected recently (online false, gone_at).
func (s *Server) devices(w http.ResponseWriter, r *http.Request) {
	out := []hub.Info{}
	for _, d := range s.Hub.Devices("") {
		out = append(out, d.Info())
	}
	if r.URL.Query().Get("all") == "1" {
		out = append(out, s.Hub.Offline()...)
	}
	writeJSON(w, http.StatusOK, out)
}

// deviceConfig handles POST /v1/devices/{id}/config: forward a config message, return the reply.
func (s *Server) deviceConfig(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	var body struct {
		Set map[string]any `json:"set"`
		Get []string       `json:"get"`
	}
	if err := readBody(r, &body); err != nil {
		writeErr(w, http.StatusBadRequest, err.Error())
		return
	}
	if len(body.Set) == 0 && len(body.Get) == 0 {
		writeErr(w, http.StatusBadRequest, "set or get is required")
		return
	}
	devs := s.Hub.Devices(id)
	if len(devs) == 0 {
		writeErr(w, http.StatusNotFound, "device "+id+" not connected")
		return
	}
	msg := map[string]any{"t": "config"}
	if len(body.Set) > 0 {
		msg["set"] = body.Set
	}
	if len(body.Get) > 0 {
		msg["get"] = body.Get
	}
	ctx, cancel := context.WithTimeout(r.Context(), 5*time.Second)
	defer cancel()
	reply, err := devs[0].Request(ctx, msg)
	if err != nil {
		writeErr(w, http.StatusGatewayTimeout, err.Error())
		return
	}
	code := http.StatusOK
	if reply["t"] == "err" {
		code = http.StatusUnprocessableEntity
	}
	writeJSON(w, code, protocol.MaskValue(reply))
}

func (s *Server) notify(w http.ResponseWriter, r *http.Request) {
	var body struct {
		protocol.Notify
		Dev string `json:"dev"`
	}
	if err := readBody(r, &body); err != nil {
		writeErr(w, http.StatusBadRequest, err.Error())
		return
	}
	if strings.TrimSpace(body.Text) == "" {
		writeErr(w, http.StatusBadRequest, "text is required")
		return
	}
	body.Notify.T = "notify"
	sendResult(w, s.Hub.Send(body.Dev, body.Notify), body.Dev)
}

func (s *Server) mood(w http.ResponseWriter, r *http.Request) {
	var body struct {
		protocol.Mood
		Dev string `json:"dev"`
	}
	if err := readBody(r, &body); err != nil {
		writeErr(w, http.StatusBadRequest, err.Error())
		return
	}
	if body.Mood.Mood == "" {
		writeErr(w, http.StatusBadRequest, "mood is required")
		return
	}
	body.Mood.T = "mood"
	sendResult(w, s.Hub.Send(body.Dev, body.Mood), body.Dev)
}

func (s *Server) data(w http.ResponseWriter, r *http.Request) {
	var body struct {
		protocol.Data
		Dev string `json:"dev"`
	}
	if err := readBody(r, &body); err != nil {
		writeErr(w, http.StatusBadRequest, err.Error())
		return
	}
	if len(body.KV) == 0 && len(body.Del) == 0 {
		writeErr(w, http.StatusBadRequest, "kv or del is required")
		return
	}
	body.Data.T = "data"
	if _, err := protocol.Encode(body.Data); err != nil {
		writeErr(w, http.StatusBadRequest, err.Error())
		return
	}
	sendResult(w, s.Hub.Send(body.Dev, body.Data), body.Dev)
}

// page handles PUT/DELETE /v1/pages/{name}. The PUT body is the page without t/name.
func (s *Server) page(w http.ResponseWriter, r *http.Request) {
	name := r.PathValue("name")
	dev := r.URL.Query().Get("dev")
	msg := map[string]any{}
	if r.Method == http.MethodPut {
		if err := readBody(r, &msg); err != nil {
			writeErr(w, http.StatusBadRequest, err.Error())
			return
		}
		if d, ok := msg["dev"].(string); ok {
			dev = d
		}
		delete(msg, "dev")
		if _, ok := msg["items"].([]any); !ok {
			writeErr(w, http.StatusBadRequest, "items (array) is required")
			return
		}
	} else {
		msg["del"] = true
	}
	msg["t"] = "page"
	msg["name"] = name
	if _, err := protocol.Encode(msg); err != nil {
		writeErr(w, http.StatusBadRequest, err.Error())
		return
	}
	sendResult(w, s.Hub.Send(dev, msg), dev)
}

// hostSettings handles GET and PUT /v1/host/settings. The PUT body is {"set":{key: value}}.
func (s *Server) hostSettings(w http.ResponseWriter, r *http.Request) {
	if s.HostConfig == nil {
		writeErr(w, http.StatusNotFound, "host settings are not editable here")
		return
	}
	if r.Method == http.MethodPut {
		var body struct {
			Set map[string]any `json:"set"`
		}
		if err := readBody(r, &body); err != nil {
			writeErr(w, http.StatusBadRequest, err.Error())
			return
		}
		if err := s.HostConfig.Set(body.Set); err != nil {
			writeErr(w, http.StatusUnprocessableEntity, err.Error())
			return
		}
	}
	writeJSON(w, http.StatusOK, s.HostConfig.Settings())
}

// events streams the event bus as Server-Sent Events.
func (s *Server) events(w http.ResponseWriter, r *http.Request) {
	fl, ok := w.(http.Flusher)
	if !ok {
		writeErr(w, http.StatusInternalServerError, "streaming not supported")
		return
	}
	ch, cancel := s.Hub.Bus.Subscribe()
	defer cancel()
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.WriteHeader(http.StatusOK)
	fmt.Fprint(w, ": connected\n\n")
	fl.Flush()
	keep := time.NewTicker(15 * time.Second)
	defer keep.Stop()
	for {
		select {
		case <-r.Context().Done():
			return
		case <-keep.C:
			fmt.Fprint(w, ": keepalive\n\n")
			fl.Flush()
		case ev := <-ch:
			b, _ := json.Marshal(ev)
			fmt.Fprintf(w, "event: %s\ndata: %s\n\n", ev.Type, b)
			fl.Flush()
		}
	}
}
