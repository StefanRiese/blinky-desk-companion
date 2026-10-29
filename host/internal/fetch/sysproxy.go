// The proxy from the system settings, used when neither proxy.url nor the proxy
// environment variables are set: the Windows Internet settings (registry) or the macOS
// network settings (scutil --proxy). Linux desktops have no common setting: env vars only.
// PAC files (automatic configuration scripts) are not supported.
package fetch

import (
	"bufio"
	"log/slog"
	"net"
	"net/http"
	"net/url"
	"os"
	"strings"
	"sync"
	"time"
)

// sysProxy is a proxy setting: proxies by scheme and the hosts that bypass them.
type sysProxy struct {
	http, https *url.URL
	bypass      []string // host patterns: "*.corp", "10.*", "169.254/16", "<local>"
}

// envProxySet reports whether a proxy env var decides (then the system setting doesn't).
func envProxySet() bool {
	for _, k := range []string{"HTTPS_PROXY", "https_proxy", "HTTP_PROXY", "http_proxy"} {
		if os.Getenv(k) != "" {
			return true
		}
	}
	return false
}

// systemProxyFunc returns the transport's Proxy function: the system setting, read again
// every minute (a laptop changes networks), or nil when the platform has none.
func systemProxyFunc() func(*http.Request) (*url.URL, error) {
	if readSystemProxy == nil {
		return nil
	}
	var mu sync.Mutex
	var cur sysProxy
	var at time.Time
	var logged string
	return func(req *http.Request) (*url.URL, error) {
		mu.Lock()
		if time.Since(at) > time.Minute {
			p, err := readSystemProxy()
			if err != nil {
				p = sysProxy{}
			}
			cur, at = p, time.Now()
			if s := p.String(); s != logged {
				logged = s
				slog.Info("system proxy", "proxy", s)
			}
		}
		p := cur
		mu.Unlock()
		return p.proxyFor(req.URL), nil
	}
}

func (p sysProxy) String() string {
	switch {
	case p.https != nil:
		return p.https.Host
	case p.http != nil:
		return p.http.Host
	}
	return "none"
}

// proxyFor returns the proxy for u, or nil for a direct connection.
func (p sysProxy) proxyFor(u *url.URL) *url.URL {
	host := u.Hostname()
	for _, b := range p.bypass {
		if bypassMatch(b, host) {
			return nil
		}
	}
	if u.Scheme == "https" && p.https != nil {
		return p.https
	}
	return p.http
}

// bypassMatch reports whether host matches a bypass pattern of the Windows or macOS
// settings: "<local>" (names without a dot), wildcards ("*.corp", "10.*") or a network
// ("169.254/16", "10.0.0.0/8").
func bypassMatch(pattern, host string) bool {
	pattern = strings.ToLower(strings.TrimSpace(pattern))
	host = strings.ToLower(host)
	switch {
	case pattern == "":
		return false
	case pattern == "<local>":
		return !strings.Contains(host, ".")
	case strings.Contains(pattern, "/"):
		ip := net.ParseIP(host)
		if ip == nil {
			return false
		}
		addr, bits, _ := strings.Cut(pattern, "/")
		for strings.Count(addr, ".") < 3 { // macOS writes "169.254/16"
			addr += ".0"
		}
		_, n, err := net.ParseCIDR(addr + "/" + bits)
		return err == nil && n.Contains(ip)
	}
	return globMatch(pattern, host)
}

// globMatch matches * (any characters) in pattern against s.
func globMatch(pattern, s string) bool {
	parts := strings.Split(pattern, "*")
	if len(parts) == 1 {
		return pattern == s
	}
	if !strings.HasPrefix(s, parts[0]) {
		return false
	}
	s = s[len(parts[0]):]
	for _, mid := range parts[1 : len(parts)-1] {
		i := strings.Index(s, mid)
		if i < 0 {
			return false
		}
		s = s[i+len(mid):]
	}
	return strings.HasSuffix(s, parts[len(parts)-1])
}

// parseWindows reads the Windows Internet settings: ProxyServer is "host:port" for all
// schemes or "http=host:port;https=host:port", ProxyOverride is a ";" list.
func parseWindows(enabled bool, server, override string) sysProxy {
	var p sysProxy
	if !enabled || server == "" {
		return p
	}
	if !strings.Contains(server, "=") {
		u := proxyURL(server)
		p.http, p.https = u, u
	} else {
		for part := range strings.SplitSeq(server, ";") {
			scheme, addr, _ := strings.Cut(strings.TrimSpace(part), "=")
			switch strings.ToLower(scheme) {
			case "http":
				p.http = proxyURL(addr)
			case "https":
				p.https = proxyURL(addr)
			}
		}
	}
	for b := range strings.SplitSeq(override, ";") {
		if b = strings.TrimSpace(b); b != "" {
			p.bypass = append(p.bypass, b)
		}
	}
	return p
}

// parseScutil reads the output of macOS "scutil --proxy".
func parseScutil(out string) sysProxy {
	var p sysProxy
	kv := map[string]string{}
	inExceptions := false
	sc := bufio.NewScanner(strings.NewReader(out))
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		if strings.HasPrefix(line, "ExceptionsList") {
			inExceptions = true
			continue
		}
		if inExceptions {
			if line == "}" {
				inExceptions = false
				continue
			}
			if _, v, ok := strings.Cut(line, " : "); ok {
				p.bypass = append(p.bypass, v)
			}
			continue
		}
		if k, v, ok := strings.Cut(line, " : "); ok {
			kv[k] = v
		}
	}
	if kv["ExcludeSimpleHostnames"] == "1" {
		p.bypass = append(p.bypass, "<local>")
	}
	if kv["HTTPEnable"] == "1" && kv["HTTPProxy"] != "" {
		p.http = proxyURL(kv["HTTPProxy"] + ":" + kv["HTTPPort"])
	}
	if kv["HTTPSEnable"] == "1" && kv["HTTPSProxy"] != "" {
		p.https = proxyURL(kv["HTTPSProxy"] + ":" + kv["HTTPSPort"])
	}
	return p
}

func proxyURL(addr string) *url.URL {
	addr = strings.TrimSuffix(strings.TrimSpace(addr), ":")
	if addr == "" {
		return nil
	}
	if !strings.Contains(addr, "://") {
		addr = "http://" + addr
	}
	u, err := url.Parse(addr)
	if err != nil {
		return nil
	}
	return u
}
