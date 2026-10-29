package fetch

import "os/exec"

// readSystemProxy reads the proxy of the macOS network settings.
var readSystemProxy = func() (sysProxy, error) {
	out, err := exec.Command("scutil", "--proxy").Output()
	if err != nil {
		return sysProxy{}, err
	}
	return parseScutil(string(out)), nil
}
