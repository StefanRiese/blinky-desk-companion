package autostart

import (
	"strings"
	"testing"
)

func TestFiles(t *testing.T) {
	p := launchAgent([]string{"/usr/bin/open", "-a", "/Applications/Buddy Link.app"})
	for _, want := range []string{"<string>/Applications/Buddy Link.app</string>", "<key>RunAtLoad</key><true/>", "com.github.stefanriese.buddy-link"} {
		if !strings.Contains(p, want) {
			t.Errorf("plist lacks %q:\n%s", want, p)
		}
	}
	if u := userUnit("/opt/b l/buddy-link"); !strings.Contains(u, `ExecStart="/opt/b l/buddy-link" run`) {
		t.Errorf("unit:\n%s", u)
	}
	if appBundle("/Applications/Buddy Link.app/Contents/MacOS/buddy-link") != "/Applications/Buddy Link.app" || appBundle("/usr/local/bin/buddy-link") != "" {
		t.Error("appBundle")
	}
}

func TestPointsTo(t *testing.T) {
	ansi := []byte("L\x00\x00\x00...C:\\Tools\\Buddy-Link.exe\x00")
	wide := []byte{}
	for _, c := range `..\Tools\buddy-link.exe` {
		wide = append(wide, byte(c), 0)
	}
	if !pointsTo(ansi, "buddy-link") || !pointsTo(wide, "buddy-link") {
		t.Error("shortcut to buddy-link not found")
	}
	if pointsTo([]byte("C:\\Windows\\notepad.exe"), "buddy-link") {
		t.Error("other shortcut taken")
	}
}
