//go:build !windows

package tray

// HideOwnConsole does nothing outside Windows.
func HideOwnConsole() bool { return false }
