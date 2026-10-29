// Package tray shows buddy-link ("Buddy Link") as an icon in the Windows notification area (lower right of the
// taskbar) or the macOS menu bar. Right click (Windows) or click (macOS) opens a menu with
// the version, "Open Buddy Link…" (the web UI), "Open config folder", "Show log" and Close.
// Other platforms, and macOS builds without cgo, have no tray: Run then only waits for the
// context.
package tray

import "context"

// Options for Run.
type Options struct {
	Version   string
	UIURL     string        // the web UI's login link, opened by "Open Buddy Link…"
	Status    func() string // a status line ("1 Blinky connected"), shown in the menu and tooltip
	Autostart *Toggle       // "Start at login"; nil = no such item
	ConfigDir string        // opened by "Open config folder" (created if missing)
	LogFile   string        // opened by "Show log"; empty when the log goes to a terminal
	OnClose   func()        // Close was chosen in the menu (buddy-link stops its context)
}

// Run shows the icon and blocks until ctx ends. Call it from the main goroutine (macOS
// draws the menu bar only from the main thread).
func Run(ctx context.Context, o Options) {
	run(ctx, o)
}

// Toggle is a menu item with a check mark.
type Toggle struct {
	Get func() bool
	Set func(on bool) error
}
