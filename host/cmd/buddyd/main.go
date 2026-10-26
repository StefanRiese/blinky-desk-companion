// Command buddyd (Buddy Link) is the Blinky host service: a stateless internet gateway that
// fetches the data a device subscribes to, over USB serial and TCP.
package main

import (
	"context"
	"fmt"
	"log/slog"
	"net"
	"os"
	"os/signal"
	"path/filepath"
	"strconv"
	"sync"
	"syscall"
	"time"
	_ "time/tzdata" // Windows has no zoneinfo files

	"github.com/StefanRiese/blinky-desk-companion/host/internal/config"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/control"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/discovery"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/events"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/fetch"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/hub"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/install"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/protocol"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/session"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/source"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/transport/serial"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/transport/tcp"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/tray"
	"github.com/spf13/cobra"
)

// Set by the release build (-ldflags "-X main.version=…").
var version = "0.1.0-dev"

const probeURL = "https://api.open-meteo.com/"

func main() {
	// buddyd is started by double-click on Windows (tray); don't let cobra refuse that.
	cobra.MousetrapHelpText = ""

	var cfgPath, tcpAddr, serialPort, logLevel string
	var showTray bool
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
		logOut := os.Stderr
		useTray := showTray && tray.Available
		if useTray && tray.HideOwnConsole() {
			// Started by double-click: no console to log to, so log next to the config.
			logPath := filepath.Join(filepath.Dir(cfgPath), "buddyd.log")
			if err := os.MkdirAll(filepath.Dir(logPath), 0o700); err == nil {
				if f, err := os.Create(logPath); err == nil {
					defer f.Close()
					logOut = f
				}
			}
		}
		slog.SetDefault(slog.New(slog.NewTextHandler(logOut, &slog.HandlerOptions{Level: level})))
		return run(cfg, useTray)
	}

	root := &cobra.Command{
		Use:           "buddyd",
		Short:         "Buddy Link, the Blinky host service",
		Long:          "buddyd (Buddy Link) is the Blinky host service: a stateless internet gateway that fetches\nthe data a device subscribes to, over USB serial and TCP. Without a command it runs the service.",
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
	root.Flags().BoolVar(&showTray, "tray", tray.Available, "show an icon in the taskbar (Windows) or menu bar (macOS) with a Close menu")

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
	var inst install.Options
	var purge bool
	installCmd := &cobra.Command{
		Use:   "install",
		Short: "Install buddyd as a system service (Linux/systemd, e.g. a Proxmox container)",
		Long: `Install copies this binary to /usr/local/bin/buddyd, writes /etc/buddyd/config.yaml
(kept if it exists), creates the buddyd system user and a systemd service, and starts it.
Run it again to update the binary. Devices find the service via mDNS (link.tcp_host "auto").`,
		Args: cobra.NoArgs,
		RunE: func(*cobra.Command, []string) error { inst.Out = os.Stdout; return install.Install(inst) },
	}
	installCmd.Flags().StringVar(&inst.TCP, "tcp", ":7531", `TCP listen address for devices on the network ("" = off)`)
	installCmd.Flags().StringVar(&inst.Serial, "serial", "auto", `serial port: "auto", a port name, or "off"`)
	installCmd.Flags().BoolVar(&inst.Start, "start", true, "enable and start the service")
	installCmd.Flags().StringVar(&inst.Root, "root", "", "write the files below this folder instead of / (for packaging; no user or service changes)")
	uninstallCmd := &cobra.Command{
		Use:   "uninstall",
		Short: "Stop and remove the system service",
		Args:  cobra.NoArgs,
		RunE:  func(*cobra.Command, []string) error { inst.Out = os.Stdout; return install.Uninstall(inst, purge) },
	}
	uninstallCmd.Flags().BoolVar(&purge, "purge", false, "also remove /etc/buddyd and the buddyd user")
	uninstallCmd.Flags().StringVar(&inst.Root, "root", "", "remove the files below this folder instead of /")

	root.AddCommand(runCmd, configCmd, versionCmd, installCmd, uninstallCmd)

	if err := root.Execute(); err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}

func run(cfg config.Config, useTray bool) error {
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
		if cfg.TCP.MDNS {
			start(func() error { announce(ctx, cfg, opt.HostName); return nil })
		}
	}
	if cfg.Serial.Enabled {
		start(func() error { serial.Run(ctx, cfg.Serial, opt); return nil })
	}
	start(func() error { watchInternet(ctx, client, h); return nil })

	wait := func() error {
		var err error
		select {
		case <-ctx.Done():
			slog.Info("shutting down")
		case err = <-errc:
			stop()
		}
		wg.Wait()
		return err
	}
	if !useTray {
		return wait()
	}
	// The tray needs the main goroutine (macOS); the service waits in the background.
	done := make(chan error, 1)
	go func() { done <- wait() }()
	tray.Run(ctx, tray.Options{Version: version, OnClose: func() {
		slog.Info("closed from the tray")
		stop()
	}})
	return <-done
}

// announce advertises the TCP transport with mDNS. A failure only means devices need a
// fixed link.tcp_host, so it is logged, not fatal.
func announce(ctx context.Context, cfg config.Config, name string) {
	_, portStr, err := net.SplitHostPort(cfg.TCP.Listen)
	port, _ := strconv.Atoi(portStr)
	if err != nil || port == 0 {
		slog.Warn("mdns: no fixed port to announce", "listen", cfg.TCP.Listen)
		return
	}
	txt := []string{"ver=" + version, "proto=" + strconv.Itoa(protocol.Version), "token=" + boolDigit(cfg.TCP.Token != "")}
	slog.Info("mdns: announcing", "service", discovery.Service, "name", name, "port", port)
	if err := discovery.Announce(ctx, name, port, txt); err != nil {
		slog.Warn("mdns: cannot announce", "err", err)
	}
}

func boolDigit(b bool) string {
	if b {
		return "1"
	}
	return "0"
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
