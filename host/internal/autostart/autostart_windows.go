// Start at login on Windows: a shortcut in the user's Startup folder, made by the user.
// buddy-link doesn't add itself: an unsigned program that writes itself into the Run list
// (what v0.3.0 did) is what antivirus behaviour monitors stop (Bitdefender removed it).
// Enable opens the Startup folder and the program's folder for the user to drag a shortcut
// over; Enabled looks for a shortcut to buddy-link there; Disable removes it and the old
// Run entry.
package autostart

import (
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"golang.org/x/sys/windows"
	"golang.org/x/sys/windows/registry"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/desktop"
)

const runKey = `Software\Microsoft\Windows\CurrentVersion\Run`

const instructions = "Two Explorer windows opened: the Startup folder and buddy-link.exe. Hold Alt and drag " +
	"buddy-link.exe into the Startup folder: that makes a shortcut, and Buddy Link starts when you log in. " +
	"(Buddy Link doesn't add itself there: antivirus programs stop programs that do.)"

func startupDir() (string, error) {
	return windows.KnownFolderPath(windows.FOLDERID_Startup, 0)
}

// Enable opens the Startup folder and selects buddy-link.exe in its folder, for the user to
// make the shortcut. It returns ErrManual: nothing is on until the shortcut is there.
func Enable() error {
	exe, err := executable()
	if err != nil {
		return err
	}
	dir, err := startupDir()
	if err != nil {
		return err
	}
	if err := desktop.Open(dir); err != nil {
		return err
	}
	_ = exec.Command("explorer", "/select,", exe).Start() // a second window with the program selected
	return ErrManual
}

// Disable removes the shortcuts to buddy-link in the Startup folder and the Run entry of
// v0.3.0.
func Disable() error {
	var errs []error
	if k, err := registry.OpenKey(registry.CURRENT_USER, runKey, registry.SET_VALUE); err == nil {
		if err := k.DeleteValue(Name); err != nil && !errors.Is(err, registry.ErrNotExist) {
			errs = append(errs, err)
		}
		_ = k.Close()
	}
	for _, p := range shortcuts() {
		if err := os.Remove(p); err != nil {
			errs = append(errs, err)
		}
	}
	return errors.Join(errs...)
}

// Enabled reports whether a shortcut to buddy-link is in the Startup folder (or the Run
// entry of v0.3.0 is still there).
func Enabled() bool {
	if k, err := registry.OpenKey(registry.CURRENT_USER, runKey, registry.QUERY_VALUE); err == nil {
		_, _, err = k.GetStringValue(Name)
		_ = k.Close()
		if err == nil {
			return true
		}
	}
	return len(shortcuts()) > 0
}

// shortcuts returns the .lnk files in the Startup folder that point to this program.
func shortcuts() []string {
	dir, err := startupDir()
	if err != nil {
		return nil
	}
	exe, err := executable()
	if err != nil {
		return nil
	}
	name := strings.TrimSuffix(filepath.Base(exe), filepath.Ext(exe))
	files, _ := filepath.Glob(filepath.Join(dir, "*.lnk"))
	var out []string
	for _, f := range files {
		if b, err := os.ReadFile(f); err == nil && pointsTo(b, name) {
			out = append(out, f)
		}
	}
	return out
}
