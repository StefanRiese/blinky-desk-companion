package install

import (
	"bytes"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"

	"github.com/StefanRiese/esp32_desk_buddy/host/internal/config"
)

func TestInstallIntoRoot(t *testing.T) {
	if runtime.GOOS != "linux" {
		t.Skip("install is Linux-only")
	}
	root := t.TempDir()
	var out bytes.Buffer
	o := Options{Root: root, TCP: ":7531", Serial: "off", Out: &out}
	if err := Install(o); err != nil {
		t.Fatal(err)
	}
	for _, f := range []string{BinPath, UnitPath, filepath.Join(ConfigDir, configName)} {
		if _, err := os.Stat(filepath.Join(root, f)); err != nil {
			t.Fatalf("%s missing: %v", f, err)
		}
	}
	cfg, found, err := config.Load(filepath.Join(root, ConfigDir, configName))
	if err != nil || !found {
		t.Fatalf("written config does not load: %v", err)
	}
	if !cfg.TCP.Enabled || cfg.TCP.Listen != ":7531" || !cfg.TCP.MDNS || cfg.Serial.Enabled {
		t.Fatalf("config = %+v", cfg)
	}

	// A second install keeps an edited config.
	cfgPath := filepath.Join(root, ConfigDir, configName)
	os.WriteFile(cfgPath, []byte("log: {level: debug}\n"), 0o640)
	if err := Install(o); err != nil {
		t.Fatal(err)
	}
	if b, _ := os.ReadFile(cfgPath); !strings.Contains(string(b), "debug") {
		t.Fatal("re-install overwrote the config")
	}

	if err := Uninstall(o, true); err != nil {
		t.Fatal(err)
	}
	for _, f := range []string{BinPath, UnitPath, ConfigDir} {
		if _, err := os.Stat(filepath.Join(root, f)); !os.IsNotExist(err) {
			t.Fatalf("%s still there after uninstall --purge", f)
		}
	}
}
