// Command buddyctl talks to a running buddy-link through its local control API.
package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/config"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/control"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/desktop"
	"github.com/spf13/cobra"
)

var version = "0.1.0-dev"

type client struct {
	base  string
	token string
	http  *http.Client
}

func main() {
	var cfgPath, baseURL, token string
	c := &client{}
	root := &cobra.Command{
		Use:           "buddyctl",
		Short:         "Control a running buddy-link",
		Version:       version,
		SilenceUsage:  true,
		SilenceErrors: true,
		// Find the control API address and token before any command talks to it.
		PersistentPreRunE: func(cmd *cobra.Command, _ []string) error {
			if !needsAPI(cmd) {
				return nil
			}
			cfg, _, err := config.Load(cfgPath)
			if err != nil {
				return err
			}
			if baseURL == "" {
				baseURL = "http://" + cfg.Control.Listen
			}
			if token == "" {
				token, err = config.ControlToken(cfg, false)
				if err != nil {
					return fmt.Errorf("%v (is buddy-link running? it creates the token on first start)", err)
				}
			}
			*c = client{base: strings.TrimRight(baseURL, "/"), token: token, http: &http.Client{Timeout: 15 * time.Second}}
			return nil
		},
	}
	pf := root.PersistentFlags()
	pf.StringVar(&cfgPath, "config", config.DefaultPath(), "buddy-link config file (to find the control API address)")
	pf.StringVar(&baseURL, "url", os.Getenv("BUDDY_LINK_CONTROL_URL"), "control API base URL (default from config, e.g. http://127.0.0.1:7532)")
	pf.StringVar(&token, "token", "", "control API token (default from the token file / BUDDY_LINK_CONTROL_TOKEN)")

	var printOnly bool
	gui := &cobra.Command{
		Use: "gui", Short: "Open the buddy-link web UI in the browser (logged in)", Args: cobra.NoArgs,
		RunE: func(*cobra.Command, []string) error {
			if _, err := c.do(http.MethodGet, "/v1/status", nil); err != nil {
				return err
			}
			u, err := url.Parse(c.base)
			if err != nil {
				return err
			}
			link := control.LoginURL(u.Host, c.token)
			if printOnly {
				fmt.Println(link)
				return nil
			}
			if err := desktop.Open(link); err != nil {
				fmt.Println(link)
				return fmt.Errorf("cannot open the browser (%v); open the link above", err)
			}
			return nil
		},
	}
	gui.Flags().BoolVar(&printOnly, "print", false, "only print the login link (it contains the token)")

	root.AddCommand(c.notifyCmd(), c.moodCmd(), c.dataCmd(), c.pageCmd(), c.deviceCmd(), gui,
		&cobra.Command{
			Use: "devices", Short: "List connected devices", Args: cobra.NoArgs,
			RunE: func(*cobra.Command, []string) error { return c.printGET("/v1/devices") },
		},
		&cobra.Command{
			Use: "status", Short: "Service status", Args: cobra.NoArgs,
			RunE: func(*cobra.Command, []string) error { return c.printGET("/v1/status") },
		},
		&cobra.Command{
			Use: "monitor", Short: "Stream device logs and events", Args: cobra.NoArgs,
			RunE: func(*cobra.Command, []string) error { return c.monitor() },
		},
		&cobra.Command{
			Use: "version", Short: "Print the version", Args: cobra.NoArgs,
			Run: func(*cobra.Command, []string) { fmt.Println("buddyctl", version) },
		},
	)
	if err := root.Execute(); err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}

// needsAPI reports whether cmd talks to buddy-link (not version, help or shell completion).
func needsAPI(cmd *cobra.Command) bool {
	for p := cmd; p != nil; p = p.Parent() {
		switch p.Name() {
		case "version", "help", "completion", cobra.ShellCompRequestCmd, cobra.ShellCompNoDescRequestCmd:
			return false
		}
	}
	return true
}

func (c *client) do(method, path string, body any) ([]byte, error) {
	var rd io.Reader
	if body != nil {
		b, err := json.Marshal(body)
		if err != nil {
			return nil, err
		}
		rd = bytes.NewReader(b)
	}
	req, err := http.NewRequest(method, c.base+path, rd)
	if err != nil {
		return nil, err
	}
	req.Header.Set("Authorization", "Bearer "+c.token)
	if body != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	resp, err := c.http.Do(req)
	if err != nil {
		return nil, fmt.Errorf("%v (is buddy-link running?)", err)
	}
	defer resp.Body.Close()
	out, _ := io.ReadAll(resp.Body)
	if resp.StatusCode >= 300 {
		var e struct {
			Error string `json:"error"`
		}
		if json.Unmarshal(out, &e) == nil && e.Error != "" {
			return out, errors.New(e.Error)
		}
		return out, fmt.Errorf("HTTP %d: %s", resp.StatusCode, bytes.TrimSpace(out))
	}
	return out, nil
}

func (c *client) printGET(path string) error {
	out, err := c.do(http.MethodGet, path, nil)
	if err != nil {
		return err
	}
	os.Stdout.Write(out)
	return nil
}

func (c *client) sent(out []byte, err error) error {
	if err != nil {
		return err
	}
	var r struct {
		Sent int `json:"sent"`
	}
	json.Unmarshal(out, &r)
	fmt.Printf("sent to %d device(s)\n", r.Sent)
	return nil
}

func (c *client) notifyCmd() *cobra.Command {
	var mood, icon, sound, dev string
	var dur int
	cmd := &cobra.Command{
		Use:   "notify TEXT...",
		Short: "Show a notification",
		Args:  cobra.MinimumNArgs(1),
		RunE: func(_ *cobra.Command, args []string) error {
			body := map[string]any{"text": strings.Join(args, " "), "mood": mood, "icon": icon, "sound": sound, "dev": dev}
			if dur > 0 {
				body["dur"] = dur
			}
			return c.sent(c.do(http.MethodPost, "/v1/notify", body))
		},
	}
	f := cmd.Flags()
	f.StringVar(&mood, "mood", "", "mood while the notification shows")
	f.StringVar(&icon, "icon", "", "icon name")
	f.StringVar(&sound, "sound", "", "sound name")
	f.IntVar(&dur, "dur", 0, "seconds to show (device default when 0)")
	f.StringVar(&dev, "dev", "", "device id (default all)")
	return cmd
}

func (c *client) moodCmd() *cobra.Command {
	var ttl int
	var dev string
	cmd := &cobra.Command{
		Use:   "mood NAME",
		Short: "Override the mood for a while",
		Args:  cobra.ExactArgs(1),
		RunE: func(_ *cobra.Command, args []string) error {
			return c.sent(c.do(http.MethodPost, "/v1/mood", map[string]any{"mood": args[0], "ttl": ttl, "dev": dev}))
		},
	}
	cmd.Flags().IntVar(&ttl, "ttl", 30, "seconds")
	cmd.Flags().StringVar(&dev, "dev", "", "device id (default all)")
	return cmd
}

func (c *client) dataCmd() *cobra.Command {
	var ttl int
	var dev string
	var del []string
	cmd := &cobra.Command{
		Use:   "data KEY=VALUE...",
		Short: "Push data values (user.* keys)",
		RunE: func(_ *cobra.Command, args []string) error {
			kv, err := keyValues(args)
			if err != nil {
				return err
			}
			if len(kv) == 0 && len(del) == 0 {
				return errors.New("data: KEY=VALUE or --del KEY is required")
			}
			body := map[string]any{"dev": dev}
			if len(kv) > 0 {
				body["kv"] = kv
			}
			if len(del) > 0 {
				body["del"] = del
			}
			if ttl > 0 {
				body["ttl"] = ttl
			}
			return c.sent(c.do(http.MethodPost, "/v1/data", body))
		},
	}
	f := cmd.Flags()
	f.IntVar(&ttl, "ttl", 0, "seconds until the values expire")
	f.StringArrayVar(&del, "del", nil, "delete a key (repeatable)")
	f.StringVar(&dev, "dev", "", "device id (default all)")
	return cmd
}

// keyValues parses KEY=VALUE; values become numbers, booleans or JSON when they parse as such.
func keyValues(pos []string) (map[string]any, error) {
	kv := map[string]any{}
	for _, p := range pos {
		k, v, ok := strings.Cut(p, "=")
		if !ok || k == "" {
			return nil, fmt.Errorf("expected KEY=VALUE, got %q", p)
		}
		kv[k] = parseValue(v)
	}
	return kv, nil
}

func parseValue(v string) any {
	if f, err := strconv.ParseFloat(v, 64); err == nil {
		return f
	}
	if b, err := strconv.ParseBool(v); err == nil {
		return b
	}
	if strings.HasPrefix(v, "[") || strings.HasPrefix(v, "{") {
		var j any
		if json.Unmarshal([]byte(v), &j) == nil {
			return j
		}
	}
	return v
}

func (c *client) pageCmd() *cobra.Command {
	var file, dev string
	var del bool
	cmd := &cobra.Command{
		Use:   "page NAME (--json FILE | --del)",
		Short: "Set or delete a custom page",
		Args:  cobra.ExactArgs(1),
		RunE: func(_ *cobra.Command, args []string) error {
			path := "/v1/pages/" + url.PathEscape(args[0])
			if dev != "" {
				path += "?dev=" + url.QueryEscape(dev)
			}
			if del {
				return c.sent(c.do(http.MethodDelete, path, nil))
			}
			b, err := os.ReadFile(file)
			if err != nil {
				return err
			}
			var body map[string]any
			if err := json.Unmarshal(b, &body); err != nil {
				return fmt.Errorf("%s: %v", file, err)
			}
			return c.sent(c.do(http.MethodPut, path, body))
		},
	}
	f := cmd.Flags()
	f.StringVar(&file, "json", "", "page definition (JSON file)")
	f.BoolVar(&del, "del", false, "delete the page")
	f.StringVar(&dev, "dev", "", "device id (default all)")
	cmd.MarkFlagsMutuallyExclusive("json", "del")
	cmd.MarkFlagsOneRequired("json", "del")
	cmd.MarkFlagFilename("json", "json")
	return cmd
}

func (c *client) deviceCmd() *cobra.Command {
	var dev string
	var get bool
	cfg := &cobra.Command{
		Use:   "config --dev ID (KEY=VALUE... | --get [KEY...])",
		Short: `Set device settings, or read them with --get ("*" = all)`,
		RunE: func(_ *cobra.Command, args []string) error {
			body := map[string]any{}
			if get {
				if len(args) == 0 {
					args = []string{"*"}
				}
				body["get"] = args
			} else {
				kv, err := keyValues(args)
				if err != nil {
					return err
				}
				if len(kv) == 0 {
					return errors.New("device config: KEY=VALUE or --get is required")
				}
				body["set"] = kv
			}
			out, err := c.do(http.MethodPost, "/v1/devices/"+url.PathEscape(dev)+"/config", body)
			if err != nil {
				return err
			}
			os.Stdout.Write(out)
			return nil
		},
	}
	cfg.Flags().StringVar(&dev, "dev", "", "device id (see buddyctl devices)")
	cfg.Flags().BoolVar(&get, "get", false, "read settings instead of setting them")
	cfg.MarkFlagRequired("dev")
	device := &cobra.Command{Use: "device", Short: "Device commands"}
	device.AddCommand(cfg)
	return device
}

func (c *client) monitor() error {
	req, err := http.NewRequest(http.MethodGet, c.base+"/v1/events", nil)
	if err != nil {
		return err
	}
	req.Header.Set("Authorization", "Bearer "+c.token)
	resp, err := (&http.Client{}).Do(req)
	if err != nil {
		return fmt.Errorf("%v (is buddy-link running?)", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("HTTP %d", resp.StatusCode)
	}
	sc := bufio.NewScanner(resp.Body)
	for sc.Scan() {
		line := sc.Text()
		if !strings.HasPrefix(line, "data: ") {
			continue
		}
		var ev struct {
			Type string          `json:"type"`
			Dev  string          `json:"dev"`
			Time time.Time       `json:"time"`
			Data json.RawMessage `json:"data"`
		}
		if json.Unmarshal([]byte(line[6:]), &ev) != nil {
			continue
		}
		fmt.Printf("%s %-12s %-14s %s\n", ev.Time.Format("15:04:05"), ev.Type, ev.Dev, ev.Data)
	}
	return sc.Err()
}
