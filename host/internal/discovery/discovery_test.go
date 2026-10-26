package discovery

import "testing"

// Announce/Find need multicast, which CI runners don't reliably have; the round trip is
// tested by hand (buddyd --tcp + buddysim --host auto).
func TestInstanceName(t *testing.T) {
	for in, want := range map[string]string{
		`buddyd@pve.` + Service + `.local.`:       "buddyd@pve",
		`buddyd@pve\.home.` + Service + `.local.`: "buddyd@pve.home",
		`buddyd@debian12.` + Service + `.local.`:  "buddyd@debian12",
	} {
		if got := instanceName(in); got != want {
			t.Errorf("instanceName(%q) = %q, want %q", in, got, want)
		}
	}
}
