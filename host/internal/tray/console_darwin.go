package tray

import (
	"os"
	"strings"
)

// Detached reports whether buddyd was started without a terminal, so the log needs a file:
// on macOS when it runs from an app bundle (Buddy Link.app opened in Finder).
func Detached() bool {
	exe, err := os.Executable()
	return err == nil && strings.Contains(exe, ".app/Contents/MacOS/")
}

// AttachParentConsole is only needed on Windows.
func AttachParentConsole() {}
