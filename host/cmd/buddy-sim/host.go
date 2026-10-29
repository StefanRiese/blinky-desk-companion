// "buddy-sim host": a stand-in for buddy-link with fixed data and no internet, for testing
// the firmware (or the emulator) the same way every time. It speaks the real protocol, so
// a Blinky connects to it over TCP or USB as to buddy-link.
package main

import (
	"context"
	"fmt"
	"log/slog"
	"net"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/spf13/cobra"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/config"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/events"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/fakehost"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/hub"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/session"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/source"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/transport/serial"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/transport/tcp"
)

type hostOpts struct {
	tcpAddr, serialPort, at string
	offline, verbose        bool
}

func hostCmd() *cobra.Command {
	var o hostOpts
	cmd := &cobra.Command{
		Use:   "host",
		Short: "Stand in for buddy-link with fixed data and no internet (for firmware tests)",
		Long: `host answers a Blinky like buddy-link does, with fixed data: Düsseldorf, 11.5 °C and rain,
air quality 32, host stats. It needs no internet. Connect the device over TCP (--tcp) or
USB (--serial). --at starts the clock at a fixed time; --offline reports no internet, so
the device shows how it handles a host without internet. --verbose logs every line.`,
		Args: cobra.NoArgs,
		RunE: func(*cobra.Command, []string) error { return runHost(o) },
	}
	f := cmd.Flags()
	f.StringVar(&o.tcpAddr, "tcp", "127.0.0.1:7531", `TCP address devices connect to ("" = off)`)
	f.StringVar(&o.serialPort, "serial", "", `serial port of a device on USB, or "auto"`)
	f.StringVar(&o.at, "at", "", "start the clock at this time (RFC 3339, e.g. 2026-10-26T07:16:00+01:00)")
	f.BoolVar(&o.offline, "offline", false, "report that the host has no internet")
	f.BoolVar(&o.verbose, "verbose", false, "log every message")
	return cmd
}

func runHost(o hostOpts) error {
	level := slog.LevelInfo
	if o.verbose {
		level = slog.LevelDebug
	}
	slog.SetDefault(slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: level})))
	now := time.Now
	if o.at != "" {
		start, err := time.Parse(time.RFC3339, o.at)
		if err != nil {
			return fmt.Errorf("--at: %w", err)
		}
		offset := time.Until(start)
		now = func() time.Time { return time.Now().Add(offset) }
	}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	h := hub.New(events.NewBus())
	h.SetNet(!o.offline)
	opt := session.Options{HostName: "buddy-sim host", Version: version,
		Registry: source.NewRegistryWith(fakehost.Sources(now)...), Hub: h}
	done := make(chan struct{}, 2)
	running := 0
	if o.tcpAddr != "" {
		ln, err := net.Listen("tcp", o.tcpAddr)
		if err != nil {
			return err
		}
		fmt.Printf("buddy-sim host: devices connect over TCP to %s\n", ln.Addr())
		running++
		go func() {
			if err := tcp.ServeListener(ctx, ln, opt); err != nil {
				slog.Error("tcp", "err", err)
			}
			done <- struct{}{}
		}()
	}
	if o.serialPort != "" {
		cfg := config.Default().Serial
		cfg.Port = o.serialPort
		fmt.Printf("buddy-sim host: looking for a device on %s\n", o.serialPort)
		running++
		go func() { serial.Run(ctx, cfg, opt); done <- struct{}{} }()
	}
	if running == 0 {
		return fmt.Errorf("nothing to serve: give --tcp or --serial")
	}
	for range running {
		<-done
	}
	return nil
}
