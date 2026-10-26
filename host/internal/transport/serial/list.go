//go:build !darwin || cgo

package serial

import "go.bug.st/serial/enumerator"

func listUSBPorts() ([]usbPort, error) {
	ports, err := enumerator.GetDetailedPortsList()
	if err != nil {
		return nil, err
	}
	var out []usbPort
	for _, p := range ports {
		if p.IsUSB {
			out = append(out, usbPort{Name: p.Name, VID: p.VID, PID: p.PID})
		}
	}
	return out, nil
}
