package sim

import (
	"fmt"
	"os"
	"regexp"
	"testing"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/protocol"
)

// TestSchemaMatchesFirmware compares the keys with the firmware's registry, in order.
func TestSchemaMatchesFirmware(t *testing.T) {
	src, err := os.ReadFile("../../../firmware/components/app_core/src/settings.cpp")
	if err != nil {
		t.Fatal(err)
	}
	var want []string
	for _, m := range regexp.MustCompile(`\.key = "([^"]+)"`).FindAllStringSubmatch(string(src), -1) {
		want = append(want, m[1])
	}
	var got []string
	for _, s := range schema {
		got = append(got, s.Key)
	}
	if fmt.Sprint(got) != fmt.Sprint(want) {
		t.Fatalf("schema keys\n got %v\nwant %v (settings.cpp)", got, want)
	}
}

func TestSchemaPages(t *testing.T) {
	d := newTestDevice(t)
	var lines []any
	reply := func(v any) { lines = append(lines, v) }
	var keys []string
	from := 0
	for pages := 1; ; pages++ {
		lines = nil
		d.handleConfig(mustEnvelope(t, fmt.Sprintf(`{"t":"config","id":5,"schema_from":%d}`, from)), reply)
		if len(lines) != 1 {
			t.Fatalf("page %d: %d replies", pages, len(lines))
		}
		if _, err := protocol.Encode(lines[0]); err != nil {
			t.Fatalf("page %d: %v", pages, err)
		}
		m := lines[0].(protocol.Config)
		for _, s := range m.Schema {
			keys = append(keys, s.Key)
		}
		if m.Next == 0 {
			if pages < 2 {
				t.Fatal("expected several pages")
			}
			break
		}
		from = m.Next
	}
	if len(keys) != len(schema) {
		t.Fatalf("got %d keys, want %d", len(keys), len(schema))
	}
}

func mustEnvelope(t *testing.T, line string) protocol.Envelope {
	t.Helper()
	e, err := protocol.Decode([]byte(line))
	if err != nil {
		t.Fatal(err)
	}
	return e
}
