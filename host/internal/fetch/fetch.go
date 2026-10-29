// Package fetch provides the shared, proxy-aware HTTP client used by all sources.
package fetch

import (
	"context"
	"crypto/tls"
	"crypto/x509"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"time"
)

// MaxBody is the largest response body a source may read.
const MaxBody = 1 << 20

// Client wraps an http.Client with JSON helpers.
type Client struct {
	HTTP      *http.Client
	UserAgent string
}

// Options configures New.
type Options struct {
	ProxyURL string // empty = proxy from the environment, else from the system settings
	CAFile   string // extra CA certificates (PEM)
	Timeout  time.Duration
	Version  string
}

// New builds a client. Without ProxyURL it honours HTTPS_PROXY, HTTP_PROXY and NO_PROXY,
// and without those the proxy of the Windows or macOS settings (sysproxy.go).
func New(o Options) (*Client, error) {
	tr := http.DefaultTransport.(*http.Transport).Clone()
	tr.Proxy = http.ProxyFromEnvironment
	if f := systemProxyFunc(); f != nil && !envProxySet() {
		tr.Proxy = f
	}
	if o.ProxyURL != "" {
		u, err := url.Parse(o.ProxyURL)
		if err != nil {
			return nil, fmt.Errorf("proxy url: %w", err)
		}
		tr.Proxy = http.ProxyURL(u)
	}
	if o.CAFile != "" {
		pem, err := os.ReadFile(o.CAFile)
		if err != nil {
			return nil, fmt.Errorf("ca file: %w", err)
		}
		pool, err := x509.SystemCertPool()
		if err != nil || pool == nil {
			pool = x509.NewCertPool()
		}
		if !pool.AppendCertsFromPEM(pem) {
			return nil, fmt.Errorf("ca file %s: no certificates found", o.CAFile)
		}
		tr.TLSClientConfig = &tls.Config{RootCAs: pool, MinVersion: tls.VersionTLS12}
	}
	if o.Timeout == 0 {
		o.Timeout = 20 * time.Second
	}
	ua := "buddy-link"
	if o.Version != "" {
		ua += "/" + o.Version
	}
	return &Client{HTTP: &http.Client{Transport: tr, Timeout: o.Timeout}, UserAgent: ua}, nil
}

// StatusError is returned for non-2xx responses.
type StatusError struct {
	Code int
	URL  string
}

func (e *StatusError) Error() string { return fmt.Sprintf("HTTP %d", e.Code) }

// GetJSON fetches rawURL and decodes the JSON body into out.
func (c *Client) GetJSON(ctx context.Context, rawURL string, headers map[string]string, out any) error {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, rawURL, nil)
	if err != nil {
		return err
	}
	req.Header.Set("Accept", "application/json")
	req.Header.Set("User-Agent", c.UserAgent)
	for k, v := range headers {
		req.Header.Set(k, v)
	}
	resp, err := c.HTTP.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	body := io.LimitReader(resp.Body, MaxBody+1)
	if resp.StatusCode < 200 || resp.StatusCode > 299 {
		io.Copy(io.Discard, body)
		return &StatusError{Code: resp.StatusCode, URL: rawURL}
	}
	data, err := io.ReadAll(body)
	if err != nil {
		return err
	}
	if len(data) > MaxBody {
		return fmt.Errorf("response larger than %d bytes", MaxBody)
	}
	if err := json.Unmarshal(data, out); err != nil {
		return fmt.Errorf("invalid JSON: %w", err)
	}
	return nil
}

// Online reports whether probeURL answers at all (any HTTP status counts).
func (c *Client) Online(ctx context.Context, probeURL string) bool {
	ctx, cancel := context.WithTimeout(ctx, 8*time.Second)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, http.MethodHead, probeURL, nil)
	if err != nil {
		return false
	}
	req.Header.Set("User-Agent", c.UserAgent)
	resp, err := c.HTTP.Do(req)
	if err != nil {
		return false
	}
	resp.Body.Close()
	return true
}
