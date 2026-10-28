package tray

import (
	_ "embed"

	"fyne.io/systray"
)

//go:embed buddy-link.ico
var icon []byte

func setIcon() { systray.SetIcon(icon) }
