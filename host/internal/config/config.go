// Package config loads the optional host settings of buddyd.
//
// Only settings about the host itself live here (ports, proxy, log level).
// Everything about the device is configured on the device.
// Layering: defaults → config.yaml → BUDDYD_* env vars → command-line flags.
package config

import (
	"crypto/rand"
	"encoding/hex"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"reflect"
	"runtime"
	"strconv"
	"strings"

	"go.yaml.in/yaml/v3"
)

// Config is the buddyd host configuration.
type Config struct {
	Serial  Serial  `yaml:"serial"`
	TCP     TCP     `yaml:"tcp"`
	Control Control `yaml:"control"`
	Proxy   Proxy   `yaml:"proxy"`
	Sources Sources `yaml:"sources"`
	Log     Log     `yaml:"log"`
}

// Serial configures the USB serial transport.
type Serial struct {
	Enabled bool     `yaml:"enabled"`
	Port    string   `yaml:"port"`    // "auto" or a port name (COM5, /dev/cu.usbmodem1101, /dev/ttyACM0)
	Baud    int      `yaml:"baud"`    // ignored by native USB-CDC, used by UART bridges
	VIDPID  []string `yaml:"vid_pid"` // auto-detect filter, "303a" (any PID) or "1a86:7523"
}

// TCP configures the TCP transport devices connect to.
type TCP struct {
	Enabled bool   `yaml:"enabled"`
	Listen  string `yaml:"listen"`
	Token   string `yaml:"token"` // devices must send this token in hello (empty = no check)
	MDNS    bool   `yaml:"mdns"`  // announce as _blinky._tcp, so devices find it (link.tcp_host "auto")
}

// Control configures the local HTTP control API used by buddyctl.
type Control struct {
	Listen string `yaml:"listen"`
	Token  string `yaml:"token"` // empty = generated and stored in control.token next to the config
}

// Proxy configures outgoing HTTP.
type Proxy struct {
	URL    string `yaml:"url"`     // empty = HTTPS_PROXY / HTTP_PROXY / NO_PROXY env vars
	CAFile string `yaml:"ca_file"` // extra CA bundle, e.g. for corporate TLS inspection
}

// Sources restricts which sources this host offers.
type Sources struct {
	Deny []string `yaml:"deny"`
}

// Log configures logging.
type Log struct {
	Level string `yaml:"level"`
}

// Default returns the built-in defaults.
func Default() Config {
	return Config{
		Serial:  Serial{Enabled: true, Port: "auto", Baud: 115200, VIDPID: []string{"303a", "1a86:7523", "1a86:55d4"}},
		TCP:     TCP{Enabled: false, Listen: ":7531", MDNS: true},
		Control: Control{Listen: "127.0.0.1:7532"},
		Log:     Log{Level: "info"},
	}
}

// Dir returns the per-user config directory for buddyd.
func Dir() string {
	if d := os.Getenv("BUDDYD_CONFIG_DIR"); d != "" {
		return d
	}
	base, err := os.UserConfigDir()
	if err != nil {
		return "."
	}
	user := filepath.Join(base, "buddyd")
	// After `buddyd install` (Linux system service) the config is in SystemDir. Use it when
	// this user has no own config, so buddyctl on a server finds the service's token.
	if runtime.GOOS == "linux" {
		if _, err := os.Stat(user); errors.Is(err, os.ErrNotExist) {
			if _, err := os.Stat(filepath.Join(SystemDir, "config.yaml")); err == nil {
				return SystemDir
			}
		}
	}
	return user
}

// SystemDir holds the config of a system-wide install (`buddyd install` on Linux).
const SystemDir = "/etc/buddyd"

// DefaultPath returns the default config file path.
func DefaultPath() string {
	if p := os.Getenv("BUDDYD_CONFIG"); p != "" {
		return p
	}
	return filepath.Join(Dir(), "config.yaml")
}

// Load reads defaults, the file at path (missing file is fine) and env overrides.
// It returns whether the file existed.
func Load(path string) (Config, bool, error) {
	b, err := os.ReadFile(path)
	switch {
	case err == nil:
		cfg, err := FromYAML(b)
		if err != nil {
			err = fmt.Errorf("%s: %w", path, err)
		}
		return cfg, true, err
	case errors.Is(err, os.ErrNotExist):
		cfg, err := FromYAML(nil)
		return cfg, false, err
	default:
		return Default(), false, err
	}
}

// Validate checks the merged configuration.
func (c Config) Validate() error {
	if c.TCP.Enabled && c.TCP.Listen == "" {
		return errors.New("tcp.listen must be set when tcp is enabled")
	}
	if c.Control.Listen == "" {
		return errors.New("control.listen must be set")
	}
	for _, f := range c.Serial.VIDPID {
		if !validVIDPID(f) {
			return fmt.Errorf("serial.vid_pid: invalid entry %q (want \"303a\" or \"303a:1001\")", f)
		}
	}
	return nil
}

func validVIDPID(s string) bool {
	parts := strings.Split(s, ":")
	if len(parts) > 2 {
		return false
	}
	for _, p := range parts {
		if len(p) != 4 {
			return false
		}
		if _, err := strconv.ParseUint(p, 16, 16); err != nil {
			return false
		}
	}
	return true
}

// applyEnv sets leaf fields from env vars named PREFIX_SECTION_KEY (yaml tag path, upper case).
func applyEnv(v reflect.Value, prefix string) error {
	t := v.Type()
	for i := 0; i < t.NumField(); i++ {
		f := t.Field(i)
		tag, _, _ := strings.Cut(f.Tag.Get("yaml"), ",")
		if tag == "" || tag == "-" {
			continue
		}
		name := prefix + "_" + strings.ToUpper(tag)
		fv := v.Field(i)
		if fv.Kind() == reflect.Struct {
			if err := applyEnv(fv, name); err != nil {
				return err
			}
			continue
		}
		val, ok := os.LookupEnv(name)
		if !ok {
			continue
		}
		switch fv.Kind() {
		case reflect.String:
			fv.SetString(val)
		case reflect.Bool:
			b, err := strconv.ParseBool(val)
			if err != nil {
				return fmt.Errorf("%s: %w", name, err)
			}
			fv.SetBool(b)
		case reflect.Int:
			n, err := strconv.Atoi(val)
			if err != nil {
				return fmt.Errorf("%s: %w", name, err)
			}
			fv.SetInt(int64(n))
		case reflect.Slice:
			var items []string
			for s := range strings.SplitSeq(val, ",") {
				if s = strings.TrimSpace(s); s != "" {
					items = append(items, s)
				}
			}
			fv.Set(reflect.ValueOf(items))
		}
	}
	return nil
}

// expandRefs resolves ${env:NAME} and ${file:PATH} in string fields.
func expandRefs(v reflect.Value) error {
	for _, fv := range v.Fields() {
		switch fv.Kind() {
		case reflect.Struct:
			if err := expandRefs(fv); err != nil {
				return err
			}
		case reflect.String:
			s, err := ExpandRef(fv.String())
			if err != nil {
				return err
			}
			fv.SetString(s)
		}
	}
	return nil
}

// ExpandRef resolves a whole-value reference: ${env:NAME} or ${file:PATH}.
func ExpandRef(s string) (string, error) {
	if !strings.HasPrefix(s, "${") || !strings.HasSuffix(s, "}") {
		return s, nil
	}
	kind, arg, ok := strings.Cut(s[2:len(s)-1], ":")
	if !ok {
		return s, nil
	}
	switch kind {
	case "env":
		return os.Getenv(arg), nil
	case "file":
		b, err := os.ReadFile(arg)
		if err != nil {
			return "", fmt.Errorf("config reference %s: %w", s, err)
		}
		return strings.TrimSpace(string(b)), nil
	default:
		return "", fmt.Errorf("config reference %s: unknown kind %q", s, kind)
	}
}

// TokenPath is where the generated control API token is stored.
func TokenPath() string { return filepath.Join(Dir(), "control.token") }

// ControlToken returns the control API token: from config, env, or the token file.
// With create set, a missing token file is generated (user-only permissions).
func ControlToken(cfg Config, create bool) (string, error) {
	if cfg.Control.Token != "" {
		return cfg.Control.Token, nil
	}
	if t := os.Getenv("BUDDYD_CONTROL_TOKEN"); t != "" {
		return t, nil
	}
	p := TokenPath()
	b, err := os.ReadFile(p)
	if err == nil {
		return strings.TrimSpace(string(b)), nil
	}
	if !create || !errors.Is(err, os.ErrNotExist) {
		return "", fmt.Errorf("control token: %w", err)
	}
	raw := make([]byte, 16)
	if _, err := rand.Read(raw); err != nil {
		return "", err
	}
	tok := hex.EncodeToString(raw)
	if err := os.MkdirAll(filepath.Dir(p), 0o700); err != nil {
		return "", err
	}
	if err := os.WriteFile(p, []byte(tok+"\n"), 0o600); err != nil {
		return "", err
	}
	return tok, nil
}

// Masked returns a copy with secrets replaced, for printing.
func (c Config) Masked() Config {
	if c.TCP.Token != "" {
		c.TCP.Token = "***"
	}
	if c.Control.Token != "" {
		c.Control.Token = "***"
	}
	return c
}

// YAML renders the config as YAML.
func (c Config) YAML() string {
	b, _ := yaml.Marshal(c)
	return string(b)
}
