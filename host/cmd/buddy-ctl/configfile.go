// Settings export and import: buddy-ctl device export writes a Blinky's settings to a JSON
// file, buddy-ctl device import sets them on a Blinky again (the same one or another).
// Both use the device's config get/set, so the firmware needs nothing for it. The web UI
// (internal/control/web) does the same in the browser.
package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"reflect"
	"sort"
	"time"

	"github.com/spf13/cobra"
)

// settingsFile is the export format. Secrets (WiFi password, tokens) are left out: the
// device only gives them masked.
type settingsFile struct {
	Blinky   string         `json:"blinky"` // always "settings"
	Dev      string         `json:"dev,omitempty"`
	Board    string         `json:"board,omitempty"`
	FW       string         `json:"fw,omitempty"`
	Exported string         `json:"exported,omitempty"`
	Settings map[string]any `json:"settings"`
}

const masked = "***"

// deviceInfo is what export writes about the device besides its settings.
type deviceInfo struct {
	Dev, Board, FW string
}

// exportFile builds the file from the device's values ("config" get "*").
func exportFile(info deviceInfo, values map[string]any, now time.Time) settingsFile {
	out := map[string]any{}
	for k, v := range values {
		if v != masked {
			out[k] = v
		}
	}
	return settingsFile{Blinky: "settings", Dev: info.Dev, Board: info.Board, FW: info.FW,
		Exported: now.UTC().Format(time.RFC3339), Settings: out}
}

// readSettings reads an export file. It also takes the output of
// buddy-ctl device config --get ({"values": …}) and a plain {"key": value} object.
func readSettings(data []byte) (map[string]any, error) {
	var f struct {
		Settings map[string]any `json:"settings"`
		Values   map[string]any `json:"values"`
	}
	if err := json.Unmarshal(data, &f); err != nil {
		return nil, fmt.Errorf("not a settings file: %w", err)
	}
	switch {
	case f.Settings != nil:
		return f.Settings, nil
	case f.Values != nil:
		return f.Values, nil
	}
	var plain map[string]any
	if err := json.Unmarshal(data, &plain); err != nil || len(plain) == 0 {
		return nil, errors.New("not a settings file: no settings in it")
	}
	return plain, nil
}

// importPlan compares the file with the device's current values. set holds what changes;
// skipped the keys the device doesn't know (other firmware), masked secrets and "dev"
// (the device keeps its own name).
func importPlan(file, current map[string]any) (set map[string]any, skipped []string) {
	set = map[string]any{}
	for k, v := range file {
		cur, known := current[k]
		switch {
		case !known || k == "dev" || v == masked:
			skipped = append(skipped, k)
		case !reflect.DeepEqual(v, cur):
			set[k] = v
		}
	}
	sort.Strings(skipped)
	return set, skipped
}

// configRequest sends a config message body to the device, through buddy-link or --direct.
func (c *client) configRequest(dev string, body map[string]any) (map[string]any, deviceInfo, error) {
	if c.direct != nil {
		return c.direct.request(dev, body)
	}
	if dev == "" {
		return nil, deviceInfo{}, errors.New(`required flag "dev" not set`)
	}
	out, err := c.do(http.MethodPost, "/v1/devices/"+url.PathEscape(dev)+"/config", body)
	if err != nil {
		return nil, deviceInfo{}, err
	}
	var reply map[string]any
	if err := json.Unmarshal(out, &reply); err != nil {
		return nil, deviceInfo{}, err
	}
	info := deviceInfo{Dev: dev}
	if list, err := c.do(http.MethodGet, "/v1/devices", nil); err == nil {
		var devs []struct{ Dev, Board, FW string }
		if json.Unmarshal(list, &devs) == nil {
			for _, d := range devs {
				if d.Dev == dev {
					info = deviceInfo{Dev: d.Dev, Board: d.Board, FW: d.FW}
				}
			}
		}
	}
	return reply, info, nil
}

// allValues reads every setting of the device.
func (c *client) allValues(dev string) (map[string]any, deviceInfo, error) {
	reply, info, err := c.configRequest(dev, map[string]any{"get": []string{"*"}})
	if err != nil {
		return nil, info, err
	}
	if reply["t"] == "err" {
		return nil, info, fmt.Errorf("device: %v", reply["msg"])
	}
	values, _ := reply["values"].(map[string]any)
	if values == nil {
		return nil, info, errors.New("device sent no settings")
	}
	return values, info, nil
}

func (c *client) exportCmd() *cobra.Command {
	var dev string
	cmd := &cobra.Command{
		Use:   "export --dev ID [FILE.json]",
		Short: "Save a device's settings to a JSON file (default: print them); secrets are left out",
		Args:  cobra.MaximumNArgs(1),
		RunE: func(_ *cobra.Command, args []string) error {
			values, info, err := c.allValues(dev)
			if err != nil {
				return err
			}
			f := exportFile(info, values, time.Now())
			b, err := json.MarshalIndent(f, "", "  ")
			if err != nil {
				return err
			}
			b = append(b, '\n')
			if len(args) == 0 {
				_, err = os.Stdout.Write(b)
				return err
			}
			if err := os.WriteFile(args[0], b, 0o600); err != nil {
				return err
			}
			fmt.Fprintf(os.Stderr, "%d settings of %s written to %s (without passwords and tokens)\n",
				len(f.Settings), info.Dev, args[0])
			return nil
		},
	}
	cmd.Flags().StringVar(&dev, "dev", "", "device id (see buddy-ctl devices); with --direct optional when one Blinky is on USB")
	return cmd
}

func (c *client) importCmd() *cobra.Command {
	var dev string
	var dryRun bool
	cmd := &cobra.Command{
		Use:   "import --dev ID FILE.json",
		Short: `Set a device's settings from a file of "buddy-ctl device export" ("-" = stdin)`,
		Long: `import sets the settings from the file that differ on the device, in one step (all or
nothing). Settings the device doesn't know, masked secrets and the device name "dev" are
skipped; secrets the file leaves out keep their values on the device.`,
		Args: cobra.ExactArgs(1),
		RunE: func(_ *cobra.Command, args []string) error {
			var data []byte
			var err error
			if args[0] == "-" {
				data, err = io.ReadAll(os.Stdin)
			} else {
				data, err = os.ReadFile(args[0])
			}
			if err != nil {
				return err
			}
			file, err := readSettings(data)
			if err != nil {
				return err
			}
			current, info, err := c.allValues(dev)
			if err != nil {
				return err
			}
			set, skipped := importPlan(file, current)
			if len(skipped) > 0 {
				fmt.Fprintf(os.Stderr, "skipped: %v\n", skipped)
			}
			keys := make([]string, 0, len(set))
			for k := range set {
				keys = append(keys, k)
			}
			sort.Strings(keys)
			if len(set) == 0 {
				fmt.Fprintf(os.Stderr, "%s has these settings already\n", info.Dev)
				return nil
			}
			if dryRun {
				fmt.Fprintf(os.Stderr, "would change on %s: %v\n", info.Dev, keys)
				return nil
			}
			reply, _, err := c.configRequest(info.Dev, map[string]any{"set": set})
			if err != nil {
				return err
			}
			if reply["t"] == "err" {
				return fmt.Errorf("device: %v", reply["msg"])
			}
			fmt.Fprintf(os.Stderr, "changed on %s: %v\n", info.Dev, keys)
			return nil
		},
	}
	cmd.Flags().StringVar(&dev, "dev", "", "device id (see buddy-ctl devices); with --direct optional when one Blinky is on USB")
	cmd.Flags().BoolVar(&dryRun, "dry-run", false, "only show what would change")
	return cmd
}
