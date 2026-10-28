package protocol

import (
	"bufio"
	"bytes"
	"errors"
	"io"
	"os"
	"strings"
	"testing"
)

func TestReaderSkipsNoiseAndLongLines(t *testing.T) {
	long := "{" + strings.Repeat("x", MaxLine+10) + "}"
	in := "ESP-ROM:esp32s3\r\n\n" + long + "\n" + `{"t":"ping"}` + "\r\n" + `  {"t":"hello","v":1}`
	r := NewReader(strings.NewReader(in))
	var got []string
	for {
		line, err := r.ReadLine()
		if errors.Is(err, io.EOF) {
			break
		}
		if err != nil {
			t.Fatal(err)
		}
		got = append(got, string(line))
	}
	want := []string{`{"t":"ping"}`, `{"t":"hello","v":1}`}
	if strings.Join(got, "|") != strings.Join(want, "|") {
		t.Fatalf("got %q, want %q", got, want)
	}
}

func TestEncodeLimit(t *testing.T) {
	_, err := Encode(Notify{T: "notify", Text: strings.Repeat("a", MaxLine)})
	if !errors.Is(err, ErrTooLong) {
		t.Fatalf("want ErrTooLong, got %v", err)
	}
	var buf bytes.Buffer
	if _, err := NewWriter(&buf).Write(Ping{T: "ping"}); err != nil {
		t.Fatal(err)
	}
	if buf.String() != "{\"t\":\"ping\"}\n" {
		t.Fatalf("got %q", buf.String())
	}
}

func TestMask(t *testing.T) {
	in := []byte(`{"t":"sub","sid":"ci","p":{"url":"u","token":"abc","headers":{"Authorization":"Bearer x"},"list":[{"password":"p"}]}}`)
	out := string(MaskJSON(in))
	for _, secret := range []string{"abc", "Bearer x", `"p"}`} {
		if strings.Contains(out, secret) {
			t.Fatalf("secret %q not masked: %s", secret, out)
		}
	}
	if !strings.Contains(out, `"url":"u"`) {
		t.Fatalf("non-secret changed: %s", out)
	}
}

// TestGoldenExamples decodes every line of protocol/examples/*.ndjson.
func TestGoldenExamples(t *testing.T) {
	f, err := os.Open("../../../protocol/examples/handshake.ndjson")
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	sc := bufio.NewScanner(f)
	n := 0
	for sc.Scan() {
		n++
		e, err := Decode(sc.Bytes())
		if err != nil {
			t.Fatalf("line %d: %v", n, err)
		}
		if len(sc.Bytes()) > MaxLine {
			t.Fatalf("line %d longer than %d bytes", n, MaxLine)
		}
		var v any
		switch e.T {
		case "hello":
			v = &Hello{}
		case "sub":
			v = &Sub{}
		case "weather":
			v = &Weather{}
		case "data":
			v = &Data{}
		case "config":
			v = &Config{}
		case "web":
			v = &Web{}
		default:
			continue
		}
		if err := e.As(v); err != nil {
			t.Fatalf("line %d (%s): %v", n, e.T, err)
		}
	}
	if n < 10 {
		t.Fatalf("only %d lines read", n)
	}
}
