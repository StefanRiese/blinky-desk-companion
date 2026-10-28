package desktop

import "os/exec"

// Open shows a folder in Finder, a file in its default app (the log in Console or TextEdit)
// or a URL in the browser.
func Open(path string) error {
	return exec.Command("open", path).Run()
}
