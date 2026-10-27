package tray

import "os/exec"

// open shows a folder in Finder or a file in its default app (the log in Console or TextEdit).
func open(path string) error {
	return exec.Command("open", path).Run()
}
