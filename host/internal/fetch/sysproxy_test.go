package fetch

import (
	"net/url"
	"testing"
)

func TestParseWindows(t *testing.T) {
	p := parseWindows(true, "proxy.corp:8080", "<local>;*.corp;10.*")
	cases := map[string]string{
		"https://api.open-meteo.com/": "proxy.corp:8080",
		"http://intranet/":            "",
		"https://git.corp/":           "",
		"http://10.1.2.3/":            "",
	}
	for raw, want := range cases {
		u, _ := url.Parse(raw)
		got := ""
		if pu := p.proxyFor(u); pu != nil {
			got = pu.Host
		}
		if got != want {
			t.Errorf("%s: proxy %q, want %q", raw, got, want)
		}
	}
	p = parseWindows(true, "http=a:1;https=b:2", "")
	if p.http.Host != "a:1" || p.https.Host != "b:2" {
		t.Errorf("per scheme: %v %v", p.http, p.https)
	}
	if p = parseWindows(false, "a:1", ""); p.http != nil {
		t.Error("disabled proxy used")
	}
}

func TestParseScutil(t *testing.T) {
	out := `<dictionary> {
  ExceptionsList : <array> {
    0 : *.local
    1 : 169.254/16
  }
  ExcludeSimpleHostnames : 1
  FTPPassive : 1
  HTTPEnable : 1
  HTTPPort : 3128
  HTTPProxy : proxy.lan
  HTTPSEnable : 1
  HTTPSPort : 3129
  HTTPSProxy : proxy.lan
}`
	p := parseScutil(out)
	if p.https == nil || p.https.Host != "proxy.lan:3129" || p.http.Host != "proxy.lan:3128" {
		t.Fatalf("proxies %v %v", p.http, p.https)
	}
	for _, raw := range []string{"http://blinky.local/", "http://169.254.3.4/", "http://nas/"} {
		u, _ := url.Parse(raw)
		if p.proxyFor(u) != nil {
			t.Errorf("%s must bypass the proxy", raw)
		}
	}
	if p := parseScutil("<dictionary> {\n  HTTPEnable : 0\n}"); p.http != nil || p.https != nil {
		t.Error("no proxy expected")
	}
}
