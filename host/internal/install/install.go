// Package install sets buddyd up as a system service. Linux (systemd) only for now:
// that covers Proxmox containers and VMs, Raspberry Pis and other servers.
package install

import (
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"os/user"
	"path/filepath"
	"runtime"
	"strings"
)

// Options configure the installation.
type Options struct {
	Root   string // prefix for all paths ("" = /); with a prefix, no users or services are touched
	TCP    string // TCP listen address, "" = no TCP
	Serial string // "auto", a port, or "off"
	Start  bool   // enable and start the service
	Out    io.Writer
}

// Paths of a system install.
const (
	BinPath    = "/usr/local/bin/buddyd"
	ConfigDir  = "/etc/buddyd"
	UnitPath   = "/etc/systemd/system/buddyd.service"
	User       = "buddyd"
	configName = "config.yaml"
)

const unit = `[Unit]
Description=Buddy Link, the Desk Buddy host service (buddyd)
Documentation=https://github.com/StefanRiese/esp32_desk_buddy
After=network-online.target
Wants=network-online.target

[Service]
User=buddyd
Group=buddyd
SupplementaryGroups=dialout
Environment=BUDDYD_CONFIG_DIR=/etc/buddyd
ExecStart=/usr/local/bin/buddyd run --config /etc/buddyd/config.yaml
Restart=on-failure
RestartSec=5
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
ReadWritePaths=/etc/buddyd

[Install]
WantedBy=multi-user.target
`

// Config returns the config file written on install.
func Config(tcp, serial string) string {
	var b strings.Builder
	b.WriteString("# buddyd config, written by `buddyd install`. See\n")
	b.WriteString("# https://github.com/StefanRiese/esp32_desk_buddy/blob/main/host/README.md#buddyd\n")
	switch serial {
	case "off":
		b.WriteString("serial: {enabled: false}\n")
	case "", "auto":
		b.WriteString("serial: {enabled: true, port: auto}\n")
	default:
		fmt.Fprintf(&b, "serial: {enabled: true, port: %q}\n", serial)
	}
	if tcp == "" {
		b.WriteString("tcp:    {enabled: false}\n")
	} else {
		fmt.Fprintf(&b, "tcp:    {enabled: true, listen: %q, mdns: true, token: \"\"}\n", tcp)
	}
	b.WriteString("control: {listen: 127.0.0.1:7532}\n")
	b.WriteString("log:    {level: info}\n")
	return b.String()
}

func supported(o Options) error {
	if runtime.GOOS != "linux" {
		return fmt.Errorf("install is only supported on Linux (systemd) so far; on %s run `buddyd` directly", runtime.GOOS)
	}
	if o.Root == "" && os.Geteuid() != 0 {
		return errors.New("install needs root: run it with sudo")
	}
	if o.Root == "" {
		if _, err := exec.LookPath("systemctl"); err != nil {
			return errors.New("systemd (systemctl) not found")
		}
	}
	return nil
}

// Install copies the running binary, writes the config (kept if it exists) and the
// systemd unit, creates the buddyd user and starts the service.
func Install(o Options) error {
	if err := supported(o); err != nil {
		return err
	}
	p := func(path string) string { return filepath.Join(o.Root, path) }
	say := func(format string, a ...any) { fmt.Fprintf(o.Out, format+"\n", a...) }

	self, err := os.Executable()
	if err != nil {
		return err
	}
	if self, err = filepath.EvalSymlinks(self); err != nil {
		return err
	}
	if self != p(BinPath) {
		if err := copyFile(self, p(BinPath), 0o755); err != nil {
			return fmt.Errorf("copy binary: %w", err)
		}
		say("installed %s", BinPath)
	}

	if o.Root == "" {
		if _, err := user.Lookup(User); err != nil {
			if out, err := exec.Command("useradd", "--system", "--no-create-home", "--home-dir", ConfigDir,
				"--shell", "/usr/sbin/nologin", "--groups", "dialout", User).CombinedOutput(); err != nil {
				return fmt.Errorf("useradd: %v: %s", err, out)
			}
			say("created system user %s", User)
		}
	}
	if err := os.MkdirAll(p(ConfigDir), 0o750); err != nil {
		return err
	}
	cfg := p(filepath.Join(ConfigDir, configName))
	if _, err := os.Stat(cfg); errors.Is(err, os.ErrNotExist) {
		if err := os.WriteFile(cfg, []byte(Config(o.TCP, o.Serial)), 0o640); err != nil {
			return err
		}
		say("wrote %s", filepath.Join(ConfigDir, configName))
	} else {
		say("kept existing %s", filepath.Join(ConfigDir, configName))
	}
	if o.Root == "" {
		if out, err := exec.Command("chown", "-R", User+":"+User, ConfigDir).CombinedOutput(); err != nil {
			return fmt.Errorf("chown: %v: %s", err, out)
		}
	}
	if err := os.MkdirAll(filepath.Dir(p(UnitPath)), 0o755); err != nil {
		return err
	}
	if err := os.WriteFile(p(UnitPath), []byte(unit), 0o644); err != nil {
		return err
	}
	say("wrote %s", UnitPath)

	if o.Root != "" {
		return nil
	}
	if err := systemctl("daemon-reload"); err != nil {
		return err
	}
	if o.Start {
		if err := systemctl("enable", "--now", "buddyd"); err != nil {
			return err
		}
		if err := systemctl("restart", "buddyd"); err != nil { // pick up a new binary on re-install
			return err
		}
		say("service buddyd enabled and started (logs: journalctl -u buddyd -f)")
	}
	return nil
}

// Uninstall stops and removes the service and binary; purge also removes config and user.
func Uninstall(o Options, purge bool) error {
	if err := supported(o); err != nil {
		return err
	}
	p := func(path string) string { return filepath.Join(o.Root, path) }
	say := func(format string, a ...any) { fmt.Fprintf(o.Out, format+"\n", a...) }
	if o.Root == "" {
		_ = systemctl("disable", "--now", "buddyd") // fine if it was not running
	}
	for _, f := range []string{UnitPath, BinPath} {
		if err := os.Remove(p(f)); err == nil {
			say("removed %s", f)
		} else if !errors.Is(err, os.ErrNotExist) {
			return err
		}
	}
	if o.Root == "" {
		_ = systemctl("daemon-reload")
	}
	if !purge {
		say("kept %s (use --purge to remove the config and the %s user)", ConfigDir, User)
		return nil
	}
	if err := os.RemoveAll(p(ConfigDir)); err != nil {
		return err
	}
	say("removed %s", ConfigDir)
	if o.Root == "" {
		if _, err := user.Lookup(User); err == nil {
			if out, err := exec.Command("userdel", User).CombinedOutput(); err != nil {
				return fmt.Errorf("userdel: %v: %s", err, out)
			}
			say("removed user %s", User)
		}
	}
	return nil
}

func systemctl(args ...string) error {
	if out, err := exec.Command("systemctl", args...).CombinedOutput(); err != nil {
		return fmt.Errorf("systemctl %s: %v: %s", strings.Join(args, " "), err, strings.TrimSpace(string(out)))
	}
	return nil
}

// copyFile copies via a temp file and rename, so a running binary can be replaced.
func copyFile(src, dst string, mode os.FileMode) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return err
	}
	tmp := dst + ".new"
	out, err := os.OpenFile(tmp, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, mode)
	if err != nil {
		return err
	}
	if _, err := io.Copy(out, in); err != nil {
		out.Close()
		os.Remove(tmp)
		return err
	}
	if err := out.Close(); err != nil {
		os.Remove(tmp)
		return err
	}
	return os.Rename(tmp, dst)
}
