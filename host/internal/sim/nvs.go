package sim

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/StefanRiese/esp32_desk_buddy/host/internal/protocol"
)

// NVS emulates the device's settings store: a JSON tree persisted to a file.
// Like the real firmware, the device owns all configuration (sources, tokens, pages).
type NVS struct {
	path string
	tree map[string]any
}

// SourceCfg is one configured data source.
type SourceCfg struct {
	SID   string         `json:"sid"`
	Src   string         `json:"src"`
	Every int            `json:"every,omitempty"`
	Via   string         `json:"via,omitempty"` // auto (default) | device | host
	P     map[string]any `json:"p,omitempty"`
}

// DefaultNVS returns the factory settings of the emulated device.
func DefaultNVS(dev string) map[string]any {
	raw := fmt.Sprintf(`{
  "dev": %q,
  "fw": "0.1.0-sim",
  "board": "sim",
  "wifi": {"ssid": "", "pass": "", "internet": false},
  "location": {"name": "Home", "lat": 51.23, "lon": 6.78, "tz": "Europe/Berlin"},
  "link": {"tcp_host": "127.0.0.1:7531", "token": "", "serial": ""},
  "sources": [
    {"sid": "time", "src": "time", "every": 3600},
    {"sid": "weather", "src": "weather", "every": 900},
    {"sid": "aqi", "src": "aqi", "every": 1800},
    {"sid": "sys", "src": "sys", "every": 10}
  ],
  "ui": {"brightness": 180, "night_from": 22, "night_to": 7},
  "mood": {"hot": 26, "cold": 18, "dry": 30}
}`, dev)
	var m map[string]any
	if err := json.Unmarshal([]byte(raw), &m); err != nil {
		panic(err)
	}
	return m
}

// LoadNVS reads path, or creates it with factory settings.
func LoadNVS(path, dev string) (*NVS, error) {
	n := &NVS{path: path}
	b, err := os.ReadFile(path)
	switch {
	case err == nil:
		if err := json.Unmarshal(b, &n.tree); err != nil {
			return nil, fmt.Errorf("%s: %w", path, err)
		}
		if _, err := n.Sources(); err != nil {
			return nil, fmt.Errorf("%s: %w", path, err)
		}
	case errors.Is(err, os.ErrNotExist):
		n.tree = DefaultNVS(dev)
		if err := n.Save(); err != nil {
			return nil, err
		}
	default:
		return nil, err
	}
	return n, nil
}

// Save writes the tree to disk.
func (n *NVS) Save() error {
	b, err := json.MarshalIndent(n.tree, "", "  ")
	if err != nil {
		return err
	}
	if dir := filepath.Dir(n.path); dir != "" {
		os.MkdirAll(dir, 0o755)
	}
	return os.WriteFile(n.path, append(b, '\n'), 0o600)
}

// Path returns the file path.
func (n *NVS) Path() string { return n.path }

// Tree returns a deep copy of all settings (secrets included).
func (n *NVS) Tree() map[string]any { return deepCopy(n.tree).(map[string]any) }

// Masked returns all settings with secrets replaced by "***".
func (n *NVS) Masked() map[string]any { return protocol.MaskValue(n.tree).(map[string]any) }

// Replace sets a new tree. "***" values keep the stored secret.
func (n *NVS) Replace(tree map[string]any) error {
	merged := mergeSecrets(tree, n.tree).(map[string]any)
	tmp := &NVS{tree: merged}
	if _, err := tmp.Sources(); err != nil {
		return err
	}
	if s, _ := merged["dev"].(string); s == "" {
		return errors.New("dev must not be empty")
	}
	n.tree = merged
	return n.Save()
}

// Get returns the value at a dotted path.
func (n *NVS) Get(path string) (any, bool) {
	var cur any = n.tree
	for p := range strings.SplitSeq(path, ".") {
		m, ok := cur.(map[string]any)
		if !ok {
			return nil, false
		}
		cur, ok = m[p]
		if !ok {
			return nil, false
		}
	}
	return cur, true
}

// String returns a string setting.
func (n *NVS) String(path string) string {
	v, _ := n.Get(path)
	s, _ := v.(string)
	return s
}

// Float returns a numeric setting or def.
func (n *NVS) Float(path string, def float64) float64 {
	v, _ := n.Get(path)
	if f, ok := v.(float64); ok {
		return f
	}
	return def
}

// Bool returns a boolean setting.
func (n *NVS) Bool(path string) bool {
	v, _ := n.Get(path)
	b, _ := v.(bool)
	return b
}

// Set sets a value at a dotted path, creating objects as needed (does not save).
func (n *NVS) Set(path string, v any) error {
	parts := strings.Split(path, ".")
	cur := n.tree
	for _, p := range parts[:len(parts)-1] {
		next, ok := cur[p].(map[string]any)
		if !ok {
			if _, exists := cur[p]; exists {
				return fmt.Errorf("%s: %s is not an object", path, p)
			}
			next = map[string]any{}
			cur[p] = next
		}
		cur = next
	}
	cur[parts[len(parts)-1]] = v
	return nil
}

// SetMany applies config "set" values atomically and saves.
func (n *NVS) SetMany(set map[string]any) error {
	backup := deepCopy(n.tree).(map[string]any)
	for k, v := range set {
		if s, ok := v.(string); ok && s == protocol.Masked {
			continue
		}
		if err := n.Set(k, v); err != nil {
			n.tree = backup
			return err
		}
	}
	if _, err := n.Sources(); err != nil {
		n.tree = backup
		return err
	}
	return n.Save()
}

// Flat returns the requested keys ("*" = all) as dotted leaves, secrets masked.
func (n *NVS) Flat(keys []string) map[string]any {
	all := map[string]any{}
	flatten("", n.Masked(), all)
	out := map[string]any{}
	for _, k := range keys {
		if k == "*" {
			return all
		}
		for fk, v := range all {
			if fk == k || strings.HasPrefix(fk, k+".") {
				out[fk] = v
			}
		}
	}
	return out
}

func flatten(prefix string, v any, out map[string]any) {
	m, ok := v.(map[string]any)
	if !ok {
		out[prefix] = v
		return
	}
	for k, val := range m {
		key := k
		if prefix != "" {
			key = prefix + "." + k
		}
		flatten(key, val, out)
	}
}

// Sources parses and validates the configured sources.
func (n *NVS) Sources() ([]SourceCfg, error) {
	raw, ok := n.tree["sources"]
	if !ok {
		return nil, nil
	}
	b, _ := json.Marshal(raw)
	var out []SourceCfg
	if err := json.Unmarshal(b, &out); err != nil {
		return nil, fmt.Errorf("sources: %w", err)
	}
	seen := map[string]bool{}
	for _, s := range out {
		if s.SID == "" || s.Src == "" {
			return nil, errors.New("sources: every entry needs sid and src")
		}
		if seen[s.SID] {
			return nil, fmt.Errorf("sources: duplicate sid %q", s.SID)
		}
		seen[s.SID] = true
		switch s.Via {
		case "", "auto", "device", "host":
		default:
			return nil, fmt.Errorf("sources: %s: via must be auto, device or host", s.SID)
		}
	}
	return out, nil
}

func deepCopy(v any) any {
	switch t := v.(type) {
	case map[string]any:
		out := make(map[string]any, len(t))
		for k, val := range t {
			out[k] = deepCopy(val)
		}
		return out
	case []any:
		out := make([]any, len(t))
		for i, val := range t {
			out[i] = deepCopy(val)
		}
		return out
	default:
		return v
	}
}

// mergeSecrets replaces "***" in next with the value at the same place in prev.
// Arrays of objects with a "sid" are matched by sid, other arrays by index.
func mergeSecrets(next, prev any) any {
	switch t := next.(type) {
	case string:
		if t == protocol.Masked {
			if ps, ok := prev.(string); ok {
				return ps
			}
			return ""
		}
		return t
	case map[string]any:
		pm, _ := prev.(map[string]any)
		out := make(map[string]any, len(t))
		for k, v := range t {
			out[k] = mergeSecrets(v, pm[k])
		}
		return out
	case []any:
		pa, _ := prev.([]any)
		bySID := map[string]any{}
		for _, p := range pa {
			if m, ok := p.(map[string]any); ok {
				if sid, ok := m["sid"].(string); ok {
					bySID[sid] = m
				}
			}
		}
		out := make([]any, len(t))
		for i, v := range t {
			var match any
			if m, ok := v.(map[string]any); ok {
				if sid, ok := m["sid"].(string); ok {
					match = bySID[sid]
				}
			}
			if match == nil && i < len(pa) {
				match = pa[i]
			}
			out[i] = mergeSecrets(v, match)
		}
		return out
	default:
		return next
	}
}
