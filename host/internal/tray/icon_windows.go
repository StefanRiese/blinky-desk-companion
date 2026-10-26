package tray

import (
	_ "embed"

	"fyne.io/systray"
)

//go:embed buddyd.ico
var icon []byte

func setIcon() { systray.SetIcon(icon) }
