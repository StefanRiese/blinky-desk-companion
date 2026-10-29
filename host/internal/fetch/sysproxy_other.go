//go:build !windows && !darwin

package fetch

// readSystemProxy is nil: Linux has no common proxy setting besides the env vars.
var readSystemProxy func() (sysProxy, error)
