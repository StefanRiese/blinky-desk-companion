// Package tcp accepts device connections over TCP (devices are TCP clients).
package tcp

import (
	"context"
	"errors"
	"log/slog"
	"net"
	"sync"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/session"
)

// Serve listens on addr and runs one session per connection until ctx ends.
// opt.Transport and opt.Addr are filled in per connection.
func Serve(ctx context.Context, addr string, opt session.Options) error {
	ln, err := net.Listen("tcp", addr)
	if err != nil {
		return err
	}
	slog.Info("tcp: listening", "addr", ln.Addr().String())
	return ServeListener(ctx, ln, opt)
}

// ServeListener is Serve on an existing listener (used by tests).
func ServeListener(ctx context.Context, ln net.Listener, opt session.Options) error {
	var wg sync.WaitGroup
	go func() {
		<-ctx.Done()
		ln.Close()
	}()
	defer wg.Wait()
	for {
		conn, err := ln.Accept()
		if err != nil {
			if ctx.Err() != nil || errors.Is(err, net.ErrClosed) {
				return nil
			}
			slog.Warn("tcp: accept", "err", err)
			continue
		}
		if tc, ok := conn.(*net.TCPConn); ok {
			_ = tc.SetKeepAlive(true) // best effort: the ping notices a dead link anyway
		}
		o := opt
		o.Transport = "tcp"
		o.Addr = conn.RemoteAddr().String()
		wg.Go(func() {
			defer conn.Close()
			go func() {
				<-ctx.Done()
				conn.Close()
			}()
			err := session.New(conn, o).Serve(ctx)
			slog.Debug("tcp: closed", "addr", o.Addr, "err", err)
		})
	}
}
