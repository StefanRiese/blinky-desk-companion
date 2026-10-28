// Package discovery announces buddy-link on the local network with mDNS/DNS-SD, so devices
// find it without a fixed address (link.tcp_host = "auto"), and looks it up (emulator).
package discovery

import (
	"context"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"strings"
	"time"

	"github.com/hashicorp/mdns"
)

// Service is the DNS-SD service type of buddy-link's TCP transport.
const Service = "_blinky._tcp"

// quiet keeps the library's own log lines out of buddy-link's log.
var quiet = log.New(io.Discard, "", 0)

// Announce advertises buddy-link on port until ctx ends. txt are key=value pairs
// (e.g. "ver=0.1.0", "token=1").
func Announce(ctx context.Context, instance string, port int, txt []string) error {
	host, _ := os.Hostname()
	host = strings.TrimSuffix(strings.Split(host, ".")[0], ".") + ".local."
	svc, err := mdns.NewMDNSService(instance, Service, "", host, port, localIPs(), txt)
	if err != nil {
		return fmt.Errorf("mdns: %w", err)
	}
	srv, err := mdns.NewServer(&mdns.Config{Zone: svc, Logger: quiet})
	if err != nil {
		return fmt.Errorf("mdns: %w", err)
	}
	<-ctx.Done()
	return srv.Shutdown()
}

// Host is a buddy-link found on the network.
type Host struct {
	Name string            // instance name, e.g. "buddy-link@pve-buddy"
	Addr string            // host:port to dial
	TXT  map[string]string // announced key=value pairs
}

// Find looks for buddy-link instances for up to timeout.
func Find(ctx context.Context, timeout time.Duration) ([]Host, error) {
	entries := make(chan *mdns.ServiceEntry, 16)
	params := mdns.DefaultParams(Service)
	params.Entries = entries
	params.Timeout = timeout
	params.DisableIPv6 = true
	params.Logger = quiet
	errc := make(chan error, 1)
	go func() { errc <- mdns.QueryContext(ctx, params); close(entries) }()
	var hosts []Host
	seen := map[string]bool{}
	for e := range entries {
		if e.AddrV4 == nil || seen[e.Name] {
			continue
		}
		seen[e.Name] = true
		h := Host{Name: instanceName(e.Name), Addr: net.JoinHostPort(e.AddrV4.String(), fmt.Sprint(e.Port)), TXT: map[string]string{}}
		for _, f := range e.InfoFields {
			k, v, _ := strings.Cut(f, "=")
			h.TXT[k] = v
		}
		hosts = append(hosts, h)
	}
	return hosts, <-errc
}

// instanceName turns "buddy-link@pve\.local._blinky._tcp.local." into "buddy-link@pve.local".
func instanceName(full string) string {
	name := strings.TrimSuffix(full, "."+Service+".local.")
	return strings.ReplaceAll(name, `\`, "")
}

// localIPs returns this machine's non-loopback IPv4 addresses (containers often can't
// resolve their own host name, so the addresses are given explicitly).
func localIPs() []net.IP {
	var ips []net.IP
	addrs, _ := net.InterfaceAddrs()
	for _, a := range addrs {
		if ipn, ok := a.(*net.IPNet); ok && !ipn.IP.IsLoopback() && ipn.IP.To4() != nil {
			ips = append(ips, ipn.IP)
		}
	}
	return ips
}
