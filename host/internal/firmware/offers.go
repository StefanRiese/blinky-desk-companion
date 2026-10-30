// Which firmware a device could be updated to: the newest release, a newer pre-release and
// the latest build from main, each compared with the version the device runs. buddy-link
// shows them in the web UI and counts them in the tray.
package firmware

import (
	"regexp"
	"strconv"
	"strings"
)

// Offers are the updates for one device. Versions without the leading "v".
type Offers struct {
	Release   string `json:"release,omitempty"`    // the newest release, if it is newer than the device
	Pre       string `json:"pre,omitempty"`        // a pre-release newer than the device and the newest release
	Main      string `json:"main,omitempty"`       // the latest build from main, if the device doesn't run it
	MainNewer bool   `json:"main_newer,omitempty"` // … and it is newer than the device's firmware
	OnMain    bool   `json:"on_main,omitempty"`    // the device runs the latest build from main
	Waiting   bool   `json:"waiting,omitempty"`    // an update is waiting (counted in the tray and the banner)
}

// buildName finds the version in the name of the build from main: "Latest build from main (0.2.1-dev.27+802b7bc)".
var buildName = regexp.MustCompile(`\(([^)]+)\)`)

// BuildVersion returns the firmware version of the build from main, from its name ("" if unknown).
func (r Release) BuildVersion() string {
	if m := buildName.FindStringSubmatch(r.Name); m != nil {
		return m[1]
	}
	return ""
}

// OffersFor compares the releases that have an app image for board with fw, the device's
// firmware. A newer release or pre-release is waiting; the build from main only for a device
// that already runs builds from main (a "-dev." version), so release users aren't nagged.
func OffersFor(rels []Release, board, fw string) Offers {
	var o Offers
	if fw == "" {
		return o
	}
	name := AssetName(App, board)
	var newest string // the newest regular release
	for _, r := range rels {
		if _, ok := r.Asset(name); !ok {
			continue
		}
		switch {
		case r.Rolling():
			v := r.BuildVersion()
			if v == "" || v == fw {
				o.OnMain = v != ""
				continue
			}
			o.Main, o.MainNewer = v, Newer(v, fw)
		case !r.Prerelease:
			if newest == "" || Newer(r.Version(), newest) {
				newest = r.Version()
			}
		}
	}
	if newest != "" && Newer(newest, fw) {
		o.Release = newest
	}
	for _, r := range rels {
		if _, ok := r.Asset(name); ok && r.Prerelease && !r.Rolling() && Newer(r.Version(), fw) &&
			(newest == "" || Newer(r.Version(), newest)) && (o.Pre == "" || Newer(r.Version(), o.Pre)) {
			o.Pre = r.Version()
		}
	}
	o.Waiting = o.Release != "" || o.Pre != "" || (o.MainNewer && strings.Contains(fw, "-dev."))
	return o
}

// Newer reports whether version a is newer than b, by semantic versioning: "0.2.0" >
// "0.2.0-rc1" > "0.1.3", "0.2.1-dev.27" > "0.2.1-dev.9"; "+build" is ignored. A version
// that doesn't parse (a bare commit) is never newer, and anything is newer than it.
func Newer(a, b string) bool {
	x, okA := parseVersion(a)
	y, okB := parseVersion(b)
	switch {
	case !okA:
		return false
	case !okB:
		return true
	}
	for i := range 3 {
		if x.nums[i] != y.nums[i] {
			return x.nums[i] > y.nums[i]
		}
	}
	return comparePre(x.pre, y.pre) > 0
}

type version struct {
	nums [3]int
	pre  []string // dot-separated pre-release identifiers; none for a release
}

func parseVersion(v string) (version, bool) {
	v, _, _ = strings.Cut(strings.TrimPrefix(v, "v"), "+")
	core, pre, hasPre := strings.Cut(v, "-")
	parts := strings.Split(core, ".")
	var out version
	if len(parts) != 3 {
		return out, false
	}
	for i, p := range parts {
		n, err := strconv.Atoi(p)
		if err != nil || n < 0 {
			return out, false
		}
		out.nums[i] = n
	}
	if hasPre {
		out.pre = strings.Split(pre, ".")
	}
	return out, true
}

// comparePre orders pre-release identifiers: none (a release) > any; numbers numerically,
// below text; a longer list wins a tie.
func comparePre(a, b []string) int {
	switch {
	case len(a) == 0 && len(b) == 0:
		return 0
	case len(a) == 0:
		return 1
	case len(b) == 0:
		return -1
	}
	for i := 0; i < len(a) && i < len(b); i++ {
		na, errA := strconv.Atoi(a[i])
		nb, errB := strconv.Atoi(b[i])
		switch {
		case errA == nil && errB == nil && na != nb:
			if na > nb {
				return 1
			}
			return -1
		case errA == nil && errB != nil:
			return -1
		case errA != nil && errB == nil:
			return 1
		case errA != nil && errB != nil && a[i] != b[i]:
			if a[i] > b[i] {
				return 1
			}
			return -1
		}
	}
	return len(a) - len(b)
}
