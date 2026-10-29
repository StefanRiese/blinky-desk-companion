//go:build !windows && !darwin

package desktop

import "os/exec"

// Open shows a folder, file or URL with xdg-open (Linux and BSD desktops). It does not wait
// for the program, since some browsers keep running in the foreground.
func Open(path string) error {
	cmd := exec.Command("xdg-open", path)
	if err := cmd.Start(); err != nil {
		return err
	}
	go func() { _ = cmd.Wait() }() // only reaps the process
	return nil
}
