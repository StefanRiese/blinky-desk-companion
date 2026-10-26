//go:build !windows && !(darwin && cgo)

package tray

import "context"

// Available reports whether this build shows a tray icon.
const Available = false

func run(ctx context.Context, _ Options) { <-ctx.Done() }
