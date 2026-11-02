// Package selfupdate replaces the running buddy-link with the one from a GitHub release
// (the web UI's "Update Buddy Link"). The release files are buddy-link-<os>-<arch>[.exe]
// (.github/workflows/release.yml); the download is checked against the SHA-256 GitHub
// lists and the new program must answer "version" before it replaces the old one.
// Then buddy-link stops its links and starts again as the new program: on Linux in the same
// process (exec, so systemd keeps it), on Windows as a new process.
// Not offered for Buddy Link.app on macOS (an app bundle), in a container (update the image)
// or when the program's folder isn't writable.
package selfupdate

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"
)

// AssetName returns the release file for goos/goarch ("" if releases have none).
func AssetName(goos, goarch string) string {
	switch goos {
	case "linux":
		return "buddy-link-linux-" + goarch
	case "windows":
		return "buddy-link-windows-" + goarch + ".exe"
	}
	return ""
}

// Updater replaces one program file.
type Updater struct {
	Exe string // the running program, symlinks resolved; "" = os.Executable
}

// exe returns the program's path.
func (u *Updater) exe() (string, error) {
	if u.Exe != "" {
		return u.Exe, nil
	}
	exe, err := os.Executable()
	if err != nil {
		return "", err
	}
	if p, err := filepath.EvalSymlinks(exe); err == nil {
		exe = p
	}
	return exe, nil
}

// Check returns why the running buddy-link can't update itself ("" = it can).
func (u *Updater) Check() string {
	if AssetName(runtime.GOOS, runtime.GOARCH) == "" {
		if runtime.GOOS == "darwin" {
			return "Buddy Link.app updates from the release page: download the new app and replace the old one."
		}
		return "The releases have no buddy-link for " + runtime.GOOS + "."
	}
	for _, f := range []string{"/.dockerenv", "/run/.containerenv"} {
		if _, err := os.Stat(f); err == nil {
			return "Buddy Link runs in a container: update its image instead."
		}
	}
	exe, err := u.exe()
	if err != nil {
		return "Cannot find the program file: " + err.Error()
	}
	f, err := os.CreateTemp(filepath.Dir(exe), ".buddy-link-check-*")
	if err != nil {
		return "Buddy Link may not write to its folder " + filepath.Dir(exe) + ": update it the way it was installed."
	}
	f.Close()
	_ = os.Remove(f.Name())
	return ""
}

// Install checks the new program bin and puts it in the place of the running one. want is
// the version it must report ("" = any). The old file stays as <exe>.old until the next
// start (Windows can't delete a running program, but can rename it).
func (u *Updater) Install(ctx context.Context, bin []byte, want string) error {
	exe, err := u.exe()
	if err != nil {
		return err
	}
	next := exe + ".new"
	if runtime.GOOS == "windows" {
		next = strings.TrimSuffix(exe, ".exe") + ".new.exe" // Windows runs only .exe files
	}
	if err := os.WriteFile(next, bin, 0o755); err != nil {
		return err
	}
	if err := checkProgram(ctx, next, want); err != nil {
		_ = os.Remove(next)
		return err
	}
	old := exe + ".old"
	_ = os.Remove(old) // left from an earlier update
	if runtime.GOOS == "windows" {
		if err := os.Rename(exe, old); err != nil {
			_ = os.Remove(next)
			return fmt.Errorf("move the old program aside: %w", err)
		}
		if err := os.Rename(next, exe); err != nil {
			_ = os.Rename(old, exe) // put the old one back
			return fmt.Errorf("put the new program in place: %w", err)
		}
		return nil
	}
	// Unix: keep a copy of the old one as .old (a hard link), then replace it in one step.
	_ = os.Link(exe, old)
	if err := os.Rename(next, exe); err != nil {
		_ = os.Remove(next)
		return fmt.Errorf("put the new program in place: %w", err)
	}
	return nil
}

// checkProgram runs "path version" and expects "buddy-link <want>".
func checkProgram(ctx context.Context, path, want string) error {
	ctx, cancel := context.WithTimeout(ctx, 15*time.Second)
	defer cancel()
	out, err := exec.CommandContext(ctx, path, "version").Output()
	if err != nil {
		return fmt.Errorf("the new program doesn't run here: %w", err)
	}
	got := strings.TrimSpace(string(bytes.TrimPrefix(bytes.TrimSpace(out), []byte("buddy-link"))))
	if want != "" && got != want {
		return fmt.Errorf("the new program is version %q, not %q", got, want)
	}
	return nil
}

// Cleanup removes the old program an update left (<exe>.old). On Windows the old process
// may still be ending, so it tries for a few seconds.
func (u *Updater) Cleanup() {
	exe, err := u.exe()
	if err != nil {
		return
	}
	old := exe + ".old"
	for range 10 {
		if err := os.Remove(old); err == nil || errors.Is(err, os.ErrNotExist) {
			return
		}
		time.Sleep(time.Second)
	}
}

// Restart starts the program file again with the same arguments, after the caller stopped
// everything it listens on. On Unix it replaces this process and only returns on an error;
// on Windows it starts a new process and returns, and the caller exits.
func (u *Updater) Restart() error {
	exe, err := u.exe()
	if err != nil {
		return err
	}
	return restart(exe, os.Args)
}
