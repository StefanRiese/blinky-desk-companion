//go:build windows || (darwin && cgo)

package tray

import (
	"context"
	"log/slog"
	"os"

	"fyne.io/systray"
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
		systray.AddSeparator()
		configItem := systray.AddMenuItem("Open config folder", "config.yaml, control token and log")
		logItem := systray.AddMenuItem("Show log", "buddyd.log")
		if o.LogFile == "" {
			logItem.SetTooltip("The log goes to the terminal Buddy Link was started from")
			logItem.Disable()
		}
		systray.AddSeparator()
		closeItem := systray.AddMenuItem("Close", "Stop Buddy Link")
		go func() {
			for {
				select {
				case <-configItem.ClickedCh:
					err := os.MkdirAll(o.ConfigDir, 0o700)
					if err == nil {
						err = open(o.ConfigDir)
					}
					logOpenErr("config folder", err)
				case <-logItem.ClickedCh:
					logOpenErr("log", open(o.LogFile))
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
