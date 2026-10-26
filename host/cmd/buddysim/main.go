// Command buddysim emulates a Desk Buddy device for testing buddyd without hardware.
//
//	buddysim device [--nvs FILE] [--http ADDR] [--host ADDR | --serial PORT]
//
// It connects to buddyd like the firmware would, keeps its settings in an emulated
// NVS file, and shows the round display plus its internals in the browser.
package main

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"os/signal"
	"runtime"
	"syscall"
	_ "time/tzdata"

	"github.com/StefanRiese/esp32_desk_buddy/host/internal/fetch"
	"github.com/StefanRiese/esp32_desk_buddy/host/internal/sim"
	"github.com/spf13/cobra"
)

var version = "0.1.0-dev"

// deviceOpts are the flags of "buddysim device".
type deviceOpts struct {
	nvsPath, httpAddr, hostAddr, serial, devID, token string
	openUI                                            bool
}

func main() {
	var o deviceOpts
	root := &cobra.Command{
		Use:           "buddysim",
		Short:         "Desk Buddy device emulator",
		Version:       version,
		SilenceUsage:  true,
		SilenceErrors: true,
	}
	device := &cobra.Command{
		Use:   "device",
		Short: "Emulate a device: connect to buddyd and show the display in the browser",
		Args:  cobra.NoArgs,
		RunE:  func(*cobra.Command, []string) error { return runDevice(o) },
	}
	f := device.Flags()
	f.StringVar(&o.nvsPath, "nvs", "buddysim-nvs.json", "emulated NVS settings file (created with factory settings if missing)")
	f.StringVar(&o.httpAddr, "http", "127.0.0.1:7600", "address of the emulator web UI")
	f.StringVar(&o.hostAddr, "host", "", "buddyd TCP address, overrides link.tcp_host from NVS (e.g. 127.0.0.1:7531)")
	f.StringVar(&o.serial, "serial", "", "serial port / PTY to talk to buddyd over, overrides link.serial from NVS")
	f.StringVar(&o.devID, "dev", "buddy-sim01", "device id for a new NVS file")
	f.StringVar(&o.token, "token", "", "TCP token, overrides link.token from NVS")
	f.BoolVar(&o.openUI, "open", false, "open the web UI in the browser")
	device.MarkFlagsMutuallyExclusive("host", "serial")
	root.AddCommand(device)

	if err := root.Execute(); err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}

func runDevice(o deviceOpts) error {
	nvs, err := sim.LoadNVS(o.nvsPath, o.devID)
	if err != nil {
		return err
	}
	overrides := map[string]any{}
	if o.hostAddr != "" {
		overrides["link.tcp_host"] = o.hostAddr
		overrides["link.serial"] = ""
	}
	if o.serial != "" {
		overrides["link.serial"] = o.serial
	}
	if o.token != "" {
		overrides["link.token"] = o.token
	}
	if len(overrides) > 0 {
		if err := nvs.SetMany(overrides); err != nil {
			return err
		}
	}
	client, err := fetch.New(fetch.Options{Version: version})
	if err != nil {
		return err
	}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	dev := sim.New(nvs, client)
	go dev.Run(ctx)
	return dev.Serve(ctx, o.httpAddr, func(url string) {
		fmt.Printf("buddysim: device %s, settings in %s\n", nvs.String("dev"), nvs.Path())
		fmt.Printf("buddysim: web UI on %s\n", url)
		if o.openUI {
			openBrowser(url)
		}
	})
}

func openBrowser(url string) {
	var cmd *exec.Cmd
	switch runtime.GOOS {
	case "windows":
		cmd = exec.Command("rundll32", "url.dll,FileProtocolHandler", url)
	case "darwin":
		cmd = exec.Command("open", url)
	default:
		cmd = exec.Command("xdg-open", url)
	}
	cmd.Start()
}
