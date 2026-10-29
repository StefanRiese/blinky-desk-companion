package firmware

import (
	"context"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/fetch"
)

// appImage builds the start of an app image with an app description.
func appImage(version string) []byte {
	b := make([]byte, 4096)
	b[0] = 0xE9
	binary.LittleEndian.PutUint32(b[0x20:], 0xABCD5432)
	copy(b[0x30:], version)
	copy(b[0x50:], "blinky")
	return b
}

func TestDescribe(t *testing.T) {
	app := appImage("0.2.0")
	info, err := Describe(app)
	if err != nil || info.Full || info.Version != "0.2.0" || info.Project != "blinky" {
		t.Fatalf("app: %+v %v", info, err)
	}
	full := make([]byte, AppOffset+len(app))
	full[0] = 0xE9 // the bootloader
	copy(full[AppOffset:], app)
	if info, err := Describe(full); err != nil || !info.Full || info.Version != "0.2.0" {
		t.Fatalf("full: %+v %v", info, err)
	}
	if _, err := Describe([]byte("hello")); err == nil {
		t.Fatal("not an image")
	}
}

func TestReleasesPickAndCache(t *testing.T) {
	img := appImage("0.2.0")
	downloads := 0
	var srv *httptest.Server
	srv = httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/releases" {
			asset := func(name string, body []byte) Asset {
				return Asset{Name: name, URL: srv.URL + "/dl/" + name, Size: len(body), Digest: "sha256:" + SHA256(body)}
			}
			json.NewEncoder(w).Encode([]Release{
				{Tag: "v0.3.0-rc1", Prerelease: true, Published: time.Now(), Assets: []Asset{asset("blinky-firmware-ws146.bin", img)}},
				{Tag: "v0.2.0", Published: time.Now().Add(-time.Hour), Assets: []Asset{asset("blinky-firmware-ws146.bin", img)}},
				{Tag: "v0.4.0", Draft: true},
			})
			return
		}
		downloads++
		w.Write(img)
	}))
	defer srv.Close()
	client, _ := fetch.New(fetch.Options{})
	s := &Source{Client: client, URL: srv.URL + "/releases", CacheDir: t.TempDir()}
	ctx := context.Background()
	rels, err := s.Releases(ctx)
	if err != nil || len(rels) != 2 || rels[0].Tag != "v0.3.0-rc1" {
		t.Fatalf("releases %+v %v", rels, err)
	}
	name := AssetName(Full, "ws146")
	r, a, err := Pick(rels, "latest", name)
	if err != nil || r.Tag != "v0.2.0" {
		t.Fatalf("latest picks the regular release: %v %v", r.Tag, err)
	}
	if r, _, err := Pick(rels, "0.3.0-rc1", name); err != nil || r.Tag != "v0.3.0-rc1" {
		t.Fatalf("by version: %v %v", r.Tag, err)
	}
	if _, _, err := Pick(rels, "latest", AssetName(Full, "amoled175")); err == nil {
		t.Fatal("no release has that board")
	}
	for range 2 {
		b, err := s.Image(ctx, r, a)
		if err != nil || len(b) != len(img) {
			t.Fatalf("image: %v", err)
		}
	}
	if downloads != 1 {
		t.Fatalf("%d downloads, the second must come from the cache", downloads)
	}
	if _, err := os.Stat(filepath.Join(s.CacheDir, "v0.2.0", name)); err != nil {
		t.Fatal(err)
	}
	bad := a
	bad.Digest = "sha256:" + fmt.Sprintf("%064d", 0)
	r.Tag = "v9"
	if _, err := s.Image(ctx, r, bad); err == nil {
		t.Fatal("a wrong SHA-256 must be an error")
	}
}
