package autostart

import (
	"errors"
	"os"
	"path/filepath"
)

const label = "com.github.stefanriese.buddy-link"

func plistPath() (string, error) {
	home, err := os.UserHomeDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(home, "Library", "LaunchAgents", label+".plist"), nil
}

// Enable starts this program at login: the app with "open" when it runs from Buddy Link.app
// (so it gets its menu bar icon), else the program itself.
func Enable() error {
	exe, err := executable()
	if err != nil {
		return err
	}
	args := []string{exe}
	if app := appBundle(exe); app != "" {
		args = []string{"/usr/bin/open", "-a", app}
	}
	p, err := plistPath()
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
		return err
	}
	return os.WriteFile(p, []byte(launchAgent(args)), 0o644)
}

// Disable removes the LaunchAgent.
func Disable() error {
	p, err := plistPath()
	if err != nil {
		return err
	}
	if err := os.Remove(p); err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}
	return nil
}

// Enabled reports whether the LaunchAgent exists.
func Enabled() bool {
	p, err := plistPath()
	if err != nil {
		return false
	}
	_, err = os.Stat(p)
	return err == nil
}
