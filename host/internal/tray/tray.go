// Package tray shows buddyd ("Buddy Link") as an icon in the Windows notification area (lower right of the
// taskbar) or the macOS menu bar. Right click (Windows) or click (macOS) opens a menu with
// the version, "Open config folder", "Show log" and Close. Other platforms, and macOS builds without cgo, have no tray: Run
// then only waits for the context.
package tray

import "context"

// Options for Run.
type Options struct {
	Version   string
	ConfigDir string // opened by "Open config folder" (created if missing)
	LogFile   string // opened by "Show log"; empty when the log goes to a terminal
	OnClose   func() // Close was chosen in the menu (buddyd stops its context)
}

// Run shows the icon and blocks until ctx ends. Call it from the main goroutine (macOS
// draws the menu bar only from the main thread).
func Run(ctx context.Context, o Options) {
	run(ctx, o)
}
