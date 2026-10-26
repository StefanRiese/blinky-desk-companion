package sim

import (
	"testing"
	"time"
)

func TestSoundReactions(t *testing.T) {
	d := newTestDevice(t)
	now := time.Now()
	feed := func(level float64, d0 time.Duration) (events int) {
		d.mu.Lock()
		defer d.mu.Unlock()
		for t := time.Duration(0); t < d0; t += soundTick {
			now = now.Add(soundTick)
			if ev, _ := d.soundSampleLocked(level, now); ev != nil {
				events++
			}
		}
		return events
	}
	mood := func() string { m, _ := d.moodLocked(now); return m }

	feed(38, 5*time.Second) // quiet room
	if n := feed(92, soundTick); n != 1 || mood() != "surprised" {
		t.Fatalf("clap: %d events, mood %q", n, mood())
	}
	if n := feed(92, soundTick); n != 0 {
		t.Fatal("second clap within the cooldown should not react")
	}
	feed(38, 4*time.Second)
	d.reaction = timedMood{}
	for range 5 { // voices: syllables with short pauses
		feed(54, 300*time.Millisecond)
		feed(40, 200*time.Millisecond)
	}
	if mood() != "curious" {
		t.Fatalf("voices: mood %q", mood())
	}
	d.reaction = timedMood{}
	feed(75, 90*time.Second) // a loud room for a while
	if m, reason := d.moodLocked(now); m != "angry" {
		t.Fatalf("lasting noise: mood %q (%s)", m, reason)
	}
	feed(38, 3*time.Minute)
	if loud, _ := d.tooLoudLocked(); loud {
		t.Fatal("still too loud after the room got quiet")
	}

	d.nvs.SetMany(map[string]any{"sound.enabled": false})
	d.reaction = timedMood{}
	if n := feed(95, soundTick); n != 0 || mood() == "surprised" {
		t.Fatal("reacted with sound.enabled=false")
	}
}
