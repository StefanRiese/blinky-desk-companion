// Command buddyd is the Desk Buddy host service: a stateless internet gateway that
// fetches the data a device subscribes to, over USB serial and TCP.
package main

import (
	"context"
	"fmt"
	"log/slog"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"
	_ "time/tzdata" // Windows has no zoneinfo files

	"github.com/StefanRiese/esp32_desk_buddy/host/internal/config"
	"github.com/StefanRiese/esp32_desk_buddy/host/internal/control"
	"github.com/StefanRiese/esp32_desk_buddy/host/internal/events"
	"github.com/StefanRiese/esp32_desk_buddy/host/internal/fetch"
	"github.com/StefanRiese/esp32_desk_buddy/host/internal/hub"
	"github.com/StefanRiese/esp32_desk_buddy/host/internal/session"
	"github.com/StefanRiese/esp32_desk_buddy/host/internal/source"
	"github.com/StefanRiese/esp32_desk_buddy/host/internal/transport/serial"
	"github.com/StefanRiese/esp32_desk_buddy/host/internal/transport/tcp"
	"github.com/spf13/cobra"
)

// Set by the release build (-ldflags "-X main.version=…").
var version = "0.1.0-dev"

const probeURL = "https://api.open-meteo.com/"

func main() {
	// buddyd may later be started by double-click (tray); don't let cobra refuse that on Windows.
	cobra.MousetrapHelpText = ""

	var cfgPath, tcpAddr, serialPort, logLevel string
	runE := func(*cobra.Command, []string) error {
		cfg, _, err := config.Load(cfgPath)
		if err != nil {
			return err
		}
		if tcpAddr != "" {
			cfg.TCP.Enabled, cfg.TCP.Listen = true, tcpAddr
		}
		switch serialPort {
		case "":
		case "off":
			cfg.Serial.Enabled = false
		default:
			cfg.Serial.Enabled, cfg.Serial.Port = true, serialPort
		}
		if logLevel != "" {
			cfg.Log.Level = logLevel
		}
		var level slog.Level
		if err := level.UnmarshalText([]byte(cfg.Log.Level)); err != nil {
			return fmt.Errorf("log level: %w", err)
		}
		slog.SetDefault(slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: level})))
		return run(cfg)
	}

	root := &cobra.Command{
		Use:           "buddyd",
		Short:         "Desk Buddy host service",
		Long:          "buddyd is the Desk Buddy host service: a stateless internet gateway that fetches\nthe data a device subscribes to, over USB serial and TCP. Without a command it runs the service.",
		Version:       version,
		Args:          cobra.NoArgs,
		RunE:          runE,
		SilenceUsage:  true,
		SilenceErrors: true,
	}
	pf := root.PersistentFlags()
	pf.StringVar(&cfgPath, "config", config.DefaultPath(), "config file (optional)")
	root.Flags().StringVar(&tcpAddr, "tcp", "", "enable the TCP transport on this address, e.g. :7531")
	root.Flags().StringVar(&serialPort, "serial", "", `serial port: "auto", a port name, or "off"`)
	root.Flags().StringVar(&logLevel, "log-level", "", "debug, info, warn, error")

	runCmd := &cobra.Command{Use: "run", Short: "Run the service (default)", Args: cobra.NoArgs, RunE: runE}
	runCmd.Flags().AddFlagSet(root.Flags())

	configCmd := &cobra.Command{Use: "config", Short: "Host config commands"}
	configCmd.AddCommand(&cobra.Command{
		Use:   "check",
		Short: "Validate and print the merged host config (secrets masked)",
		Args:  cobra.NoArgs,
		RunE: func(*cobra.Command, []string) error {
			cfg, found, err := config.Load(cfgPath)
			if err != nil {
				return err
			}
			if found {
				fmt.Printf("# %s\n", cfgPath)
			} else {
				fmt.Printf("# %s not found, using defaults + env\n", cfgPath)
			}
			fmt.Print(cfg.Masked().YAML())
			return nil
		},
	})
	versionCmd := &cobra.Command{
		Use:   "version",
		Short: "Print the version",
		Args:  cobra.NoArgs,
		Run:   func(*cobra.Command, []string) { fmt.Println("buddyd", version) },
	}
	root.AddCommand(runCmd, configCmd, versionCmd)

	if err := root.Execute(); err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}

func run(cfg config.Config) error {
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	client, err := fetch.New(fetch.Options{ProxyURL: cfg.Proxy.URL, CAFile: cfg.Proxy.CAFile, Version: version})
	if err != nil {
		return err
	}
	token, err := config.ControlToken(cfg, true)
	if err != nil {
		return err
	}
	reg := source.NewRegistry(client, cfg.Sources.Deny)
	h := hub.New(events.NewBus())
	hostname, _ := os.Hostname()
	opt := session.Options{
		HostName: "buddyd@" + hostname, Version: version,
		Registry: reg, Hub: h, Token: cfg.TCP.Token,
	}
	slog.Info("buddyd starting", "version", version, "sources", reg.Names())

	var wg sync.WaitGroup
	errc := make(chan error, 3)
	start := func(f func() error) {
		wg.Go(func() {
			if err := f(); err != nil {
				errc <- err
			}
		})
	}

	start(func() error {
		srv := &control.Server{Hub: h, Token: token, Version: version, Status: func() map[string]any {
			return map[string]any{
				"serial":  map[string]any{"enabled": cfg.Serial.Enabled, "port": cfg.Serial.Port},
				"tcp":     map[string]any{"enabled": cfg.TCP.Enabled, "listen": cfg.TCP.Listen, "token": cfg.TCP.Token != ""},
				"sources": reg.Names(),
			}
		}}
		return srv.ListenAndServe(ctx, cfg.Control.Listen)
	})
	if cfg.TCP.Enabled {
		start(func() error { return tcp.Serve(ctx, cfg.TCP.Listen, opt) })
	}
	if cfg.Serial.Enabled {
		start(func() error { serial.Run(ctx, cfg.Serial, opt); return nil })
	}
	start(func() error { watchInternet(ctx, client, h); return nil })

	select {
	case <-ctx.Done():
		slog.Info("shutting down")
	case err = <-errc:
		stop()
	}
	wg.Wait()
	return err
}

// watchInternet checks connectivity every minute and tells devices when it changes.
func watchInternet(ctx context.Context, c *fetch.Client, h *hub.Hub) {
	for {
		online := c.Online(ctx, probeURL)
		if ctx.Err() != nil {
			return
		}
		if online != h.Net() {
			slog.Info("internet state changed", "online", online)
		}
		h.SetNet(online)
		select {
		case <-ctx.Done():
			return
		case <-time.After(time.Minute):
		}
	}
}
