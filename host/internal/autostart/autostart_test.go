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
