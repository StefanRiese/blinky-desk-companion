package flasher

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"errors"
	"fmt"
	"testing"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/hub"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/jobs"
)

// otaDevice takes "ota" messages like the firmware: blocks in order, then the SHA-256.
type otaDevice struct {
	got     []byte
	sha     string
	next    int
	failAt  int
	aborted bool
}

func (d *otaDevice) ID() string       { return "desk" }
func (d *otaDevice) Info() hub.Info   { return hub.Info{Dev: "desk", Board: "ws146"} }
func (d *otaDevice) Send(v any) error { d.aborted = true; return nil }
func (d *otaDevice) Request(_ context.Context, m map[string]any) (map[string]any, error) {
	switch {
	case m["begin"] != nil:
		d.sha = m["begin"].(map[string]any)["sha256"].(string)
	case m["data"] != nil:
		if m["seq"].(int) != d.next || d.next == d.failAt {
			return map[string]any{"t": "err", "msg": "bad block"}, nil
		}
		b, _ := base64.StdEncoding.DecodeString(m["data"].(string))
		d.got = append(d.got, b...)
		d.next++
	case m["end"] == true:
		sum := sha256.Sum256(d.got)
		if hex.EncodeToString(sum[:]) != d.sha {
			return map[string]any{"t": "err", "msg": "SHA-256 differs"}, nil
		}
	default:
		return nil, errors.New("unexpected message")
	}
	return map[string]any{"t": "ack"}, nil
}

func TestSend(t *testing.T) {
	img := bytes.Repeat([]byte("firmware"), 1000) // 8000 bytes: 8 blocks
	js := &jobs.Jobs{}
	runs := 0
	run := func(dev *otaDevice) error {
		runs++
		done := make(chan error, 1)
		// A target per run: a finished job may still count as running for a moment.
		j, err := js.Start("update", fmt.Sprintf("desk%d", runs), func(r jobs.Report) error {
			err := Send(context.Background(), dev, img, "0.2.0", r)
			done <- err
			return err
		})
		if err != nil {
			t.Fatal(err)
		}
		err = <-done
		if got, _ := js.Get(j.ID); err == nil && got.Done != len(img) {
			t.Errorf("progress %d of %d", got.Done, len(img))
		}
		return err
	}
	dev := &otaDevice{failAt: -1}
	if err := run(dev); err != nil || !bytes.Equal(dev.got, img) {
		t.Fatalf("send: %v (%d bytes arrived)", err, len(dev.got))
	}
	dev = &otaDevice{failAt: 3}
	if err := run(dev); err == nil || !dev.aborted {
		t.Fatalf("a refused block must stop the update and abort it: %v", err)
	}
}
