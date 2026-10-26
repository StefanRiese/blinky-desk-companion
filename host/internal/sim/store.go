package sim

import (
	"sort"
	"strings"
	"sync"
	"time"
)

// Store emulates the firmware DataStore: key → value with source, timestamp and TTL.
type Store struct {
	mu sync.Mutex
	m  map[string]Entry
}

// Entry is one stored value.
type Entry struct {
	V   any
	Src string // local | device | host | push
	TS  time.Time
	TTL time.Duration
}

// Stale reports whether the entry is past its TTL.
func (e Entry) Stale(now time.Time) bool { return e.TTL > 0 && now.Sub(e.TS) > e.TTL }

// priority: local sensor > fetched (by device or host) > pushed by scripts.
func priority(src string) int {
	switch src {
	case "local":
		return 3
	case "device", "host":
		return 2
	default:
		return 1
	}
}

// NewStore returns an empty store.
func NewStore() *Store { return &Store{m: map[string]Entry{}} }

// Put stores a value unless a fresh value with higher priority exists.
func (s *Store) Put(key string, v any, src string, ttl time.Duration) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	now := time.Now()
	if old, ok := s.m[key]; ok && !old.Stale(now) && priority(old.Src) > priority(src) {
		return false
	}
	s.m[key] = Entry{V: v, Src: src, TS: now, TTL: ttl}
	return true
}

// Del removes a key.
func (s *Store) Del(key string) {
	s.mu.Lock()
	delete(s.m, key)
	s.mu.Unlock()
}

// Get returns an entry.
func (s *Store) Get(key string) (Entry, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	e, ok := s.m[key]
	return e, ok
}

// Fresh returns the value if present and not stale.
func (s *Store) Fresh(key string) (any, bool) {
	e, ok := s.Get(key)
	if !ok || e.Stale(time.Now()) {
		return nil, false
	}
	return e.V, true
}

// View is an entry as shown in the web UI.
type View struct {
	K     string  `json:"k"`
	V     any     `json:"v"`
	Src   string  `json:"src"`
	Age   float64 `json:"age"`
	TTL   float64 `json:"ttl"`
	Stale bool    `json:"stale"`
}

// Views returns all entries sorted by key, optionally only those with a prefix.
func (s *Store) Views(prefix string) []View {
	s.mu.Lock()
	defer s.mu.Unlock()
	now := time.Now()
	out := []View{}
	for k, e := range s.m {
		if prefix != "" && !strings.HasPrefix(k, prefix) {
			continue
		}
		out = append(out, View{K: k, V: e.V, Src: e.Src, Age: now.Sub(e.TS).Seconds(),
			TTL: e.TTL.Seconds(), Stale: e.Stale(now)})
	}
	sort.Slice(out, func(i, j int) bool { return out[i].K < out[j].K })
	return out
}
