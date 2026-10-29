// Which firmware can be updated over the link: the "ota" capability, or a release from
// 0.2.0 on (0.2.0 takes updates but doesn't announce them yet).
package flasher

import (
	"fmt"
	"slices"
	"strconv"
	"strings"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/hub"
)

// CanUpdate reports whether info's firmware takes "ota" messages. Development builds and
// unknown versions count as yes; the update then fails with the device's own error.
func CanUpdate(info hub.Info) bool {
	if slices.Contains(info.Caps, "ota") {
		return true
	}
	major, minor, ok := releaseVersion(info.FW)
	return !ok || major > 0 || minor >= 2
}

// releaseVersion parses "X.Y.Z" (a release, no suffix) into X and Y.
func releaseVersion(v string) (major, minor int, ok bool) {
	parts := strings.Split(v, ".")
	if len(parts) != 3 {
		return 0, 0, false
	}
	var n [3]int
	for i, p := range parts {
		x, err := strconv.Atoi(p)
		if err != nil || x < 0 {
			return 0, 0, false
		}
		n[i] = x
	}
	return n[0], n[1], true
}

// errNoOTA explains why a device can't be updated over the link.
func errNoOTA(info hub.Info) error {
	return fmt.Errorf("firmware %s on %s can't be updated over the link; install the new firmware once over USB "+
		"(the setup page, or \"Flash a new device\"), after that updates work from here", info.FW, info.Dev)
}
