// Firmware updates waiting for the connected devices: the release list from GitHub, kept for
// 10 minutes and refreshed in the background, compared with each device's firmware
// (firmware.OffersFor). The device list carries the offers; the tray counts them.
package control

import (
	"context"
	"log/slog"
	"sync"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/firmware"
	"github.com/StefanRiese/blinky-desk-companion/host/internal/hub"
)

// releasesMaxAge is how old the release list may get before it is fetched again.
const releasesMaxAge = 10 * time.Minute

type releaseCache struct {
	mu   sync.Mutex
	rels []firmware.Release
	at   time.Time
	busy bool
}

// cachedReleases returns the release list as last fetched (nil before the first fetch) and
// starts a refresh in the background when it is older than releasesMaxAge.
func (s *Server) cachedReleases() []firmware.Release {
	if s.Flasher == nil || s.Flasher.Source == nil {
		return nil
	}
	c := &s.rels
	c.mu.Lock()
	defer c.mu.Unlock()
	if time.Since(c.at) > releasesMaxAge && !c.busy {
		c.busy = true
		go s.refreshReleases(context.Background())
	}
	return c.rels
}

func (s *Server) refreshReleases(ctx context.Context) {
	ctx, cancel := context.WithTimeout(ctx, time.Minute)
	defer cancel()
	rels, err := s.Flasher.Source.Releases(ctx)
	c := &s.rels
	c.mu.Lock()
	defer c.mu.Unlock()
	c.busy = false
	if err != nil {
		slog.Debug("firmware releases", "err", err)
		c.at = time.Now().Add(-releasesMaxAge + time.Minute) // try again in a minute
		return
	}
	c.rels, c.at = rels, time.Now()
}

// WatchReleases keeps the release list fresh until ctx ends, so the tray knows about updates
// even while nobody has the web UI open.
func (s *Server) WatchReleases(ctx context.Context) {
	t := time.NewTicker(time.Minute)
	defer t.Stop()
	for {
		s.cachedReleases()
		select {
		case <-ctx.Done():
			return
		case <-t.C:
		}
	}
}

// offers returns the updates for a device (nil when the release list isn't known yet).
func (s *Server) offers(info hub.Info, rels []firmware.Release) *firmware.Offers {
	if rels == nil || info.FW == "" {
		return nil
	}
	o := firmware.OffersFor(rels, info.Board, info.FW)
	return &o
}

// UpdatesWaiting counts the connected devices with an update waiting (for the tray).
func (s *Server) UpdatesWaiting() int {
	rels := s.cachedReleases()
	n := 0
	for _, d := range s.Hub.Devices("") {
		if o := s.offers(d.Info(), rels); o != nil && o.Waiting {
			n++
		}
	}
	return n
}
