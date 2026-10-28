package discovery

import "testing"

// Announce/Find need multicast, which CI runners don't reliably have; the round trip is
// tested by hand (buddy-link --tcp + buddysim --host auto).
func TestInstanceName(t *testing.T) {
	for in, want := range map[string]string{
		`buddy-link@pve.` + Service + `.local.`:       "buddy-link@pve",
		`buddy-link@pve\.home.` + Service + `.local.`: "buddy-link@pve.home",
		`buddy-link@debian12.` + Service + `.local.`:  "buddy-link@debian12",
	} {
		if got := instanceName(in); got != want {
			t.Errorf("instanceName(%q) = %q, want %q", in, got, want)
		}
	}
}
