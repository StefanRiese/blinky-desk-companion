//go:build !windows

// Tests for putting a new program in place (Unix; a shell script stands in for buddy-link).
package selfupdate

import (
	"context"
	"os"
	"path/filepath"
	"testing"
)

func script(version string) []byte {
	return []byte("#!/bin/sh\necho buddy-link " + version + "\n")
}

func TestInstall(t *testing.T) {
	dir := t.TempDir()
	exe := filepath.Join(dir, "buddy-link")
	if err := os.WriteFile(exe, script("0.2.0"), 0o755); err != nil {
		t.Fatal(err)
	}
	u := &Updater{Exe: exe}
	if r := u.Check(); r != "" && !fileExists("/.dockerenv") && !fileExists("/run/.containerenv") {
		t.Fatalf("check: %s", r)
	}

	// The wrong version is refused; the old program stays.
	if err := u.Install(context.Background(), script("0.2.9"), "0.3.0"); err == nil {
		t.Fatal("accepted the wrong version")
	}
	if b, _ := os.ReadFile(exe); string(b) != string(script("0.2.0")) {
		t.Fatal("old program changed")
	}
	if fileExists(exe + ".new") {
		t.Fatal(".new left behind")
	}

	if err := u.Install(context.Background(), script("0.3.0"), "0.3.0"); err != nil {
		t.Fatal(err)
	}
	if b, _ := os.ReadFile(exe); string(b) != string(script("0.3.0")) {
		t.Fatal("new program not in place")
	}
	if b, _ := os.ReadFile(exe + ".old"); string(b) != string(script("0.2.0")) {
		t.Fatal("no copy of the old program")
	}
	u.Cleanup()
	if fileExists(exe + ".old") {
		t.Fatal(".old not removed")
	}

	// Something that isn't a program for this system.
	if err := u.Install(context.Background(), []byte("MZ not a program"), ""); err == nil {
		t.Fatal("accepted a file that doesn't run")
	}
}

func TestAssetName(t *testing.T) {
	for _, c := range [][3]string{
		{"linux", "arm64", "buddy-link-linux-arm64"},
		{"windows", "amd64", "buddy-link-windows-amd64.exe"},
		{"darwin", "arm64", ""},
	} {
		if got := AssetName(c[0], c[1]); got != c[2] {
			t.Errorf("%s/%s: %q", c[0], c[1], got)
		}
	}
}

func fileExists(p string) bool {
	_, err := os.Stat(p)
	return err == nil
}
