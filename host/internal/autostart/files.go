package autostart

import (
	"fmt"
	"html"
	"strings"
)

// launchAgent is the macOS LaunchAgent plist that runs args at login.
func launchAgent(args []string) string {
	var b strings.Builder
	for _, a := range args {
		fmt.Fprintf(&b, "    <string>%s</string>\n", html.EscapeString(a))
	}
	return `<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>com.github.stefanriese.buddy-link</string>
  <key>ProgramArguments</key>
  <array>
` + b.String() + `  </array>
  <key>RunAtLoad</key><true/>
</dict>
</plist>
`
}

// userUnit is the systemd user unit that runs exe in the user's session.
func userUnit(exe string) string {
	return `[Unit]
Description=Buddy Link, the Blinky host service
After=network-online.target

[Service]
ExecStart="` + exe + `" run
Restart=on-failure

[Install]
WantedBy=default.target
`
}
