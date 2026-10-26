package sim

import (
	"encoding/json"
	"strings"
	"testing"
	"time"
)

func TestSetupPort(t *testing.T) {
	d := newTestDevice(t)
	d.nvs.SetMany(map[string]any{"link.token": "tok", "wifi.pass": "pw"})
	ch := make(chan []byte, 8)
	d.setup = ch
	next := func() map[string]any {
		t.Helper()
		select {
		case line := <-ch:
			var m map[string]any
			if err := json.Unmarshal(line, &m); err != nil {
				t.Fatal(err)
			}
			return m
		case <-time.After(time.Second):
			t.Fatal("no answer on the setup port")
			return nil
		}
	}

	d.handleSetup([]byte(`{"t":"hello","v":1,"host":"setup page","role":"setup"}`))
	if m := next(); m["t"] != "hello" || m["dev"] != "buddy-t" || m["token"] != nil {
		t.Fatalf("hello: %v (must not carry the link token)", m)
	}
	d.handleSetup([]byte(`{"t":"config","id":1,"get":["wifi"]}`))
	if m := next(); m["t"] != "config" || m["values"].(map[string]any)["wifi.pass"] != "***" {
		t.Fatalf("config get: %v", m)
	}
	d.handleSetup([]byte(`{"t":"config","id":2,"set":{"ui.night_from":21}}`))
	if m := next(); m["t"] != "ack" || d.nvs.Float("ui.night_from", 0) != 21 {
		t.Fatalf("config set: %v", m)
	}
	d.handleSetup([]byte(`{"t":"config","id":3,"set":{"alarm.list":[{"at":"7"}]}}`))
	if m := next(); m["t"] != "err" || !strings.Contains(m["msg"].(string), "HH:MM") {
		t.Fatalf("bad set: %v", m)
	}
	if d.linkState != linkOff {
		t.Fatalf("setup port changed the host link: %s", d.linkState)
	}
}
