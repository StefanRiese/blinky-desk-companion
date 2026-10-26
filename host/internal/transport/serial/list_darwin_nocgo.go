//go:build darwin && !cgo

package serial

import "path/filepath"

// Without cgo there is no IOKit access, so VID/PID are unknown. Match the device
// node names macOS uses for USB-CDC (ESP32-S3), CH340/CH9102 and CP210x adapters.
func listUSBPorts() ([]usbPort, error) {
	var out []usbPort
	for _, pattern := range []string{"/dev/cu.usbmodem*", "/dev/cu.wchusbserial*", "/dev/cu.usbserial*", "/dev/cu.SLAB_USBtoUART*"} {
		names, _ := filepath.Glob(pattern)
		for _, n := range names {
			out = append(out, usbPort{Name: n})
		}
	}
	return out, nil
}
