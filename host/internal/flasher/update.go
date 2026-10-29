// Updates over the link: buddy-link sends a connected Blinky the app image of a release
// (or a file) as "ota" messages, over USB or TCP, and waits until it comes back with the
// new version.
package flasher

import (
	"context"
	"encoding/base64"
	"errors"
	"fmt"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/firmware"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/hub"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/jobs"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/protocol"
)

// UpdateRequest is what to send to which device. Without Image, the release Version
// ("" = latest) for the device's board is downloaded.
type UpdateRequest struct {
	Dev     string `json:"dev"`
	Version string `json:"version,omitempty"`
	Image   []byte `json:"-"`
}

// Updater starts update jobs.
type Updater struct {
	Hub    *hub.Hub
	Source *firmware.Source
	Jobs   *jobs.Jobs
	// BackWithin is how long to wait for the device to come back after the update.
	BackWithin time.Duration
}

// Start checks that the device is connected and runs the update as a job.
func (u *Updater) Start(req UpdateRequest) (jobs.Job, error) {
	devs := u.Hub.Devices(req.Dev)
	if len(devs) == 0 {
		return jobs.Job{}, fmt.Errorf("device %s not connected", req.Dev)
	}
	board := devs[0].Info().Board
	return u.Jobs.Start("update", req.Dev, func(r jobs.Report) error {
		ctx := context.Background()
		img := req.Image
		if img == nil {
			r.Step("looking for the firmware release")
			rels, err := u.Source.Releases(ctx)
			if err != nil {
				return err
			}
			rel, asset, err := firmware.Pick(rels, req.Version, firmware.AssetName(firmware.App, board))
			if err != nil {
				return err
			}
			r.Step(fmt.Sprintf("getting %s from release %s", asset.Name, rel.Tag))
			if img, err = u.Source.Image(ctx, rel, asset); err != nil {
				return err
			}
		}
		info, err := firmware.Describe(img)
		if err != nil {
			return err
		}
		if info.Full {
			return errors.New("this is a full flash image; an update needs the app image, blinky-app-<board>.bin")
		}
		devs := u.Hub.Devices(req.Dev)
		if len(devs) == 0 {
			return fmt.Errorf("device %s went away", req.Dev)
		}
		started := time.Now()
		if err := Send(ctx, devs[0], img, info.Version, r); err != nil {
			return err
		}
		r.Step("the Blinky restarts with the new firmware")
		return u.waitBack(ctx, req.Dev, info.Version, started)
	})
}

// Send sends img to dev as "ota" messages and returns when the device has checked it.
func Send(ctx context.Context, dev hub.Device, img []byte, version string, r jobs.Report) error {
	ask := func(msg map[string]any, timeout time.Duration) error {
		ctx, cancel := context.WithTimeout(ctx, timeout)
		defer cancel()
		reply, err := dev.Request(ctx, msg)
		if err != nil {
			return err
		}
		if reply["t"] == "err" {
			return fmt.Errorf("the device refused the update: %v", reply["msg"])
		}
		return nil
	}
	r.Step(fmt.Sprintf("sending firmware %s (%d KiB)", version, len(img)>>10))
	begin := map[string]any{"size": len(img), "sha256": firmware.SHA256(img), "version": version}
	if err := ask(map[string]any{"t": "ota", "begin": begin}, 30*time.Second); err != nil {
		return err
	}
	for seq, off := 0, 0; off < len(img); seq, off = seq+1, off+protocol.OTABlock {
		block := img[off:min(off+protocol.OTABlock, len(img))]
		msg := map[string]any{"t": "ota", "seq": seq, "data": base64.StdEncoding.EncodeToString(block)}
		if err := ask(msg, 10*time.Second); err != nil {
			dev.Send(protocol.OTA{T: "ota", Abort: true})
			return fmt.Errorf("block %d: %w", seq, err)
		}
		r.Progress(off+len(block), len(img))
	}
	r.Step("the device checks the image")
	return ask(map[string]any{"t": "ota", "end": true}, 30*time.Second)
}

// waitBack waits until dev is connected again with firmware version (any new connection
// if version is unknown).
func (u *Updater) waitBack(ctx context.Context, dev, version string, since time.Time) error {
	within := u.BackWithin
	if within == 0 {
		within = 2 * time.Minute
	}
	deadline := time.Now().Add(within)
	for time.Now().Before(deadline) {
		for _, d := range u.Hub.Devices(dev) {
			info := d.Info()
			if info.ConnectedAt.After(since) && (version == "" || info.FW == version) {
				return nil
			}
			if info.ConnectedAt.After(since) && info.FW != version {
				return fmt.Errorf("the Blinky came back with firmware %s, not %s: the new one didn't start, so it went back", info.FW, version)
			}
		}
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-time.After(time.Second):
		}
	}
	return fmt.Errorf("the Blinky didn't come back within %s; check it", within)
}
