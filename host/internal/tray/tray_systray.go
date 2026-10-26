//go:build windows || (darwin && cgo)

package tray

import (
	"context"

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
		systray.SetTooltip("Buddy Link – Desk Buddy host")
		title := systray.AddMenuItem("Buddy Link "+o.Version, "")
		title.Disable()
		systray.AddSeparator()
		closeItem := systray.AddMenuItem("Close", "Stop Buddy Link")
		go func() {
			select {
			case <-closeItem.ClickedCh:
				if o.OnClose != nil {
					o.OnClose()
				}
			case <-ctx.Done():
			}
		}()
	}, nil)
}
