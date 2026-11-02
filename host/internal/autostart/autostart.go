// Package autostart starts buddy-link when the user logs in: a shortcut in the Startup
// folder on Windows (made by the user, see autostart_windows.go), a LaunchAgent on macOS, a
// systemd user unit on Linux. It runs as the user, not as a system service, so the tray
// icon shows and the user's proxy settings apply.
package autostart

import (
	"bytes"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"unicode/utf16"
)

// ErrManual is what Enable returns where the user makes the entry (Windows): it opened
// what they need; Instructions says what to do.
var ErrManual = errors.New("start at login: make the shortcut yourself")

// Instructions tells the user what to do after Enable returned ErrManual ("" = nothing).
func Instructions() string { return instructions }

// pointsTo reports whether the shortcut file lnk names the program name (without ".exe",
// any case). A .lnk holds the target path in ANSI and often again in UTF-16.
func pointsTo(lnk []byte, name string) bool {
	low := bytes.ToLower(lnk)
	n := strings.ToLower(name)
	if bytes.Contains(low, []byte(n)) {
		return true
	}
	var wide []byte
	for _, r := range utf16.Encode([]rune(n)) {
		wide = append(wide, byte(r), byte(r>>8))
	}
	return bytes.Contains(low, wide)
}

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
