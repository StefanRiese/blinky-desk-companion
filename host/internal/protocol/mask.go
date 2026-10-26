package protocol

import (
	"encoding/json"
	"strings"
)

// Masked replaces secret values in logs, APIs and config exports.
const Masked = "***"

var secretKeys = map[string]bool{
	"token": true, "token_secret": true, "secret": true, "pass": true,
	"password": true, "authorization": true, "api_key": true, "apikey": true,
}

// IsSecretKey reports whether a (possibly dotted) key names a secret.
func IsSecretKey(key string) bool {
	if i := strings.LastIndexByte(key, '.'); i >= 0 {
		key = key[i+1:]
	}
	return secretKeys[strings.ToLower(key)]
}

// MaskValue returns a copy of v (decoded JSON) with secret values replaced.
func MaskValue(v any) any {
	switch t := v.(type) {
	case map[string]any:
		out := make(map[string]any, len(t))
		for k, val := range t {
			if IsSecretKey(k) {
				if s, ok := val.(string); ok && s == "" {
					out[k] = ""
				} else {
					out[k] = Masked
				}
				continue
			}
			out[k] = MaskValue(val)
		}
		return out
	case []any:
		out := make([]any, len(t))
		for i, val := range t {
			out[i] = MaskValue(val)
		}
		return out
	default:
		return v
	}
}

// MaskJSON masks secrets in an encoded JSON object. Invalid input is returned unchanged.
func MaskJSON(b []byte) []byte {
	var v any
	if err := json.Unmarshal(b, &v); err != nil {
		return b
	}
	out, err := json.Marshal(MaskValue(v))
	if err != nil {
		return b
	}
	return out
}
