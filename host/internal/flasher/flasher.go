// Package flasher writes the Blinky firmware to a board on USB, for buddy-link (as a job
// the web UI and buddy-ctl follow) and for buddy-ctl --direct. It takes the port away from
// the serial transport while it flashes, so a running Blinky on it lets go first.
package flasher

import (
	"context"
	"errors"
	"fmt"
	"slices"
	"strings"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/espflash"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/firmware"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/jobs"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/transport/serial"
)

// Request is what to flash where. Without Image, the release Version ("" = latest) for
// Board is downloaded. The settings on the board stay unless EraseSettings is set.
type Request struct {
	Port          string `json:"port"`
	Board         string `json:"board"`
	Version       string `json:"version,omitempty"`
	EraseSettings bool   `json:"erase_settings,omitempty"`
	Image         []byte `json:"-"`
}

// Flasher starts flash jobs.
type Flasher struct {
	Source *firmware.Source
	Jobs   *jobs.Jobs
}

// Start checks the request and runs the flash as a job.
func (f *Flasher) Start(req Request) (jobs.Job, error) {
	if !slices.Contains(firmware.Boards, req.Board) {
		return jobs.Job{}, fmt.Errorf("unknown board %q (%s)", req.Board, strings.Join(firmware.Boards, ", "))
	}
	if req.Port == "" {
		return jobs.Job{}, errors.New("port is required")
	}
	return f.Jobs.Start("flash", req.Port, func(r jobs.Report) error {
		img, version, err := Image(context.Background(), f.Source, req, r.Step)
		if err != nil {
			return err
		}
		if err := FlashPort(req.Port, img, !req.EraseSettings, r.Progress, r.Step); err != nil {
			return err
		}
		r.Step("done: the Blinky starts with firmware " + version)
		return nil
	})
}

// Image returns the image to flash (the given file, or the release downloaded) and its
// version. Only a full image (bootloader, partition table, app) can be flashed at 0.
func Image(ctx context.Context, src *firmware.Source, req Request, step func(string)) ([]byte, string, error) {
	img := req.Image
	if img == nil {
		step("looking for the firmware release")
		rels, err := src.Releases(ctx)
		if err != nil {
			return nil, "", err
		}
		rel, asset, err := firmware.Pick(rels, req.Version, firmware.AssetName(firmware.Full, req.Board))
		if err != nil {
			return nil, "", err
		}
		step(fmt.Sprintf("getting %s from release %s", asset.Name, rel.Tag))
		if img, err = src.Image(ctx, rel, asset); err != nil {
			return nil, "", err
		}
	}
	info, err := firmware.Describe(img)
	if err != nil {
		return nil, "", err
	}
	if !info.Full {
		return nil, "", errors.New("this is an app image (for updates); a first flash needs the full image, blinky-firmware-<board>.bin")
	}
	return img, info.Version, nil
}

// FlashPort writes img to the board on port name. With keepSettings the settings
// partitions (nvs, nvs_keys) are left as they are, so WiFi, rotation and the rest survive.
// buddy-link's serial transport lets go of the port meanwhile and takes it up again afterwards.
func FlashPort(name string, img []byte, keepSettings bool, progress func(done, total int), step func(string)) error {
	release := serial.Hold(name)
	defer release()
	p, err := serial.Open(name, 115200)
	if err != nil {
		return fmt.Errorf("open %s: %w", name, err)
	}
	defer p.Close()
	step("flashing on " + name)
	o := espflash.Options{USBJTAG: usbJTAG(name), Progress: progress, Log: step}
	if keepSettings {
		o.Skip = espflash.SettingsRanges(img)
	}
	return espflash.Flash(p, img, o)
}

// usbJTAG reports whether the port is the ESP32-S3's own USB (Espressif's VID 303a; on
// macOS without USB ids, a usbmodem port) rather than a USB-UART bridge.
func usbJTAG(name string) bool {
	if ports, err := serial.List(); err == nil {
		for _, p := range ports {
			if p.Name == name && p.VID != "" {
				return p.VID == "303a"
			}
		}
	}
	return !strings.Contains(name, "usbserial") && !strings.Contains(name, "ttyUSB") && !strings.Contains(name, "wchusbserial")
}
