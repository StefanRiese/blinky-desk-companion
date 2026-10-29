package sim

import (
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"fmt"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/firmware"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/protocol"
)

// otaState is a firmware update in progress, as the firmware's OtaReceiver
// (firmware/components/app_core/src/ota.cpp) checks it: blocks in order, size, SHA-256.
// The emulator can't run the image; after end it takes the image's version as its fw and
// "restarts" (says hello again).
type otaState struct {
	active  bool
	size    int
	sha     string
	nextSeq int
	data    []byte
}

// handleOTA answers an "ota" message through reply.
func (d *Device) handleOTA(e protocol.Envelope, reply func(any)) {
	var m protocol.OTA
	if e.As(&m) != nil {
		return
	}
	fail := func(msg string) { reply(protocol.Err{T: "err", ID: m.ID, Code: "ota", Msg: msg}) }
	ok := func() {
		if m.ID != nil {
			reply(protocol.Ack{T: "ack", ID: *m.ID})
		}
	}
	d.mu.Lock()
	o := &d.ota
	switch {
	case m.Abort:
		*o = otaState{}
		d.mu.Unlock()
		ok()
	case m.Begin != nil:
		if m.Begin.Size <= 0 || len(m.Begin.SHA256) != 64 {
			d.mu.Unlock()
			fail("begin needs size and sha256")
			return
		}
		*o = otaState{active: true, size: m.Begin.Size, sha: m.Begin.SHA256}
		d.mu.Unlock()
		d.logf("sys", "update: %d bytes, version %s", m.Begin.Size, m.Begin.Version)
		ok()
	case !o.active:
		d.mu.Unlock()
		fail("no update in progress")
	case m.Data != "":
		b, err := base64.StdEncoding.DecodeString(m.Data)
		switch {
		case m.Seq == nil || *m.Seq != o.nextSeq:
			d.mu.Unlock()
			fail(fmt.Sprintf("expected block %d", o.nextSeq))
		case err != nil || len(b) == 0 || len(b) > protocol.OTABlock || len(o.data)+len(b) > o.size:
			d.mu.Unlock()
			fail("bad block")
		default:
			o.data = append(o.data, b...)
			o.nextSeq++
			d.mu.Unlock()
			ok()
		}
	case m.End:
		sum := sha256.Sum256(o.data)
		switch {
		case len(o.data) != o.size:
			d.mu.Unlock()
			fail(fmt.Sprintf("got %d of %d bytes", len(o.data), o.size))
		case hex.EncodeToString(sum[:]) != o.sha:
			*o = otaState{}
			d.mu.Unlock()
			fail("SHA-256 differs: the image was damaged on the way")
		default:
			info, err := firmware.Describe(o.data)
			*o = otaState{}
			if err != nil || info.Full {
				d.mu.Unlock()
				fail("the image is not valid (an app image is needed)")
				return
			}
			if err := d.nvs.Set("fw", info.Version); err == nil {
				err = d.nvs.Save()
			}
			d.mu.Unlock()
			if err != nil {
				fail("cannot store the new version: " + err.Error())
				return
			}
			ok()
			d.logf("sys", "update: restarting with fw %s", info.Version)
			time.AfterFunc(500*time.Millisecond, d.triggerRelink)
		}
	default:
		d.mu.Unlock()
		fail("unknown ota message")
	}
}
