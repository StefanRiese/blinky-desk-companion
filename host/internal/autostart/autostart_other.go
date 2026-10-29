//go:build !windows && !darwin

package autostart

import (
	"errors"
	"os"
	"os/exec"
	"path/filepath"
)

const unitName = "buddy-link.service"

func unitPath() (string, error) {
	dir, err := os.UserConfigDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(dir, "systemd", "user", unitName), nil
}

// Enable writes a systemd user unit for this program and enables it, so it starts with
// the user's session (and now).
func Enable() error {
	exe, err := executable()
	if err != nil {
		return err
	}
	p, err := unitPath()
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
		return err
	}
	if err := os.WriteFile(p, []byte(userUnit(exe)), 0o644); err != nil {
		return err
	}
	return systemctlUser("enable", "--now", unitName)
}

// Disable stops and removes the unit.
func Disable() error {
	p, err := unitPath()
	if err != nil {
		return err
	}
	_ = systemctlUser("disable", "--now", unitName) // fine if it was not there
	if err := os.Remove(p); err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}
	return systemctlUser("daemon-reload")
}

// Enabled reports whether the unit exists.
func Enabled() bool {
	p, err := unitPath()
	if err != nil {
		return false
	}
	_, err = os.Stat(p)
	return err == nil
}

func systemctlUser(args ...string) error {
	out, err := exec.Command("systemctl", append([]string{"--user"}, args...)...).CombinedOutput()
	if err != nil {
		return errors.New(string(out))
	}
	return nil
}
