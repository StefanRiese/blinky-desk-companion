// Editing the config file from the web UI: a few host settings, by their YAML path. The
// file keeps its other content and its comments.
package config

import (
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"reflect"
	"slices"
	"sort"
	"strings"

	"go.yaml.in/yaml/v3"
)

// Editable lists the settings the web UI may change.
var Editable = []string{
	"tcp.enabled", "tcp.listen", "tcp.token", "tcp.mdns",
	"serial.enabled", "serial.port",
	"proxy.url", "log.level",
	"gui.listen", "gui.password",
}

// Secret reports whether a setting is shown masked.
func Secret(path string) bool {
	return strings.HasSuffix(path, ".token") || strings.HasSuffix(path, ".password")
}

// EnvName returns the environment variable that overrides a setting.
func EnvName(path string) string {
	return "BUDDY_LINK_" + strings.ToUpper(strings.ReplaceAll(path, ".", "_"))
}

// Value returns a setting of c by its YAML path.
func (c Config) Value(path string) (any, bool) {
	v := reflect.ValueOf(c)
	for name := range strings.SplitSeq(path, ".") {
		f, ok := fieldByTag(v, name)
		if !ok {
			return nil, false
		}
		v = f
	}
	return v.Interface(), true
}

func fieldByTag(v reflect.Value, name string) (reflect.Value, bool) {
	if v.Kind() != reflect.Struct {
		return reflect.Value{}, false
	}
	for i := range v.NumField() {
		tag, _, _ := strings.Cut(v.Type().Field(i).Tag.Get("yaml"), ",")
		if tag == name {
			return v.Field(i), true
		}
	}
	return reflect.Value{}, false
}

// FromYAML merges defaults, the file content b and the env overrides, as Load does.
func FromYAML(b []byte) (Config, error) {
	cfg := Default()
	dec := yaml.NewDecoder(strings.NewReader(string(b)))
	dec.KnownFields(true)
	if err := dec.Decode(&cfg); err != nil && !errors.Is(err, io.EOF) {
		return cfg, err
	}
	if err := applyEnv(reflect.ValueOf(&cfg).Elem(), "BUDDY_LINK"); err != nil {
		return cfg, err
	}
	if err := expandRefs(reflect.ValueOf(&cfg).Elem()); err != nil {
		return cfg, err
	}
	return cfg, cfg.Validate()
}

// References returns the settings whose value in the file b is a ${env:…} or ${file:…}
// reference. Editing them would replace the reference, so the web UI doesn't.
func References(b []byte) []string {
	var doc yaml.Node
	if yaml.Unmarshal(b, &doc) != nil || len(doc.Content) == 0 {
		return nil
	}
	var out []string
	for _, p := range Editable {
		if n := find(doc.Content[0], strings.Split(p, ".")); n != nil && strings.HasPrefix(n.Value, "${") {
			out = append(out, p)
		}
	}
	return out
}

// Edit returns the file content b with the values in set (YAML path → value) changed or
// added. Comments and all other settings stay as they are. Only Editable paths are allowed.
func Edit(b []byte, set map[string]any) ([]byte, error) {
	var doc yaml.Node
	if err := yaml.Unmarshal(b, &doc); err != nil {
		return nil, err
	}
	if len(doc.Content) == 0 {
		doc = yaml.Node{Kind: yaml.DocumentNode, Content: []*yaml.Node{{Kind: yaml.MappingNode}}}
	}
	root := doc.Content[0]
	if root.Kind != yaml.MappingNode {
		return nil, errors.New("config file: the top level is not a mapping")
	}
	keys := make([]string, 0, len(set))
	for k := range set {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	for _, k := range keys {
		if !slices.Contains(Editable, k) {
			return nil, fmt.Errorf("%s cannot be changed here", k)
		}
		def, _ := Default().Value(k)
		if reflect.TypeOf(set[k]) != reflect.TypeOf(def) {
			return nil, fmt.Errorf("%s must be a %s", k, reflect.TypeOf(def))
		}
		var val yaml.Node
		if err := val.Encode(set[k]); err != nil {
			return nil, fmt.Errorf("%s: %w", k, err)
		}
		if err := put(root, strings.Split(k, "."), &val); err != nil {
			return nil, fmt.Errorf("%s: %w", k, err)
		}
	}
	var buf strings.Builder
	enc := yaml.NewEncoder(&buf)
	enc.SetIndent(2)
	if err := enc.Encode(&doc); err != nil {
		return nil, err
	}
	return []byte(buf.String()), enc.Close()
}

// find returns the value node at path in a mapping, or nil.
func find(m *yaml.Node, path []string) *yaml.Node {
	for _, name := range path {
		if m == nil || m.Kind != yaml.MappingNode {
			return nil
		}
		var next *yaml.Node
		for i := 0; i+1 < len(m.Content); i += 2 {
			if m.Content[i].Value == name {
				next = m.Content[i+1]
			}
		}
		m = next
	}
	return m
}

// put sets the value at path in a mapping, creating the sections it needs.
func put(m *yaml.Node, path []string, val *yaml.Node) error {
	for i := 0; i+1 < len(m.Content); i += 2 {
		if m.Content[i].Value != path[0] {
			continue
		}
		if len(path) == 1 {
			old := m.Content[i+1]
			val.LineComment, val.HeadComment, val.FootComment = old.LineComment, old.HeadComment, old.FootComment
			m.Content[i+1] = val
			return nil
		}
		if m.Content[i+1].Kind != yaml.MappingNode {
			if m.Content[i+1].Tag != "!!null" {
				return fmt.Errorf("%s is not a section", path[0])
			}
			m.Content[i+1] = &yaml.Node{Kind: yaml.MappingNode}
		}
		return put(m.Content[i+1], path[1:], val)
	}
	key := &yaml.Node{Kind: yaml.ScalarNode, Value: path[0]}
	if len(path) == 1 {
		m.Content = append(m.Content, key, val)
		return nil
	}
	sub := &yaml.Node{Kind: yaml.MappingNode, Style: m.Style}
	m.Content = append(m.Content, key, sub)
	return put(sub, path[1:], val)
}

// WriteFile replaces the config file in one step (a new file, then rename), readable by
// this user only.
func WriteFile(path string, b []byte) error {
	dir := filepath.Dir(path)
	if err := os.MkdirAll(dir, 0o700); err != nil {
		return err
	}
	f, err := os.CreateTemp(dir, ".config-*.yaml")
	if err != nil {
		return err
	}
	defer func() { _ = os.Remove(f.Name()) }() // gone after the rename; left over after an error
	if _, err := f.Write(b); err != nil {
		f.Close()
		return err
	}
	if err := f.Chmod(0o600); err != nil && !errors.Is(err, errors.ErrUnsupported) {
		f.Close()
		return err
	}
	if err := f.Close(); err != nil {
		return err
	}
	return os.Rename(f.Name(), path)
}
