//go:build cgo

package tray

import (
	_ "embed"

	"fyne.io/systray"
)

// A template image: macOS tints it to match light and dark menu bars.
//
//go:embed menubar.png
var menubar []byte

func setIcon() { systray.SetTemplateIcon(menubar, menubar) }
