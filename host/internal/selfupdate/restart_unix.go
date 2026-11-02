//go:build !windows

// Restart on Unix: exec in the same process, so a service manager keeps it.
package selfupdate

import (
	"os"
	"syscall"
)

func restart(exe string, args []string) error {
	return syscall.Exec(exe, args, os.Environ())
}
