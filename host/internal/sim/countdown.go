package sim

import (
	"time"

	"github.com/StefanRiese/esp32_desk_buddy/host/internal/protocol"
)

// countdown is the countdown page: swipe up/down on the minutes or seconds to set it,
// shake to start, pause and resume. At zero it rings until shaken, or for ringFor.
type countdown struct {
	set     time.Duration // last value set by swiping; restored after ringing or reset
	left    time.Duration // remaining time while not running
	endAt   time.Time     // end while running
	running bool
	ringing time.Time // when it started ringing; zero when quiet
	timer   *time.Timer
}

const (
	countdownMaxMin = 99
	ringFor         = time.Minute
)

// CountdownView is the countdown state for the web UI.
type CountdownView struct {
	SetMS   int64 `json:"set_ms"`
	LeftMS  int64 `json:"left_ms"`
	Running bool  `json:"running"`
	Ringing bool  `json:"ringing"`
}

func (c *countdown) view(now time.Time) CountdownView {
	left := c.left
	if c.running {
		left = max(c.endAt.Sub(now), 0)
	}
	return CountdownView{SetMS: c.set.Milliseconds(), LeftMS: left.Milliseconds(),
		Running: c.running, Ringing: !c.ringing.IsZero()}
}

// ceilSec rounds up to whole seconds, as the display shows it.
func ceilSec(d time.Duration) time.Duration { return (d + time.Second - 1).Truncate(time.Second) }

// countdownInputLocked handles a gesture on the countdown page. x is where a swipe
// started (0..1 across the screen, < 0 if unknown): the left half sets minutes, the right
// half seconds. It returns the countdown event state to report ("" for none) and whether
// the gesture was used.
func (d *Device) countdownInputLocked(action string, x float64, now time.Time) (string, bool) {
	c := &d.cd
	switch action {
	case "swipe_up", "swipe_down":
		if c.running || !c.ringing.IsZero() {
			return "", true
		}
		step := 1
		if action == "swipe_down" {
			step = -1
		}
		cur := ceilSec(c.left)
		m, s := int(cur/time.Minute), int(cur%time.Minute/time.Second)
		if x >= 0.5 {
			s = (s + step + 60) % 60
		} else {
			m = (m + step + countdownMaxMin + 1) % (countdownMaxMin + 1)
		}
		c.left = time.Duration(m)*time.Minute + time.Duration(s)*time.Second
		c.set = c.left
		return "", true
	case "shake":
		switch {
		case !c.ringing.IsZero():
			c.ringing, c.left = time.Time{}, c.set
			return "stopped", true
		case c.running:
			c.timer.Stop()
			c.running, c.left = false, max(c.endAt.Sub(now), 0)
			return "paused", true
		case c.left > 0:
			c.running, c.endAt = true, now.Add(c.left)
			c.timer = time.AfterFunc(c.left, d.countdownDone)
			return "started", true
		}
		return "", false // nothing set: a normal shake
	case "long_press":
		if c.running {
			c.timer.Stop()
		}
		c.running, c.ringing, c.left = false, time.Time{}, c.set
		return "reset", true
	}
	return "", false
}

// countdownDone runs when the countdown reaches zero: ring and show the page.
func (d *Device) countdownDone() {
	now := time.Now()
	d.mu.Lock()
	c := &d.cd
	if !c.running || now.Before(c.endAt) { // paused or restarted meanwhile
		d.mu.Unlock()
		return
	}
	c.running, c.left, c.ringing = false, 0, now
	d.lastActive = now
	events := []protocol.Event{{T: "event", Kind: "countdown", State: "done"}}
	if d.page != "countdown" {
		d.page = "countdown"
		events = append(events, protocol.Event{T: "event", Kind: "page", Name: "countdown"})
	}
	ready := d.linkState == linkReady
	d.mu.Unlock()
	if ready {
		for _, e := range events {
			d.send(e)
		}
	}
	d.notifyChange()
}

// countdownTickLocked stops ringing after ringFor.
func (d *Device) countdownTickLocked(now time.Time) {
	if c := &d.cd; !c.ringing.IsZero() && now.Sub(c.ringing) >= ringFor {
		c.ringing, c.left = time.Time{}, c.set
	}
}
