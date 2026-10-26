package source

import (
	"context"
	"encoding/json"
	"os"
	"strings"
	"time"

	"github.com/shirou/gopsutil/v4/cpu"
	"github.com/shirou/gopsutil/v4/host"
	"github.com/shirou/gopsutil/v4/mem"
	"github.com/shirou/gopsutil/v4/sensors"
)

// Sys reports this host's CPU, RAM and temperature. No params.
type Sys struct{}

// NewSys returns the sys source and primes the CPU counter.
func NewSys() Sys {
	cpu.Percent(0, false)
	return Sys{}
}

func (Sys) Name() string                     { return "sys" }
func (Sys) NeedsInternet() bool              { return false }
func (Sys) MinInterval() time.Duration       { return 2 * time.Second }
func (Sys) DefaultInterval() time.Duration   { return 10 * time.Second }
func (Sys) Validate(p json.RawMessage) error { return decodeParams(p, &struct{}{}) }

func (Sys) Fetch(ctx context.Context, _ json.RawMessage) (Result, error) {
	data := map[string]any{}
	if name, err := os.Hostname(); err == nil {
		data["host"] = name
	}
	if p, err := cpu.PercentWithContext(ctx, 0, false); err == nil && len(p) == 1 {
		data["cpu"] = round1(p[0])
	}
	if vm, err := mem.VirtualMemoryWithContext(ctx); err == nil {
		data["ram"] = round1(vm.UsedPercent)
	}
	if up, err := host.UptimeWithContext(ctx); err == nil {
		data["uptime_h"] = round1(float64(up) / 3600)
	}
	if t, ok := cpuTemp(ctx); ok {
		data["temp"] = round1(t)
	}
	return Result{Data: data}, nil
}

// cpuTemp returns the hottest CPU-like sensor (not available on every OS).
func cpuTemp(ctx context.Context) (float64, bool) {
	temps, _ := sensors.TemperaturesWithContext(ctx)
	best, ok := 0.0, false
	for _, t := range temps {
		k := strings.ToLower(t.SensorKey)
		if !(strings.Contains(k, "core") || strings.Contains(k, "cpu") || strings.Contains(k, "package") ||
			strings.Contains(k, "tctl") || strings.Contains(k, "k10temp")) {
			continue
		}
		if t.Temperature > 0 && t.Temperature < 130 && t.Temperature > best {
			best, ok = t.Temperature, true
		}
	}
	return best, ok
}
