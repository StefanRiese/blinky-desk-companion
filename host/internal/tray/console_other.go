//go:build !windows && !darwin

package tray

// Detached reports whether buddyd was started without a terminal; never on Linux.
func Detached() bool { return false }

// AttachParentConsole is only needed on Windows.
func AttachParentConsole() {}
