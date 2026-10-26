package sim

import (
	"context"
	"embed"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"strings"
	"time"

	"github.com/StefanRiese/esp32_desk_buddy/host/internal/protocol"
)

//go:embed web/index.html
var webFS embed.FS

// State is everything the web UI needs to render the device and its internals.
type State struct {
	Dev           string              `json:"dev"`
	FW            string              `json:"fw"`
	ClockSet      bool                `json:"clock_set"`
	ClockSrc      string              `json:"clock_src"`
	ClockOffsetMS int64               `json:"clock_offset_ms"`
	TZ            string              `json:"tz"`
	Location      string              `json:"location"`
	WifiInternet  bool                `json:"wifi_internet"`
	Link          LinkView            `json:"link"`
	Subs          []SubView           `json:"subs"`
	Data          []View              `json:"data"`
	Weather       *protocol.Weather   `json:"weather"`
	WeatherStale  bool                `json:"weather_stale"`
	WeatherSrc    string              `json:"weather_src"`
	Indoor        IndoorView          `json:"indoor"`
	Mood          string              `json:"mood"`
	MoodReason    string              `json:"mood_reason"`
	Notify        *NotifyView         `json:"notify"`
	Notifications []notifyState       `json:"notifications"`
	Page          string              `json:"page"`
	Pages         []string            `json:"pages"`
	Custom        map[string]PageView `json:"custom"`
	NightFrom     int                 `json:"night_from"`
	NightTo       int                 `json:"night_to"`
	Log           []logLine           `json:"log"`
}

// LinkView describes the host link.
type LinkView struct {
	Enabled bool     `json:"enabled"`
	State   string   `json:"state"`
	Err     string   `json:"err,omitempty"`
	Target  string   `json:"target"`
	Host    string   `json:"host,omitempty"`
	Ver     string   `json:"ver,omitempty"`
	Srcs    []string `json:"srcs,omitempty"`
	Net     bool     `json:"net"`
}

// SubView describes one configured source and who fetches it.
type SubView struct {
	SID    string `json:"sid"`
	Src    string `json:"src"`
	Via    string `json:"via"`
	Reason string `json:"reason"`
	State  string `json:"state"`
	Msg    string `json:"msg,omitempty"`
	Every  int    `json:"every"`
}

// IndoorView is the local sensor block.
type IndoorView struct {
	Temp float64   `json:"temp"`
	Hum  float64   `json:"hum"`
	Lux  float64   `json:"lux"`
	Hist []float64 `json:"hist"`
}

// NotifyView is the active notification.
type NotifyView struct {
	Text   string `json:"text"`
	Icon   string `json:"icon,omitempty"`
	LeftMS int64  `json:"left_ms"`
}

// PageView is a custom page.
type PageView struct {
	Title string          `json:"title"`
	Items json.RawMessage `json:"items"`
}

// Snapshot returns the current state.
func (d *Device) Snapshot() State {
	now := time.Now()
	d.mu.Lock()
	defer d.mu.Unlock()
	st := State{
		Dev: d.nvs.String("dev"), FW: d.nvs.String("fw"),
		ClockSet: d.clockSet, ClockSrc: d.clockSrc, ClockOffsetMS: d.clockOffset.Milliseconds(),
		TZ: d.nvs.String("location.tz"), Location: d.nvs.String("location.name"),
		WifiInternet: d.nvs.Bool("wifi.internet"),
		Link: LinkView{Enabled: d.linkEnabled, State: d.linkState, Err: d.linkErr, Target: d.linkTarget,
			Host: d.host.Host, Ver: d.host.Ver, Srcs: d.host.Srcs, Net: d.hostNet},
		Subs:          []SubView{},
		Weather:       d.weather,
		WeatherStale:  d.weather != nil && now.Sub(d.weatherAt) > d.weatherTTL,
		Indoor:        IndoorView{Temp: round1(d.temp), Hum: round1(d.hum), Lux: d.lux, Hist: append([]float64(nil), d.hist...)},
		Notifications: append([]notifyState(nil), d.notifs...),
		Page:          d.page,
		Pages:         d.pagesLocked(),
		Custom:        map[string]PageView{},
		NightFrom:     int(d.nvs.Float("ui.night_from", 22)),
		NightTo:       int(d.nvs.Float("ui.night_to", 7)),
		Log:           append([]logLine(nil), d.logLines...),
	}
	if e, ok := d.store.Get("weather.now.temp"); ok {
		st.WeatherSrc = e.Src
	}
	st.Mood, st.MoodReason = d.moodLocked(now)
	if d.notify != nil {
		st.Notify = &NotifyView{Text: d.notify.Text, Icon: d.notify.Icon, LeftMS: d.notify.Until.Sub(now).Milliseconds()}
	}
	for name, p := range d.custom {
		st.Custom[name] = PageView{Title: p.Title, Items: p.Items}
	}
	cfgs, _ := d.nvs.Sources()
	for _, c := range cfgs {
		if a := d.assign[c.SID]; a != nil {
			st.Subs = append(st.Subs, SubView{SID: a.sid, Src: a.src, Via: a.via, Reason: a.reason,
				State: a.state, Msg: a.msg, Every: a.every})
		}
	}
	st.Data = d.store.Views("")
	return st
}

// Serve runs the web UI on addr until ctx ends.
func (d *Device) Serve(ctx context.Context, addr string, ready func(url string)) error {
	ln, err := net.Listen("tcp", addr)
	if err != nil {
		return err
	}
	mux := http.NewServeMux()
	mux.HandleFunc("GET /{$}", func(w http.ResponseWriter, r *http.Request) {
		b, _ := webFS.ReadFile("web/index.html")
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.Write(b)
	})
	mux.HandleFunc("GET /api/state", func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, http.StatusOK, d.Snapshot())
	})
	mux.HandleFunc("GET /api/stream", d.stream)
	mux.HandleFunc("POST /api/input", func(w http.ResponseWriter, r *http.Request) {
		var body struct {
			Action string `json:"action"`
		}
		if readJSON(r, &body) != nil {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": "expected {\"action\":…}"})
			return
		}
		if err := d.Input(body.Action); err != nil {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
			return
		}
		writeJSON(w, http.StatusOK, map[string]bool{"ok": true})
	})
	mux.HandleFunc("POST /api/toggle", func(w http.ResponseWriter, r *http.Request) {
		var body struct {
			Link         *bool `json:"link"`
			WifiInternet *bool `json:"wifi_internet"`
		}
		if readJSON(r, &body) != nil {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": "bad request"})
			return
		}
		if body.Link != nil {
			d.SetLinkEnabled(*body.Link)
		}
		if body.WifiInternet != nil {
			if err := d.SetWifiInternet(*body.WifiInternet); err != nil {
				writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
				return
			}
		}
		writeJSON(w, http.StatusOK, map[string]bool{"ok": true})
	})
	mux.HandleFunc("GET /api/config", func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, http.StatusOK, d.Config())
	})
	mux.HandleFunc("PUT /api/config", func(w http.ResponseWriter, r *http.Request) {
		var tree map[string]any
		if err := readJSON(r, &tree); err != nil {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
			return
		}
		if err := d.SetConfig(tree); err != nil {
			writeJSON(w, http.StatusUnprocessableEntity, map[string]string{"error": err.Error()})
			return
		}
		writeJSON(w, http.StatusOK, d.Config())
	})
	srv := &http.Server{Handler: localOnly(mux), ReadHeaderTimeout: 10 * time.Second}
	go func() {
		<-ctx.Done()
		sctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
		defer cancel()
		srv.Shutdown(sctx)
	}()
	if ready != nil {
		ready("http://" + ln.Addr().String())
	}
	if err := srv.Serve(ln); err != nil && !errors.Is(err, http.ErrServerClosed) {
		return err
	}
	return nil
}

// localOnly rejects cross-site browser requests to the state-changing API.
func localOnly(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			if o := r.Header.Get("Origin"); o != "" && !strings.HasSuffix(o, "://"+r.Host) {
				http.Error(w, "cross-origin request rejected", http.StatusForbidden)
				return
			}
		}
		next.ServeHTTP(w, r)
	})
}

func (d *Device) stream(w http.ResponseWriter, r *http.Request) {
	fl, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "streaming unsupported", http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	send := func() {
		b, _ := json.Marshal(d.Snapshot())
		fmt.Fprintf(w, "data: %s\n\n", b)
		fl.Flush()
	}
	changed, cancel := d.Watch()
	defer cancel()
	send()
	limit := time.NewTicker(150 * time.Millisecond)
	defer limit.Stop()
	pending := false
	for {
		select {
		case <-r.Context().Done():
			return
		case <-changed:
			pending = true
		case <-limit.C:
			if pending {
				pending = false
				send()
			}
		}
	}
}

func writeJSON(w http.ResponseWriter, code int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	json.NewEncoder(w).Encode(v)
}

func readJSON(r *http.Request, v any) error {
	return json.NewDecoder(io.LimitReader(r.Body, 256<<10)).Decode(v)
}
