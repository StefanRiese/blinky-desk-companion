// buddy-ctl flash and ports: the first flash of a board over USB, through buddy-link (it
// lets go of the port meanwhile) or with --direct on its own.
package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"net/url"
	"os"
	"strings"
	"time"

	"github.com/spf13/cobra"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/fetch"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/firmware"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/flasher"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/jobs"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/transport/serial"
)

func (c *client) portsCmd() *cobra.Command {
	return &cobra.Command{
		Use: "ports", Short: "List the USB serial ports (and the Blinky on each)", Args: cobra.NoArgs,
		RunE: func(*cobra.Command, []string) error {
			if c.direct != nil {
				list, err := serial.List()
				if err != nil {
					return err
				}
				return printJSON(list)
			}
			return c.printGET("/v1/ports")
		},
	}
}

func (c *client) flashCmd() *cobra.Command {
	var req flasher.Request
	cmd := &cobra.Command{
		Use:   "flash --board ws146|amoled175 [--port PORT] [--version V] [FILE.bin]",
		Short: "Flash the Blinky firmware to a board on USB (first install or recovery)",
		Long: `flash writes the whole firmware (bootloader, partition table, app) to a board on USB.
Without FILE.bin it downloads blinky-firmware-<board>.bin of the latest release (or
--version) from GitHub and checks its SHA-256. Without --port it takes the only USB port.
Through buddy-link the port is released while flashing; --direct flashes without it.
If the board isn't found, hold its BOOT button while plugging it in.`,
		Args: cobra.MaximumNArgs(1),
		RunE: func(_ *cobra.Command, args []string) error {
			if len(args) == 1 {
				img, err := os.ReadFile(args[0])
				if err != nil {
					return err
				}
				req.Image = img
			}
			if c.direct != nil {
				return flashDirect(req)
			}
			return c.flashAPI(req)
		},
	}
	f := cmd.Flags()
	f.StringVar(&req.Board, "board", "", "the board: "+strings.Join(firmware.Boards, " or "))
	f.StringVar(&req.Port, "port", "", "the USB serial port (default: the only one)")
	f.StringVar(&req.Version, "version", "", "release to download, e.g. 0.2.0 (default: the latest)")
	cmd.MarkFlagRequired("board")
	return cmd
}

// onlyPort returns the only USB serial port, or an error that lists them.
func onlyPort(names []string) (string, error) {
	switch len(names) {
	case 0:
		return "", errors.New("no USB serial port found: is the board plugged in? (hold BOOT while plugging it in)")
	case 1:
		return names[0], nil
	}
	return "", fmt.Errorf("several USB serial ports (%s): choose one with --port", strings.Join(names, ", "))
}

func (c *client) flashAPI(req flasher.Request) error {
	if req.Port == "" {
		out, err := c.do(http.MethodGet, "/v1/ports", nil)
		if err != nil {
			return err
		}
		var list []serial.PortInfo
		json.Unmarshal(out, &list)
		var names []string
		for _, p := range list {
			names = append(names, p.Name)
		}
		if req.Port, err = onlyPort(names); err != nil {
			return err
		}
	}
	var out []byte
	var err error
	if req.Image != nil {
		q := url.Values{"port": {req.Port}, "board": {req.Board}}
		out, err = c.doRaw(http.MethodPost, "/v1/flash?"+q.Encode(), "application/octet-stream", req.Image)
	} else {
		out, err = c.do(http.MethodPost, "/v1/flash", req)
	}
	if err != nil {
		return err
	}
	var job jobs.Job
	if err := json.Unmarshal(out, &job); err != nil {
		return err
	}
	return c.follow(job.ID)
}

// follow prints a job's steps and progress until it ends.
func (c *client) follow(id string) error {
	seen, pct := 0, -1
	for {
		out, err := c.do(http.MethodGet, "/v1/jobs/"+url.PathEscape(id), nil)
		if err != nil {
			return err
		}
		var j jobs.Job
		if err := json.Unmarshal(out, &j); err != nil {
			return err
		}
		for _, l := range j.Log[min(seen, len(j.Log)):] {
			if !strings.HasPrefix(l, "error: ") { // the error comes back as the result
				fmt.Printf("\r%-60s\n", l)
			}
		}
		seen = len(j.Log)
		if p := 100 * j.Done / max(j.Total, 1); j.Total > 0 && j.State == "running" && p != pct && p < 100 {
			pct = p
			fmt.Printf("\r%3d %%", p)
		}
		switch j.State {
		case "done":
			return nil
		case "error":
			return errors.New(j.Error)
		}
		time.Sleep(300 * time.Millisecond)
	}
}

func flashDirect(req flasher.Request) error {
	if req.Port == "" {
		list, err := serial.List()
		if err != nil {
			return err
		}
		var names []string
		for _, p := range list {
			names = append(names, p.Name)
		}
		if req.Port, err = onlyPort(names); err != nil {
			return err
		}
	}
	client, err := fetch.New(fetch.Options{Version: version})
	if err != nil {
		return err
	}
	step := func(s string) { fmt.Printf("\r%-60s\n", s) }
	img, ver, err := flasher.Image(context.Background(), &firmware.Source{Client: client}, req, step)
	if err != nil {
		return err
	}
	progress := func(done, total int) { fmt.Printf("\r%3d %%", 100*done/total) }
	if err := flasher.FlashPort(req.Port, img, progress, step); err != nil {
		return err
	}
	step("done: the Blinky starts with firmware " + ver)
	return nil
}
