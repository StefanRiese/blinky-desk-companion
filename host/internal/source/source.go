// Package source implements the data sources a device can subscribe to.
package source

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"sort"
	"sync"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/fetch"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/protocol"
)

// Result is what one fetch produces. Data keys are relative; the session prefixes
// them with the subscription id.
type Result struct {
	Time    *protocol.Time
	Weather *protocol.Weather
	Data    map[string]any
}

// Source fetches one kind of data.
type Source interface {
	Name() string
	// NeedsInternet is false for sources that work offline or on the LAN only.
	NeedsInternet() bool
	MinInterval() time.Duration
	DefaultInterval() time.Duration
	Validate(params json.RawMessage) error
	Fetch(ctx context.Context, params json.RawMessage) (Result, error)
}

// ErrUnknown is returned for sources the registry doesn't offer.
var ErrUnknown = errors.New("unknown source")

// Registry holds the offered sources and shares identical fetches between devices.
type Registry struct {
	sources map[string]Source
	mu      sync.Mutex
	cache   map[string]*cacheEntry
}

type cacheEntry struct {
	mu  sync.Mutex
	at  time.Time
	res Result
	err error
}

// NewRegistry returns a registry with the built-in sources, minus denied ones.
func NewRegistry(c *fetch.Client, deny []string) *Registry {
	r := &Registry{sources: map[string]Source{}, cache: map[string]*cacheEntry{}}
	all := []Source{
		Time{},
		&Weather{Client: c},
		&AQI{Client: c},
		&HTTP{Client: c},
		NewSys(),
	}
	denied := map[string]bool{}
	for _, d := range deny {
		denied[d] = true
	}
	for _, s := range all {
		if !denied[s.Name()] {
			r.sources[s.Name()] = s
		}
	}
	return r
}

// NewRegistryWith builds a registry from explicit sources (tests, simulator).
func NewRegistryWith(srcs ...Source) *Registry {
	r := &Registry{sources: map[string]Source{}, cache: map[string]*cacheEntry{}}
	for _, s := range srcs {
		r.sources[s.Name()] = s
	}
	return r
}

// Names returns the offered source names, sorted.
func (r *Registry) Names() []string {
	out := make([]string, 0, len(r.sources))
	for n := range r.sources {
		out = append(out, n)
	}
	sort.Strings(out)
	return out
}

// Get returns a source by name.
func (r *Registry) Get(name string) (Source, error) {
	s, ok := r.sources[name]
	if !ok {
		return nil, fmt.Errorf("%w %q", ErrUnknown, name)
	}
	return s, nil
}

// Fetch runs a source. Identical requests (same source and params) within half the
// source's minimum interval (max 60 s) share one result, so several devices with the
// same subscription cause one upstream request.
func (r *Registry) Fetch(ctx context.Context, name string, params json.RawMessage) (Result, error) {
	s, err := r.Get(name)
	if err != nil {
		return Result{}, err
	}
	ttl := min(s.MinInterval()/2, time.Minute)
	key := name + "|" + canonical(params)
	r.mu.Lock()
	e, ok := r.cache[key]
	if !ok {
		e = &cacheEntry{}
		r.cache[key] = e
	}
	r.mu.Unlock()

	e.mu.Lock()
	defer e.mu.Unlock()
	if !e.at.IsZero() && time.Since(e.at) < ttl {
		return e.res, e.err
	}
	res, err := s.Fetch(ctx, params)
	if ctx.Err() == nil {
		e.at, e.res, e.err = time.Now(), res, err
	}
	r.prune()
	return res, err
}

func (r *Registry) prune() {
	r.mu.Lock()
	defer r.mu.Unlock()
	if len(r.cache) < 256 {
		return
	}
	for k, e := range r.cache {
		if e.mu.TryLock() {
			old := time.Since(e.at) > 10*time.Minute
			e.mu.Unlock()
			if old {
				delete(r.cache, k)
			}
		}
	}
}

func canonical(p json.RawMessage) string {
	if len(bytes.TrimSpace(p)) == 0 {
		return "{}"
	}
	var v any
	if err := json.Unmarshal(p, &v); err != nil {
		return string(p)
	}
	b, _ := json.Marshal(v) // map keys are sorted
	return string(b)
}

// decodeParams strictly decodes params into out (empty params allowed).
func decodeParams(p json.RawMessage, out any) error {
	if len(bytes.TrimSpace(p)) == 0 || string(bytes.TrimSpace(p)) == "null" {
		return nil
	}
	dec := json.NewDecoder(bytes.NewReader(p))
	dec.DisallowUnknownFields()
	if err := dec.Decode(out); err != nil {
		return fmt.Errorf("params: %w", err)
	}
	return nil
}

// round1 rounds to one decimal to keep messages small.
func round1(f float64) float64 {
	if f < 0 {
		return -float64(int64(-f*10+0.5)) / 10
	}
	return float64(int64(f*10+0.5)) / 10
}
