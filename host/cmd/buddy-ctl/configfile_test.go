// Tests for the settings export file and the import plan.
package main

import (
	"encoding/json"
	"reflect"
	"testing"
	"time"
)

func TestExportLeavesOutSecrets(t *testing.T) {
	values := map[string]any{"dev": "buddy", "wifi.ssid": "home", "wifi.pass": "***", "ui.dim_after": 60.0}
	f := exportFile(deviceInfo{Dev: "buddy", Board: "amoled175", FW: "0.3.0"}, values,
		time.Date(2026, 11, 2, 10, 0, 0, 0, time.UTC))
	if _, ok := f.Settings["wifi.pass"]; ok {
		t.Fatal("masked secret exported")
	}
	if f.Blinky != "settings" || f.Board != "amoled175" || f.Exported != "2026-11-02T10:00:00Z" || len(f.Settings) != 3 {
		t.Fatalf("file = %+v", f)
	}
	b, _ := json.Marshal(f)
	back, err := readSettings(b)
	if err != nil || !reflect.DeepEqual(back, f.Settings) {
		t.Fatalf("read back = %v, %v", back, err)
	}
}

func TestReadSettingsFormats(t *testing.T) {
	for _, in := range []string{
		`{"blinky":"settings","settings":{"ui.color":"#FFFFFF"}}`,
		`{"t":"config","id":1,"values":{"ui.color":"#FFFFFF"}}`, // buddy-ctl device config --get
		`{"ui.color":"#FFFFFF"}`,
	} {
		got, err := readSettings([]byte(in))
		if err != nil || got["ui.color"] != "#FFFFFF" {
			t.Errorf("%s: %v, %v", in, got, err)
		}
	}
	for _, in := range []string{`not json`, `{}`, `[1,2]`} {
		if _, err := readSettings([]byte(in)); err == nil {
			t.Errorf("%s: no error", in)
		}
	}
}

func TestImportPlan(t *testing.T) {
	current := map[string]any{"dev": "desk", "wifi.pass": "***", "ui.color": "#3EE8FF",
		"ui.pages": []any{"eyes", "clock"}, "ui.dim_after": 60.0}
	file := map[string]any{"dev": "buddy", "wifi.pass": "***", "ui.color": "#FFFFFF",
		"ui.pages": []any{"eyes", "clock"}, "ui.dim_after": 60.0, "new.setting": true}
	set, skipped := importPlan(file, current)
	if !reflect.DeepEqual(set, map[string]any{"ui.color": "#FFFFFF"}) {
		t.Errorf("set = %v", set)
	}
	if !reflect.DeepEqual(skipped, []string{"dev", "new.setting", "wifi.pass"}) {
		t.Errorf("skipped = %v", skipped)
	}
}
