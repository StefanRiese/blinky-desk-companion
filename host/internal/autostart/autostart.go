// Package autostart starts buddy-link when the user logs in: a Run entry in the Windows
// registry, a LaunchAgent on macOS, a systemd user unit on Linux. It runs as the user,
// not as a system service, so the tray icon shows and the user's proxy settings apply.
package autostart

import (
	"os"
	"path/filepath"
	"strings"
)

// Name is the entry's name where the system shows it (Windows Task Manager, macOS
// Login Items).
const Name = "Buddy Link"

// executable returns the running program's path, with symlinks resolved.
func executable() (string, error) {
	exe, err := os.Executable()
	if err != nil {
		return "", err
	}
	if p, err := filepath.EvalSymlinks(exe); err == nil {
		exe = p
	}
	return exe, nil
}

// appBundle returns the .app folder exe is in on macOS ("" if none).
func appBundle(exe string) string {
	if i := strings.Index(exe, ".app/Contents/MacOS/"); i >= 0 {
		return exe[:i+len(".app")]
	}
	return ""
}
