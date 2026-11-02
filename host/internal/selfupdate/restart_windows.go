// Restart on Windows: start the new program as its own process; the caller exits.
package selfupdate

import (
	"os"
	"os/exec"
)

func restart(exe string, args []string) error {
	cmd := exec.Command(exe, args[1:]...)
	cmd.Env = os.Environ()
	return cmd.Start()
}
