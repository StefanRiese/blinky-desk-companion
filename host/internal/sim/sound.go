package sim

import (
	"context"
	"math"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/protocol"
)

// The sound monitor emulates the firmware's microphone task: it samples the sound
// level (in dB) 10 times a second. Only levels are used; no audio is kept or sent.
//
//   - a sudden loud noise (clap, door) → startled ("surprised") for a moment
//   - voices (a few dB above the room for a while) → "curious", listening
//   - lasting noise (30 s average above sound.too_loud) → "angry" mood
//
// In the emulator the level comes from the browser microphone, or is synthesized
// from a room level plus clap/talk bursts.
type soundState struct {
	ambient    float64   // emulated room level (dB)
	live       float64   // last level from the browser microphone
	liveAt     time.Time // when it arrived
	burst      string    // "clap" | "talk"
	burstUntil time.Time
	level      float64   // current sample
	base       float64   // slow baseline of the room
	avg        float64   // 30 s average, for "too loud"
	voiceSince time.Time // start of the current stretch of voices
	voiceLast  time.Time // last sample above the voice level
	lastReact  time.Time
}

const (
	soundTick     = 100 * time.Millisecond
	voiceAbove    = 10 // dB above the baseline counts as voices
	voiceFor      = 800 * time.Millisecond
	voiceGap      = 400 * time.Millisecond // pauses between syllables don't end a stretch
	loudAbove     = 20                     // dB above the baseline for a startle
	reactCooldown = 3 * time.Second
)

// SoundInput feeds the emulated microphone: a live level from the browser, a room level,
// or a burst ("clap", "talk"). Pass nil/"" for values that don't change.
func (d *Device) SoundInput(live, ambient *float64, burst string) {
	d.mu.Lock()
	defer d.mu.Unlock()
	now := time.Now()
	if live != nil {
		d.snd.live, d.snd.liveAt = *live, now
	}
	if ambient != nil {
		d.snd.ambient = clamp(*ambient, 20, 100)
	}
	switch burst {
	case "clap":
		d.snd.burst, d.snd.burstUntil = burst, now.Add(150*time.Millisecond)
	case "talk":
		d.snd.burst, d.snd.burstUntil = burst, now.Add(2500*time.Millisecond)
	}
}

func (d *Device) soundLoop(ctx context.Context) {
	tick := time.NewTicker(soundTick)
	defer tick.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-tick.C:
		}
		now := time.Now()
		d.mu.Lock()
		ev, changed := d.soundSampleLocked(d.nextLevelLocked(now), now)
		ready := d.linkState == linkReady
		level := math.Round(d.snd.avg)
		d.mu.Unlock()
		d.store.Put("indoor.noise", level, "local", 10*time.Second)
		if ev != nil && ready {
			d.send(*ev)
		}
		if changed {
			d.notifyChange()
		}
	}
}

// nextLevelLocked returns the next microphone sample.
func (d *Device) nextLevelLocked(now time.Time) float64 {
	s := &d.snd
	if !s.liveAt.IsZero() && now.Sub(s.liveAt) < time.Second {
		return s.live
	}
	level := s.ambient + d.rnd.NormFloat64()*1.5
	if now.Before(s.burstUntil) {
		switch s.burst {
		case "clap":
			level = 92
		case "talk": // syllables: loud and quiet in turn
			level = s.ambient + 14 + 6*math.Sin(float64(now.UnixMilli())/90)
		}
	}
	return clamp(level, 0, 120)
}

// soundSampleLocked processes one sample. It returns an event to report and whether
// the visible state changed.
func (d *Device) soundSampleLocked(level float64, now time.Time) (*protocol.Event, bool) {
	s := &d.snd
	if s.base == 0 {
		s.base, s.avg = level, level
	}
	s.level = level
	s.avg += (level - s.avg) * (float64(soundTick) / float64(30*time.Second))
	// The baseline drops quickly and rises slowly, so lasting noise becomes the new normal
	// after about a minute, while short sounds (voices, claps) hardly move it.
	tau := 20 * time.Second
	if level > s.base {
		tau = time.Minute
	}
	s.base += (level - s.base) * (float64(soundTick) / float64(tau))
	if !d.soundEnabledLocked() {
		return nil, false
	}
	loud := d.nvs.Float("sound.loud", 80)
	switch {
	case level >= loud && level-s.base >= loudAbove:
		s.voiceSince = time.Time{}
		if now.Sub(s.lastReact) < reactCooldown {
			return nil, false
		}
		s.lastReact = now
		d.reaction = timedMood{name: "surprised", reason: "loud noise", until: now.Add(1500 * time.Millisecond)}
		return &protocol.Event{T: "event", Kind: "sound", Name: "loud"}, true
	case level >= s.base+voiceAbove:
		if s.voiceSince.IsZero() || now.Sub(s.voiceLast) > voiceGap {
			s.voiceSince = now
		}
		s.voiceLast = now
		if now.Sub(s.voiceSince) >= voiceFor && now.Sub(s.lastReact) >= reactCooldown {
			s.lastReact = now
			d.reaction = timedMood{name: "curious", reason: "hears voices", until: now.Add(2 * time.Second)}
			return nil, true
		}
	}
	return nil, false
}

// tooLoudLocked reports whether the room has been loud for a while (for the mood).
func (d *Device) tooLoudLocked() (bool, float64) {
	if !d.soundEnabledLocked() || d.snd.base == 0 {
		return false, 0
	}
	return d.snd.avg >= d.nvs.Float("sound.too_loud", 70), d.snd.avg
}

// soundEnabledLocked is sound.enabled, true when not set (older settings).
func (d *Device) soundEnabledLocked() bool {
	v, ok := d.nvs.Get("sound.enabled")
	return !ok || v == true
}
