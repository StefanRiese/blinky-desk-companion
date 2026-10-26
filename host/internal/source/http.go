package source

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/url"
	"regexp"
	"strconv"
	"strings"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/fetch"
)

// HTTP fetches any JSON URL and picks values with JSON Pointers (RFC 6901), so new data
// needs only a device-config change:
//
//	{"url":"https://…","headers":{"Authorization":"…"},"pick":{"eur":"/bpi/EUR/rate_float"}}
type HTTP struct {
	Client *fetch.Client
}

type httpParams struct {
	URL     string            `json:"url"`
	Headers map[string]string `json:"headers,omitempty"`
	Pick    map[string]string `json:"pick"`
}

var keyRe = regexp.MustCompile(`^[a-zA-Z0-9_]+(\.[a-zA-Z0-9_]+)*$`)

func (*HTTP) Name() string                   { return "http" }
func (*HTTP) NeedsInternet() bool            { return true }
func (*HTTP) MinInterval() time.Duration     { return 30 * time.Second }
func (*HTTP) DefaultInterval() time.Duration { return 5 * time.Minute }

func (h *HTTP) params(raw json.RawMessage) (httpParams, error) {
	var p httpParams
	if err := decodeParams(raw, &p); err != nil {
		return p, err
	}
	u, err := url.Parse(p.URL)
	if err != nil || (u.Scheme != "http" && u.Scheme != "https") || u.Host == "" {
		return p, errors.New("params: url must be an absolute http(s) URL")
	}
	if len(p.Pick) == 0 {
		return p, errors.New("params: pick must name at least one value")
	}
	if len(p.Pick) > 16 {
		return p, errors.New("params: at most 16 picks")
	}
	for k, ptr := range p.Pick {
		if !keyRe.MatchString(k) {
			return p, fmt.Errorf("params: invalid key %q", k)
		}
		if ptr != "" && !strings.HasPrefix(ptr, "/") {
			return p, fmt.Errorf("params: pointer for %q must start with /", k)
		}
	}
	return p, nil
}

func (h *HTTP) Validate(raw json.RawMessage) error { _, err := h.params(raw); return err }

func (h *HTTP) Fetch(ctx context.Context, raw json.RawMessage) (Result, error) {
	p, err := h.params(raw)
	if err != nil {
		return Result{}, err
	}
	var doc any
	if err := h.Client.GetJSON(ctx, p.URL, p.Headers, &doc); err != nil {
		return Result{}, err
	}
	data := map[string]any{}
	var missing []string
	for k, ptr := range p.Pick {
		v, err := Pointer(doc, ptr)
		if err != nil {
			missing = append(missing, k)
			continue
		}
		data[k] = scalar(v)
	}
	if len(data) == 0 {
		return Result{}, fmt.Errorf("no pick matched (%s)", strings.Join(missing, ", "))
	}
	return Result{Data: data}, nil
}

// scalar keeps strings, numbers and bools; anything else becomes a short JSON string.
func scalar(v any) any {
	switch t := v.(type) {
	case string:
		if len(t) > 120 {
			return t[:120]
		}
		return t
	case float64, bool, nil:
		return t
	default:
		b, _ := json.Marshal(t)
		if len(b) > 120 {
			b = b[:120]
		}
		return string(b)
	}
}

// Pointer resolves a JSON Pointer in a decoded JSON document.
func Pointer(doc any, ptr string) (any, error) {
	if ptr == "" {
		return doc, nil
	}
	if !strings.HasPrefix(ptr, "/") {
		return nil, errors.New("pointer must start with /")
	}
	cur := doc
	for tok := range strings.SplitSeq(ptr[1:], "/") {
		tok = strings.ReplaceAll(strings.ReplaceAll(tok, "~1", "/"), "~0", "~")
		switch c := cur.(type) {
		case map[string]any:
			v, ok := c[tok]
			if !ok {
				return nil, fmt.Errorf("no member %q", tok)
			}
			cur = v
		case []any:
			i, err := strconv.Atoi(tok)
			if err != nil || i < 0 || i >= len(c) {
				return nil, fmt.Errorf("bad index %q", tok)
			}
			cur = c[i]
		default:
			return nil, fmt.Errorf("cannot descend into %T at %q", cur, tok)
		}
	}
	return cur, nil
}
