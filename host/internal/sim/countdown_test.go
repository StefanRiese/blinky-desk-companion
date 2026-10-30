package sim

import (
	"path/filepath"
	"testing"
	"time"
)

func TestCountdown(t *testing.T) {
	nvs, err := LoadNVS(filepath.Join(t.TempDir(), "nvs.json"), "buddy-t")
	if err != nil {
		t.Fatal(err)
	}
	d := New(nvs, nil)
	in := func(action string, x float64) {
		t.Helper()
		if err := d.InputAt(action, x); err != nil {
			t.Fatal(err)
		}
	}
	view := func() CountdownView { return d.Snapshot().Countdown }

	// A shake never touches the countdown: only the eyes get dizzy.
	d.page = "countdown"
	in("swipe_up", .25) // 1:00
	in("shake", -1)
	if st := d.Snapshot(); st.Mood != "dizzy" || st.Countdown.Running {
		t.Fatalf("shake on the countdown page: mood %q, running %v", st.Mood, st.Countdown.Running)
	}
	d.reaction = timedMood{}
	d.cd = countdown{}

	// Left half sets minutes, right half seconds; both wrap.
	in("swipe_up", .25)
	in("swipe_up", .25)
	in("swipe_down", .75)
	if v := view(); v.LeftMS != (2*time.Minute+59*time.Second).Milliseconds() || v.SetMS != v.LeftMS {
		t.Fatalf("after setting: %+v", v)
	}
	in("swipe_down", .25)
	in("swipe_down", .25)
	in("swipe_down", .25)
	if v := view(); v.LeftMS != (99*time.Minute + 59*time.Second).Milliseconds() {
		t.Fatalf("minutes should wrap to 99: %+v", v)
	}

	// A tap starts and pauses; swipes are ignored while running.
	d.cd = countdown{}
	in("swipe_up", .25) // 1:00
	in("tap", -1)
	if v := view(); !v.Running {
		t.Fatalf("not running after a tap: %+v", v)
	}
	in("swipe_up", .25)
	if v := view(); v.SetMS != time.Minute.Milliseconds() {
		t.Fatalf("swipe changed a running countdown: %+v", v)
	}
	in("tap", -1)
	if v := view(); v.Running || v.LeftMS <= 0 || v.LeftMS > time.Minute.Milliseconds() {
		t.Fatalf("pause: %+v", v)
	}
	in("long_press", -1)
	if v := view(); v.LeftMS != time.Minute.Milliseconds() {
		t.Fatalf("long press should reset to the set time: %+v", v)
	}

	// At zero it rings, jumps to the page and looks surprised; a tap stops it.
	d.cd = countdown{}
	in("swipe_up", .75) // 0:01
	in("tap", -1)
	d.page = "eyes"
	deadline := time.Now().Add(3 * time.Second)
	for !view().Ringing {
		if time.Now().After(deadline) {
			t.Fatal("countdown did not ring")
		}
		time.Sleep(20 * time.Millisecond)
	}
	if st := d.Snapshot(); st.Page != "countdown" || st.Mood != "surprised" {
		t.Fatalf("ringing: page %q, mood %q", st.Page, st.Mood)
	}
	in("shake", -1) // a shake doesn't stop it
	if v := view(); !v.Ringing {
		t.Fatalf("a shake stopped the ringing countdown: %+v", v)
	}
	in("tap", -1)
	if v := view(); v.Ringing || v.LeftMS != time.Second.Milliseconds() {
		t.Fatalf("after stopping the alarm: %+v", v)
	}

	// Ringing stops by itself after ringFor.
	d.cd.ringing = time.Now().Add(-ringFor)
	d.mu.Lock()
	d.countdownTickLocked(time.Now())
	d.mu.Unlock()
	if view().Ringing {
		t.Fatal("still ringing after ringFor")
	}
}
