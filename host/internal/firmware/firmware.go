// Package firmware finds Blinky firmware images in the GitHub releases, downloads them
// only when a flash or update needs one, checks them against the SHA-256 GitHub lists for
// each file, and keeps the newest one per file in a cache folder. Images are never built
// into buddy-link, so a new firmware release needs no new buddy-link.
package firmware

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/fetch"
)

// ReleasesURL lists the project's releases (GitHub API).
const ReleasesURL = "https://api.github.com/repos/StefanRiese/blinky-desk-companion/releases?per_page=20"

// Boards are the boards the firmware is built for.
var Boards = []string{"ws146", "amoled175"}

// Image kinds: the whole flash from offset 0 (first flash) or the app alone (update).
const (
	Full = "blinky-firmware-%s.bin"
	App  = "blinky-app-%s.bin"
)

// AssetName returns the release file of kind (Full or App) for board.
func AssetName(kind, board string) string { return fmt.Sprintf(kind, board) }

// Release is one GitHub release.
type Release struct {
	Tag        string    `json:"tag_name"`
	Name       string    `json:"name"`
	Prerelease bool      `json:"prerelease"`
	Draft      bool      `json:"draft"`
	Published  time.Time `json:"published_at"`
	Assets     []Asset   `json:"assets"`
}

// Asset is a file of a release. Digest is "sha256:<hex>".
type Asset struct {
	Name   string `json:"name"`
	URL    string `json:"browser_download_url"`
	Size   int    `json:"size"`
	Digest string `json:"digest"`
}

// Version returns the release's version without the leading "v".
func (r Release) Version() string { return strings.TrimPrefix(r.Tag, "v") }

// Asset returns the release's file called name.
func (r Release) Asset(name string) (Asset, bool) {
	for _, a := range r.Assets {
		if a.Name == name {
			return a, true
		}
	}
	return Asset{}, false
}

// Source reads releases and downloads images.
type Source struct {
	Client   *fetch.Client
	URL      string // releases list; "" = ReleasesURL
	CacheDir string // "" = <user cache dir>/buddy-link/firmware
}

// Releases returns the published releases, newest first.
func (s *Source) Releases(ctx context.Context) ([]Release, error) {
	u := s.URL
	if u == "" {
		u = ReleasesURL
	}
	var all []Release
	if err := s.Client.GetJSON(ctx, u, map[string]string{"Accept": "application/vnd.github+json"}, &all); err != nil {
		return nil, fmt.Errorf("releases: %w", err)
	}
	var out []Release
	for _, r := range all {
		if !r.Draft {
			out = append(out, r)
		}
	}
	sort.SliceStable(out, func(i, j int) bool { return out[i].Published.After(out[j].Published) })
	return out, nil
}

// Pick returns the release called version ("v0.1.3" or "0.1.3"), or for "" or "latest"
// the newest regular release that has file name, or else the newest pre-release with it.
func Pick(rels []Release, version, name string) (Release, Asset, error) {
	if version != "" && version != "latest" {
		for _, r := range rels {
			if r.Version() == strings.TrimPrefix(version, "v") {
				if a, ok := r.Asset(name); ok {
					return r, a, nil
				}
				return Release{}, Asset{}, fmt.Errorf("release %s has no %s", r.Tag, name)
			}
		}
		return Release{}, Asset{}, fmt.Errorf("no release %s", version)
	}
	for _, pre := range []bool{false, true} {
		for _, r := range rels {
			if r.Prerelease != pre {
				continue
			}
			if a, ok := r.Asset(name); ok {
				return r, a, nil
			}
		}
	}
	return Release{}, Asset{}, fmt.Errorf("no release has %s yet", name)
}

func (s *Source) cacheDir() (string, error) {
	if s.CacheDir != "" {
		return s.CacheDir, nil
	}
	dir, err := os.UserCacheDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(dir, "buddy-link", "firmware"), nil
}

// MaxImage is the largest image downloaded (the app partitions are 6 MiB).
const MaxImage = 16 << 20

// Image returns the file a of release r: from the cache if it is there and intact, else
// downloaded and checked. Older cached versions of the same file are removed.
func (s *Source) Image(ctx context.Context, r Release, a Asset) ([]byte, error) {
	want, hasDigest := strings.CutPrefix(a.Digest, "sha256:")
	if !hasDigest {
		return nil, fmt.Errorf("%s %s: GitHub lists no SHA-256 for it", r.Tag, a.Name)
	}
	dir, err := s.cacheDir()
	if err != nil {
		return nil, err
	}
	path := filepath.Join(dir, r.Tag, a.Name)
	if b, err := os.ReadFile(path); err == nil && sha(b) == want {
		return b, nil
	}
	b, err := s.download(ctx, a.URL)
	if err != nil {
		return nil, fmt.Errorf("download %s: %w", a.Name, err)
	}
	if got := sha(b); got != want {
		return nil, fmt.Errorf("download %s: SHA-256 %s, but the release lists %s", a.Name, got, want)
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err == nil {
		if os.WriteFile(path, b, 0o644) == nil {
			s.prune(dir, r.Tag, a.Name)
		}
	}
	return b, nil
}

// prune removes cached copies of name from other releases.
func (s *Source) prune(dir, keepTag, name string) {
	tags, _ := os.ReadDir(dir)
	for _, t := range tags {
		if !t.IsDir() || t.Name() == keepTag {
			continue
		}
		os.Remove(filepath.Join(dir, t.Name(), name))
		os.Remove(filepath.Join(dir, t.Name())) // only when empty
	}
}

func (s *Source) download(ctx context.Context, url string) ([]byte, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", s.Client.UserAgent)
	client := *s.Client.HTTP
	client.Timeout = 5 * time.Minute
	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, &fetch.StatusError{Code: resp.StatusCode, URL: url}
	}
	b, err := io.ReadAll(io.LimitReader(resp.Body, MaxImage+1))
	if err != nil {
		return nil, err
	}
	if len(b) > MaxImage {
		return nil, errors.New("file too large for a firmware image")
	}
	return b, nil
}

func sha(b []byte) string {
	h := sha256.Sum256(b)
	return hex.EncodeToString(h[:])
}

// SHA256 returns the hex SHA-256 of b.
func SHA256(b []byte) string { return sha(b) }

// AppOffset is where the app starts in a full image (partitions.csv: ota_0).
const AppOffset = 0x20000

// Info describes an image: whether it is a full image (bootloader at 0, app at AppOffset)
// or an app alone, and the app's version and project name from its esp_app_desc_t.
type Info struct {
	Full    bool
	Version string
	Project string
}

// Describe reads an image's app description. A file that is no ESP32 image is an error.
func Describe(b []byte) (Info, error) {
	for _, off := range []int{0, AppOffset} {
		const descAt = 0x20 // the app description is the first segment's data
		if len(b) < off+0x70 || b[off] != 0xE9 {
			continue
		}
		if binary.LittleEndian.Uint32(b[off+descAt:]) != 0xABCD5432 {
			continue
		}
		return Info{
			Full:    off != 0,
			Version: cstr(b[off+descAt+16 : off+descAt+48]),
			Project: cstr(b[off+descAt+48 : off+descAt+80]),
		}, nil
	}
	return Info{}, errors.New("not an ESP32 firmware image")
}

func cstr(b []byte) string {
	if i := bytes.IndexByte(b, 0); i >= 0 {
		b = b[:i]
	}
	return string(b)
}
