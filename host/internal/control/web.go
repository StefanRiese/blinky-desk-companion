// The web UI: one embedded page that lists the connected devices and edits their settings
// through the control API. On the control address the browser logs in with a link that
// carries the token (LoginURL) and then holds the token in a cookie for this host only. On
// the network address (gui.listen) it logs in with gui.password and gets a session cookie.
package control

import (
	"context"
	"crypto/rand"
	"crypto/subtle"
	_ "embed"
	"encoding/hex"
	"errors"
	"html"
	"net"
	"net/http"
	"net/url"
	"strings"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/protocol"
)

//go:embed web/index.html
var indexHTML []byte

const (
	cookieName    = "buddy_link_token"
	sessionCookie = "buddy_link_session"
	cookieAge     = 30 * 24 * 3600 // a bookmark keeps working
)

// LoginURL returns the link that opens the web UI on listen (the control.listen address)
// and logs the browser in. A wildcard address is reached through the loopback address.
func LoginURL(listen, token string) string {
	host, port, err := net.SplitHostPort(listen)
	if err != nil {
		host, port = "127.0.0.1", "7532"
	}
	if ip := net.ParseIP(host); host == "" || (ip != nil && ip.IsUnspecified()) {
		host = "127.0.0.1"
	}
	return "http://" + net.JoinHostPort(host, port) + "/login?token=" + url.QueryEscape(token)
}

// login handles /login: GET with ?token=… (the login link) sets the token cookie; on the
// network address a POST with the password sets the session cookie, and GET shows the form.
func (s *Server) login(w http.ResponseWriter, r *http.Request, lan bool) {
	if r.Method == http.MethodGet && s.validToken(r.URL.Query().Get("token")) {
		setCookie(w, cookieName, s.Token)
		http.Redirect(w, r, "/", http.StatusSeeOther)
		return
	}
	if !lan {
		loginHint(w)
		return
	}
	if r.Method != http.MethodPost {
		loginForm(w, http.StatusOK, "")
		return
	}
	pw := r.PostFormValue("password")
	if s.Password == "" || subtle.ConstantTimeCompare([]byte(pw), []byte(s.Password)) != 1 {
		time.Sleep(time.Second) // slows down guessing
		loginForm(w, http.StatusUnauthorized, "Wrong password.")
		return
	}
	setCookie(w, sessionCookie, s.sessionID())
	http.Redirect(w, r, "/", http.StatusSeeOther)
}

func setCookie(w http.ResponseWriter, name, value string) {
	http.SetCookie(w, &http.Cookie{Name: name, Value: value, Path: "/", MaxAge: cookieAge,
		HttpOnly: true, SameSite: http.SameSiteStrictMode})
}

// sessionID returns the session cookie value of password logins (random, per start).
func (s *Server) sessionID() string {
	s.sessionOnce.Do(func() {
		b := make([]byte, 24)
		rand.Read(b)
		s.session = hex.EncodeToString(b)
	})
	return s.session
}

func (s *Server) validSession(got string) bool {
	return s.Password != "" && subtle.ConstantTimeCompare([]byte(got), []byte(s.sessionID())) == 1
}

// loginForm asks for gui.password (the web UI on the network address).
func loginForm(w http.ResponseWriter, code int, msg string) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.WriteHeader(code)
	if msg != "" {
		msg = `<p style="color:#b42318">` + html.EscapeString(msg) + `</p>`
	}
	w.Write([]byte(`<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>Buddy Link</title><body style="font:16px/1.5 system-ui,sans-serif;max-width:24em;margin:3em auto;padding:0 16px">
<h1>Buddy Link</h1>` + msg + `<form method="post" action="/login">
<label for="pw">Password (gui.password)</label><br>
<input id="pw" name="password" type="password" autofocus required style="width:100%;min-height:44px;font:inherit;margin:6px 0 12px">
<button style="min-height:44px;padding:0 18px;font:inherit">Log in</button></form>`))
}

// loginHint is the answer to a browser without a valid cookie.
func loginHint(w http.ResponseWriter) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.WriteHeader(http.StatusUnauthorized)
	w.Write([]byte(`<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>Buddy Link</title><body style="font:16px/1.5 system-ui,sans-serif;max-width:36em;margin:3em auto;padding:0 16px">
<h1>Buddy Link</h1><p>This page needs the login link. Open it with <b>Open Buddy Link…</b> in the
tray menu, or run <code>buddy-ctl gui</code>, or start buddy-link with <code>--gui</code>.</p>`))
}

// sameOrigin reports whether a request that the browser sent with the cookie comes from
// the web UI itself. GET requests change nothing, so only the others are checked.
func sameOrigin(r *http.Request) bool {
	if r.Method == http.MethodGet || r.Method == http.MethodHead {
		return true
	}
	o, err := url.Parse(r.Header.Get("Origin"))
	return err == nil && o.Host == r.Host
}

func (s *Server) ui(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Content-Security-Policy", "default-src 'self'; style-src 'self' 'unsafe-inline'; script-src 'self' 'unsafe-inline'")
	w.Write(indexHTML)
}

// deviceSettings handles GET /v1/devices/{id}/settings: the device's schema and all its
// values, for the settings form. Both are read in pieces, since one message line is
// limited (protocol.MaxLine).
func (s *Server) deviceSettings(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	devs := s.Hub.Devices(id)
	if len(devs) == 0 {
		writeErr(w, http.StatusNotFound, "device "+id+" not connected")
		return
	}
	dev := devs[0]
	ask := func(msg map[string]any) (map[string]any, error) {
		ctx, cancel := context.WithTimeout(r.Context(), 3*time.Second)
		defer cancel()
		reply, err := dev.Request(ctx, msg)
		if err == nil && reply["t"] == "err" {
			err = errors.New(fmtAny(reply["msg"]))
		}
		return reply, err
	}

	schema := []any{}
	groups := []string{}
	seen := map[string]bool{}
	for from := 0; ; {
		reply, err := ask(map[string]any{"t": "config", "schema_from": from})
		if err != nil {
			if len(schema) == 0 && errors.Is(err, context.DeadlineExceeded) {
				writeErr(w, http.StatusNotImplemented, "this firmware does not send its settings list; update it")
				return
			}
			writeErr(w, http.StatusGatewayTimeout, err.Error())
			return
		}
		page, _ := reply["schema"].([]any)
		for _, e := range page {
			schema = append(schema, e)
			if m, ok := e.(map[string]any); ok {
				key, _ := m["key"].(string)
				g, _, _ := strings.Cut(key, ".")
				if !seen[g] {
					seen[g] = true
					groups = append(groups, g)
				}
			}
		}
		next, _ := reply["next"].(float64)
		if next <= float64(from) || len(page) == 0 {
			break
		}
		from = int(next)
	}

	values := map[string]any{}
	for _, g := range groups {
		reply, err := ask(map[string]any{"t": "config", "get": []string{g}})
		if err != nil {
			writeErr(w, http.StatusGatewayTimeout, g+": "+err.Error())
			return
		}
		if v, ok := reply["values"].(map[string]any); ok {
			for k, val := range v {
				values[k] = val
			}
		}
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"schema": schema,
		"values": protocol.MaskValue(values),
	})
}

func fmtAny(v any) string {
	if s, ok := v.(string); ok && s != "" {
		return s
	}
	return "error from the device"
}
