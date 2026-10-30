//go:build windows || (darwin && cgo)

package tray

import (
	"context"
	"fmt"
	"log/slog"
	"os"
	"time"

	"fyne.io/systray"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/desktop"
)

// Available reports whether this build shows a tray icon.
const Available = true

func run(ctx context.Context, o Options) {
	go func() {
		<-ctx.Done()
		systray.Quit()
	}()
	systray.Run(func() {
		setIcon()
		systray.SetTooltip("Buddy Link – Blinky host")
		title := systray.AddMenuItem("Buddy Link "+o.Version, "")
		title.Disable()
		status := systray.AddMenuItem("", "")
		status.Disable()
		status.Hide()
		systray.AddSeparator()
		uiItem := systray.AddMenuItem("Open Buddy Link…", "Connected devices and their settings, in the browser")
		if o.UIURL == "" {
			uiItem.Disable()
		}
		updateItem := systray.AddMenuItem("Update firmware…", "Update the connected Blinkies to the latest release")
		if o.UpdateURL == "" {
			updateItem.Hide()
		}
		if o.Status != nil {
			status.Show()
			go showStatus(ctx, o, status, updateItem)
		}
		flashItem := systray.AddMenuItem("Flash new device…", "Install the Blinky firmware on a board on USB")
		if o.FlashURL == "" {
			flashItem.Hide()
		}
		systray.AddSeparator()
		configItem := systray.AddMenuItem("Open config folder", "config.yaml, control token and log")
		logItem := systray.AddMenuItem("Show log", "buddy-link.log")
		if o.LogFile == "" {
			logItem.SetTooltip("The log goes to the terminal Buddy Link was started from")
			logItem.Disable()
		}
		var startItem *systray.MenuItem
		clicks := make(<-chan struct{})
		if o.Autostart != nil {
			startItem = systray.AddMenuItemCheckbox("Start at login", "Start Buddy Link when you log in", o.Autostart.Get())
			clicks = startItem.ClickedCh
		}
		systray.AddSeparator()
		closeItem := systray.AddMenuItem("Close", "Stop Buddy Link")
		go func() {
			for {
				select {
				case <-uiItem.ClickedCh:
					logOpenErr("web UI", desktop.Open(o.UIURL))
				case <-updateItem.ClickedCh:
					logOpenErr("web UI", desktop.Open(o.UpdateURL))
				case <-flashItem.ClickedCh:
					logOpenErr("web UI", desktop.Open(o.FlashURL))
				case <-configItem.ClickedCh:
					err := os.MkdirAll(o.ConfigDir, 0o700)
					if err == nil {
						err = desktop.Open(o.ConfigDir)
					}
					logOpenErr("config folder", err)
				case <-logItem.ClickedCh:
					logOpenErr("log", desktop.Open(o.LogFile))
				case <-clicks:
					on := !startItem.Checked()
					if err := o.Autostart.Set(on); err != nil {
						slog.Warn("tray: start at login", "err", err)
					} else if on {
						startItem.Check()
					} else {
						startItem.Uncheck()
					}
				case <-closeItem.ClickedCh:
					if o.OnClose != nil {
						o.OnClose()
					}
					return
				case <-ctx.Done():
					return
				}
			}
		}()
	}, nil)
}

func logOpenErr(what string, err error) {
	if err != nil {
		slog.Warn("tray: cannot open "+what, "err", err)
	}
}

// showStatus refreshes the status line, the tooltip and the update count every few seconds.
func showStatus(ctx context.Context, o Options, item, updateItem *systray.MenuItem) {
	t := time.NewTicker(3 * time.Second)
	defer t.Stop()
	for {
		s := o.Status()
		n := 0
		if o.Updates != nil {
			n = o.Updates()
		}
		if n > 0 {
			s += fmt.Sprintf(" · %d firmware update%s waiting", n, map[bool]string{true: "s"}[n > 1])
			updateItem.SetTitle(fmt.Sprintf("Update firmware… (%d)", n))
		} else {
			updateItem.SetTitle("Update firmware…")
		}
		item.SetTitle(s)
		systray.SetTooltip("Buddy Link – " + s)
		select {
		case <-ctx.Done():
			return
		case <-t.C:
		}
	}
}
