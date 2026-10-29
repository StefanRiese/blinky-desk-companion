// --direct: buddy-ctl talks to a Blinky on USB itself, as the setup page does, when no
// buddy-link is running: devices, device config, ports and flash work this way.
package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"time"

	"github.com/spf13/cobra"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/config"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/setupclient"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/transport/serial"
)

type direct struct {
	serial config.Serial
	port   string
}

func supportsDirect(cmd *cobra.Command) bool {
	switch cmd.CommandPath() {
	case "buddy-ctl devices", "buddy-ctl device config", "buddy-ctl ports", "buddy-ctl flash":
		return true
	}
	return false
}

// each opens every candidate port and calls f with the device on it; f returns true to stop.
func (d *direct) each(f func(port string, c *setupclient.Client) (bool, error)) error {
	cfg := d.serial
	cfg.Port = d.port
	ports := serial.Ports(cfg)
	if len(ports) == 0 {
		return errors.New("no USB serial port with a Blinky found")
	}
	var errs []error
	for _, name := range ports {
		p, err := serial.Open(name, cfg.Baud)
		if err != nil {
			errs = append(errs, fmt.Errorf("%s: %w (is buddy-link running? then leave out --direct)", name, err))
			continue
		}
		ctx, cancel := context.WithTimeout(context.Background(), 6*time.Second)
		c, err := setupclient.Dial(ctx, p, "buddy-ctl")
		cancel()
		if err != nil {
			p.Close()
			errs = append(errs, fmt.Errorf("%s: %w", name, err))
			continue
		}
		stop, err := f(name, c)
		p.Close()
		if err != nil || stop {
			return err
		}
	}
	return errors.Join(errs...)
}

func (d *direct) devices() error {
	var out []map[string]any
	err := d.each(func(port string, c *setupclient.Client) (bool, error) {
		out = append(out, map[string]any{"dev": c.Hello.Dev, "fw": c.Hello.FW, "board": c.Hello.Board, "transport": "serial", "addr": port})
		return false, nil
	})
	if len(out) == 0 {
		return err
	}
	return printJSON(out)
}

// config sends a config get/set to the device dev ("" = the only one found).
func (d *direct) config(dev string, body map[string]any) error {
	found := false
	err := d.each(func(port string, c *setupclient.Client) (bool, error) {
		if dev != "" && c.Hello.Dev != dev {
			return false, nil
		}
		found = true
		msg := map[string]any{"t": "config"}
		for k, v := range body {
			msg[k] = v
		}
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		reply, err := c.Request(ctx, msg)
		if err != nil {
			return true, fmt.Errorf("%s on %s: %w", c.Hello.Dev, port, err)
		}
		return true, printJSON(reply)
	})
	if !found && err == nil {
		err = fmt.Errorf("device %s not found on USB", dev)
	}
	return err
}

func printJSON(v any) error {
	enc := json.NewEncoder(os.Stdout)
	enc.SetIndent("", "  ")
	return enc.Encode(v)
}
