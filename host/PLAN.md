# Host Service Plan (`buddyd`)

Part of the [main plan](../PLAN.md). Protocol details: [protocol/PROTOCOL.md](../protocol/PROTOCOL.md).

## Role
`buddyd` is a **stateless internet gateway** for the device. The device owns all
configuration (what to show, which sources, locations, tokens, intervals) and tells the
host what to fetch. The host only does the fetching the device can't do itself because
its WiFi has no internet (or there is no WiFi at all).

Consequences:
- **Swap the host anytime.** Plug the buddy into another laptop, or let it connect to the
  Proxmox container. As soon as it connects, the device sends its subscriptions and
  everything works again. Nothing needs to be set up on the new host.
- **Zero config by default.** `buddyd` runs without a config file. The few host settings
  that exist are about the host itself (ports, proxy, log level).
- **No duplicated logic.** Sources the device can also fetch itself over WiFi (time,
  weather, simple HTTP JSON) produce the same keys either way, so the UI doesn't care
  who fetched them.

## Status (2026-10-26)
First implementation in this folder, see [README.md](README.md). Needs Go 1.27 (the module
says `go 1.27.0`), cross-compiles without cgo for Windows, macOS and Linux. CI
([`.github/workflows/host.yml`](../.github/workflows/host.yml)) runs gofmt, vet and
`go test -race` on Linux, Windows and macOS, then builds all 7 OS/arch binaries.

| Done | Not yet |
|---|---|
| protocol package + golden examples, session (handshake, subscriptions, keepalive, request/reply) | Windows/macOS autostart (service or `--user`), tray icon |
| serial transport (VID/PID auto-detect, PTY/explicit port), TCP transport + token | system proxy settings on Windows/macOS (env vars and `proxy.url` work) |
| sources `time`, `weather`, `aqi`, `http`, `sys`, shared fetch cache | sources `gitlab`, `calendar`, `proxmox`, `ha` |
| control API + `buddyctl` (notify, mood, data, page, devices, device config, monitor, status) | `--direct` mode, Docker image |
| mDNS advert `_deskbuddy._tcp` (hashicorp/mdns), `buddyd install` (Linux/systemd), Proxmox LXC script, release workflow | per-device TCP tokens |
| host config (YAML + env + flags, `${env:}`/`${file:}`) | GoReleaser, Docker image, golangci-lint |
| CI: test on Linux/Windows/macOS, cross-build artifacts | |
| `buddysim device`: emulator with browser UI (display, SourceRouter, NVS, failover toggles) | `buddysim host` (fake host for firmware tests) |

## Goals
- Runs on **Windows, macOS and Linux** (laptop/desktop) and in a **Proxmox LXC / Docker
  container**. One static binary per OS/arch, no runtime to install.
- Serves devices over **USB serial** and **TCP** at the same time, any number of devices.
- Offers a fixed set of **sources** (weather, GitLab, calendar, …) plus a generic `http`
  source, so new data needs a device config change, not a host update.
- Works behind corporate proxies (the usual reason the laptop has internet and the
  device's WiFi doesn't).
- Small extras that only make sense on a host: its own system stats (as a source the device
  can subscribe to) and `buddyctl notify` for scripts.

## Why Go
Single static binaries from one module, cheap cross-compiling, a ~15 MB container image,
and libraries for every part of the plan. Go was preferred over Python (painful
packaging on Windows/macOS) and over Rust (slower to write for I/O glue, no maintained
Proxmox/CalDAV crates, harder macOS cross-builds). Protocol drift between firmware (C++)
and host is prevented by the shared JSON Schemas and golden files in `protocol/`.

## Stack
| Concern | Library |
|---|---|
| CLI / subcommands | `github.com/spf13/cobra` (help per command, shell completion) |
| Host config (optional file + env + flags) | `go.yaml.in/yaml/v3` + small env/reference layer. Not koanf: the host config has only 6 small sections since the device owns its config, so koanf would add many dependencies for ~200 lines of code |
| Serial + USB VID/PID lookup | `go.bug.st/serial` + `go.bug.st/serial/enumerator` |
| mDNS advert | `github.com/hashicorp/mdns` (maintained; zeroconf was last released 2020) |
| System proxy (Windows/macOS settings, PAC) | `github.com/mattn/go-ieproxy` (or similar), on top of `HTTPS_PROXY` env |
| JSON extraction for `http` source | stdlib + JSON Pointer (RFC 6901), small own implementation |
| System stats | `github.com/shirou/gopsutil/v4` |
| Proxmox | `github.com/luthermonson/go-proxmox` |
| GitLab | `gitlab.com/gitlab-org/api/client-go` |
| Calendar | `github.com/emersion/go-webdav` (CalDAV), `github.com/arran4/golang-ical` (ICS) |
| Service install | `github.com/kardianos/service` (systemd, launchd, Windows service) |
| Tray icon | `fyne.io/systray` |
| Logging / HTTP / JSON | stdlib `log/slog` (structured), `net/http` (method + path route patterns), `encoding/json` |
| Tests | stdlib `testing`, `net/http/httptest`, `github.com/santhosh-tekuri/jsonschema/v6` |
| Release | GoReleaser |

## Directory layout
```
host/
  go.mod
  cmd/
    buddyd/              daemon: run, install/uninstall, config check
    buddyctl/            CLI client for the control API
    buddysim/            simulator: `buddysim device` (fake ESP32) / `buddysim host` (fake buddyd)
  internal/
    config/              host settings only (ports, proxy, log level, allowed sources)
    protocol/            message types, NDJSON reader/writer (2 KB line limit), version check
    session/             one per connected device: subscriptions, scheduler, send queue
    fetch/               shared HTTP client (proxy, timeouts, retries), dedupe cache
    control/             HTTP API on 127.0.0.1 + token, SSE event stream
    transport/
      serial/            auto-detect by VID/PID, reconnect on unplug, DTR/RTS off
      tcp/               listener :7531, optional token, zeroconf advert
    source/
      source.go          interface + registry
      time/              epoch from the host clock (NTP-synced by the OS)
      weather/           Open-Meteo (current, hourly rain, 3-day, sunrise/sunset)
      aqi/               Open-Meteo air quality
      http/              generic: GET a URL, pick values via JSON Pointer
      gitlab/            pipelines for projects, MRs waiting for review
      calendar/          CalDAV / ICS: next meeting + countdown
      proxmox/           node load, VMs/CTs up/down, storage, last backup status
      homeassistant/     selected entity states (REST)
      sys/               gopsutil: CPU, RAM, temps, battery of this host
    service/             kardianos/service wrapper, user autostart
    tray/                tray menu (build tag `tray`)
  deploy/
    Dockerfile           FROM scratch, CGO_ENABLED=0, -tags notray
    docker-compose.yml
    99-deskbuddy.rules   optional udev rule (Linux: uaccess + /dev/deskbuddy symlink)
  .goreleaser.yaml
```

## Core design

### Session flow
```
device                                   buddyd
  │ hello {dev, fw, caps}                  │
  │──────────────────────────────────────►│
  │      hello {host, ver, srcs, net}      │   srcs = sources this host offers
  │◄──────────────────────────────────────│   net  = host has internet right now
  │ sub {sid:"wx", src:"weather", every:900, p:{lat,lon,days}}
  │ sub {sid:"ci", src:"gitlab",  every:60,  p:{url,projects,token}}
  │──────────────────────────────────────►│
  │      ack / err {code:"unknown_src"}    │
  │      weather …   data {ci.*} …         │   first result right away, then every N s
  │◄──────────────────────────────────────│
  │      sub_state {sid:"ci", state:"error", msg:"HTTP 401"}
  │ unsub {sid:"ci"}                       │
```
- The session holds the subscriptions **in memory only**. On disconnect they're gone.
  On reconnect the device sends them again. Nothing is written to disk.
- Each subscription has its own timer (interval from the device, clamped to a per-source
  minimum, e.g. weather ≥ 5 min, gitlab ≥ 30 s), jitter, timeout and backoff on errors.
  One failing source never affects the others.
- Results go out as `weather`, `time` or `data` messages. `data` keys are prefixed with
  the subscription id (`sid`), e.g. `ci.main`, so the device decides the namespace.
- Source health is reported with `sub_state` (ok / error / backoff), so the device can show it.
- **Dedupe:** if two devices subscribe to the same source with identical parameters, the
  fetch is shared (short in-memory cache keyed by a hash of source + params).
- When the host's internet state changes, it sends `host_state {net:false|true}`. The
  device can then fetch sources itself over WiFi or show them as stale.

### Sources
```go
type Source interface {
    Name() string
    MinInterval() time.Duration
    Validate(params json.RawMessage) error            // reject bad subs with err
    Fetch(ctx context.Context, params json.RawMessage) (Result, error) // weather/time/data
}
```
| Source | Params (from the device) | Output |
|---|---|---|
| `time` | – | `time` (epoch only; the device has its own TZ table) |
| `weather` | `lat`, `lon`, `days` | `weather` |
| `aqi` | `lat`, `lon` | `data` `<sid>.pm25`, `<sid>.index` |
| `http` | `url`, `headers?`, `pick: {key: "/json/pointer"}` | `data` |
| `gitlab` | `url`, `token`, `projects[]`, `mrs?` | `data` + optional `page` |
| `calendar` | `ics_url` or `caldav{url,user,pass}` | `data` `<sid>.next`, `<sid>.next_in` |
| `proxmox` | `url`, `token_id`, `token_secret`, `insecure_tls?` | `data` `<sid>.vms_up` … |
| `ha` | `url`, `token`, `entities[]` | `data` |
| `sys` | – | `data` CPU/RAM/temp/battery of this host |

- The `http` source makes most "new data" a pure device-config change:
  `{"src":"http","sid":"btc","every":300,"p":{"url":"https://…","pick":{"eur":"/bpi/EUR/rate_float"}}}`
  → `data {"btc.eur": 61234.5}`. Response size limit 1 MB, only JSON.
- **Secrets** (tokens, passwords) arrive in `sub` params, are kept in memory only and are
  never logged (params are masked in logs and in the control API).
- **LAN vs internet:** `proxmox` and `ha` are LAN services, not internet. They are still
  sources because a device on USB has no network at all.

### Proxy and network
- The shared HTTP client uses `HTTPS_PROXY`/`NO_PROXY`, and on Windows/macOS also the
  system proxy settings (incl. PAC) when no env vars are set.
- Optional custom CA bundle for corporate TLS interception (`tls.ca_file`).
- Internet check every 60 s (HEAD to the Open-Meteo API) → `net` flag in `host_state`.

## Host configuration (optional)
Only settings about the host itself. Defaults work, so most installs have no file.
Layered: **defaults → `config.yaml` → `BUDDYD_*` env → flags**.

| OS | Path |
|---|---|
| Windows | `%AppData%\buddyd\config.yaml` |
| macOS | `~/Library/Application Support/buddyd/config.yaml` |
| Linux | `~/.config/buddyd/config.yaml` or `/etc/buddyd/config.yaml` |
| Container | `/config/config.yaml`, or env vars only |

```yaml
serial:  {enabled: true, port: auto}           # auto = VID/PID; or COM5 / /dev/cu.usbmodem1101
tcp:     {enabled: false, listen: ":7531", token: ${env:BUDDY_TOKEN}}   # token devices must send
control: {listen: 127.0.0.1:7532}              # token generated on first start
proxy:   {url: "", ca_file: ""}                # empty = env / system settings
sources: {deny: [sys]}                         # e.g. don't expose host stats from the container
log:     {level: info}
```
- `BUDDYD_TCP_ENABLED=true` etc. is enough for Docker Compose.
- `buddyd config check` prints the merged settings.
- Device settings never go here. Configuring the device is done on the device itself (see the
  [firmware plan](../firmware/PLAN.md#configuration)). `buddyd` can only forward a
  `config` message from `buddyctl`, as a convenience.

## Control API and `buddyctl`
HTTP + JSON on `127.0.0.1:7532`, `Authorization: Bearer <token>` (token file next to the
config, user-only permissions). For scripts, CI jobs, Stream Deck, tray.

| Endpoint | Purpose |
|---|---|
| `POST /v1/notify` | `{"text","icon?","mood?","sound?","dev?"}` |
| `POST /v1/mood` | `{"mood","ttl","dev?"}` |
| `POST /v1/data` | push `user.*` keys `{"kv":{…},"ttl?"}` (lost when the device disconnects) |
| `PUT/DELETE /v1/pages/{name}` | temporary custom pages |
| `GET /v1/devices` | connected devices, transport, firmware, active subscriptions (secrets masked) |
| `POST /v1/devices/{id}/config` | forward a `config` message, returns the device `ack`/`err` |
| `GET /v1/events` | Server-Sent Events: device logs, events, sensor readings (`buddyctl monitor`) |
| `GET /v1/status` | transports, internet/proxy state, per-subscription health |

`buddyctl`: `notify TEXT [--mood M] [--sound] [--dev ID]`, `mood NAME [--ttl S]`,
`data KEY=VALUE…`, `page NAME --json FILE`, `devices`, `monitor`, `status`,
`device config KEY=VALUE…`. With `--direct` it talks to a USB device without a running daemon.

## Desktop integration
- **Tray icon** (Windows, macOS, Linux with StatusNotifier): device connected, internet/proxy
  state, subscription errors, "Configure device…" (opens the device's setup page), "Logs", "Quit".
- **Autostart:** on desktops `buddyd` runs **as the user, not as a system service**, because a
  Windows service runs in session 0 and can't show a tray icon (it also wouldn't see the
  user's proxy settings). `buddyd install --user` creates a Windows Startup entry, a macOS
  LaunchAgent or a systemd user unit.
- **Headless (container):** `buddyd install` registers a system service, or run the Docker
  image. Built with `-tags notray`.
- **Serial specifics:**
  - Windows: COM numbers change, so always auto-detect.
  - macOS: use `/dev/cu.*`, never `tty.*`.
  - Linux: user needs `dialout`, or install the optional udev `uaccess` rule.
  - CH340 boards (Cheap Yellow Display) may need a driver on Windows and older macOS.
    The ESP32-S3's native USB-CDC needs none.
  - DTR/RTS are set low on open so the board doesn't reset. Unplug/replug is detected
    and the port reopened.

## Build and release
- `CGO_ENABLED=0` everywhere except **macOS with the tray** (`fyne.io/systray` needs cgo
  there), which builds on a macOS GitHub runner.
- GoReleaser outputs:
  - archives for `windows/amd64,arm64`, `darwin` universal, `linux/amd64,arm64,armv7`
  - `.deb`/`.rpm`
  - multi-arch Docker image on `ghcr.io` (`notray`)
  - Homebrew tap, Scoop bucket (winget later)
- Unsigned binaries trigger Gatekeeper / SmartScreen warnings. Document the workaround
  (Homebrew/Scoop installs avoid most of it). Code signing and notarization are an
  M10 option.

## Deployment
- **Laptop/desktop:** install via Homebrew/Scoop/.deb or unpack the archive, then
  `buddyd install --user`. Done. No setup needed.
- **Proxmox:** `deploy/proxmox/buddyd-lxc.sh` on the Proxmox host creates a Debian LXC
  (13, or 12 if 13 is not offered) and runs `buddyd install` in it; the binary comes from
  the latest GitHub release. Devices find it via mDNS, or use a fixed IP / DHCP reservation.
  Later also Docker Compose with `BUDDYD_TCP_ENABLED=true`.

## Milestone breakdown
| Main | Host tasks |
|---|---|
| M0 | Go module skeleton, cobra commands, `go test` + golangci-lint + GoReleaser snapshot in CI (all three OSes) |
| M3 | protocol pkg + golden tests, session + subscription scheduler, serial transport, `time` + `weather` + `sys` sources, proxy-aware HTTP client, control API, `buddyctl notify`, `buddysim device` |
| M5 | TCP server + zeroconf + token, Docker image, `buddyd install` (service + `--user`), `host_state` / internet check |
| M7 | tray icon |
| M8 | `http`, `gitlab`, `calendar`, `proxmox`, `aqi` sources; dedupe cache; temporary pages |
| M9 | `ha` source |
| M10 | Homebrew/Scoop/.deb publishing, optional signing/notarization |

## Testing
- Unit tests: protocol against the golden files in `protocol/examples/` (and the JSON
  Schemas), subscription scheduler (intervals, clamping, backoff, unsub on disconnect),
  JSON Pointer picking, secret masking.
- Sources against recorded HTTP responses (`httptest` servers serving fixture files),
  including proxy and error cases.
- `buddysim device` emulates the firmware (NVS, SourceRouter, DataStore, MoodEngine, pages)
  and shows the display in the browser. It runs over TCP or a PTY (Linux/macOS). The same
  code runs in `go test` against a real session over TCP (`internal/sim/sim_test.go`).
- CI runs `go test -race ./...` on Linux, macOS and Windows runners.
