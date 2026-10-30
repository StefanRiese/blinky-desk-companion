# Host Service Plan (`buddy-link`)

Part of the [main plan](../PLAN.md). Protocol details: [protocol/PROTOCOL.md](../protocol/PROTOCOL.md).

## Role
`buddy-link` is a **stateless internet gateway** for the device. The device owns all
configuration (what to show, which sources, locations, tokens, intervals) and tells the
host what to fetch. The host only does the fetching the device can't do itself because
its WiFi has no internet (or there is no WiFi at all).

Consequences:
- **Swap the host anytime.** Plug the buddy into another laptop, or let it connect to the
  Proxmox container. As soon as it connects, the device sends its subscriptions and
  everything works again. Nothing needs to be set up on the new host.
- **Zero config by default.** `buddy-link` runs without a config file. The few host settings
  that exist are about the host itself (ports, proxy, log level).
- **No duplicated logic.** Sources the device can also fetch itself over WiFi (time,
  weather, simple HTTP JSON) produce the same keys either way, so the UI doesn't care
  who fetched them.

## Status (2026-10-30)
First implementation in this folder, see [README.md](README.md). Needs Go 1.27 (the module
says `go 1.27.0`), cross-compiles without cgo for Windows, macOS and Linux. CI
([`.github/workflows/host.yml`](../.github/workflows/host.yml)) runs gofmt, vet and
`go test -race` on Linux, Windows and macOS, then builds all 7 OS/arch binaries.

| Done | Not yet |
|---|---|
| protocol package + golden examples, session (handshake, subscriptions, keepalive, request/reply) | |
| serial transport (VID/PID auto-detect, PTY/explicit port), TCP transport + token, per-device tokens (`tcp.tokens`) | |
| sources `time`, `weather`, `aqi`, `geo`, `http`, `sys`, shared fetch cache | sources `gitlab`, `calendar`, `proxmox`, `ha` |
| control API + `buddy-ctl` (notify, mood, data, page, devices, device config, monitor, status); `--direct` for devices, device config, ports and flash over USB without buddy-link | |
| tray icon (Windows notification area, macOS menu bar): version, status line, web UI, config folder, log, Start at login, Close; console hidden on double-click | |
| `Dockerfile` (distroless, multi-arch) and a Compose file that builds it (no published image) | |
| mDNS advert `_blinky._tcp` (hashicorp/mdns), `buddy-link install` (Linux/systemd), `install --user` (start at login on Windows, macOS, Linux), Proxmox LXC script, release workflow | |
| host config (YAML + env + flags, `${env:}`/`${file:}`); proxy from `proxy.url`, env vars or the Windows/macOS settings | GoReleaser (open: the hand-written release workflow covers the Mac app and firmware) |
| CI: test on Linux/Windows/macOS, golangci-lint, cross-build artifacts | |
| `buddy-sim device`: emulator with browser UI (display, SourceRouter, NVS, failover toggles); `buddy-sim host`: stand-in host with fixed data for firmware tests | |
| web UI (`internal/control/web`): embedded page on the control port, login link with the token (cookie), `buddy-link --gui`, `buddy-ctl gui`, tray "Open Buddy Link…"; lists the connected devices and edits their settings from the device's schema (`config` `schema_from`); links to each device's own page (`web` message); the header, a device's header, its save bar and section menu stay in place while scrolling (on phones only the header and the save bar); "Buddy Link settings" edits TCP, USB, proxy and log level, written to the config file and applied at once; `gui.listen` + `gui.password` for the network (password login, session cookie); offline devices for 24 h (`/v1/devices?all=1`); sources editor | |
| firmware update through `buddy-link` (`flasher.Updater`): `buddy-ctl device update --dev ID [--version V] [FILE.bin]`, `POST /v1/devices/{id}/update`, the release app image (`blinky-app-<board>.bin`) as `ota` messages over USB or TCP, then waits for the device to come back with the new version; refuses firmware before 0.2.0 at once (it has no `ota`; the device announces `ota` in `caps` since then) | test on real boards |
| "Flash a new device" in the web UI (port, board, release or `.bin`, progress), tray "Flash new device…"; "Update to vX" per device (newest release with an app image) and from a `.bin`, tray "Update firmware…"; endpoints `/v1/ports`, `/v1/firmware/releases`, `/v1/flash`, `/v1/devices/{id}/update`, `/v1/jobs/{id}` | |
| firmware images downloaded on demand from the GitHub releases (`internal/firmware`), checked against the SHA-256 GitHub lists, newest per file kept in `<user cache>/buddy-link/firmware`; `--version`, `FILE.bin` offline; the rolling pre-release `latest-main` ("Latest build from main", `--version main`), never picked as "latest"; first one published 2026-10-30 (0.2.1-dev.20+4efd9d8, 17 files) | try "Update to the latest build from main" on a real Blinky |
| update offers (`firmware.OffersFor`, `Server.WatchReleases`): the release list every 10 min, compared per device by semantic versioning: newer release, newer pre-release, the build from main (`on_main`, `main_newer`); `updates` in `/v1/devices`, buttons on the cards and pages, a banner over the device list, "Update firmware… (N)" in the tray | |
| first flash over USB (`internal/espflash`: the ROM loader protocol in Go, zlib blocks, MD5 check; `internal/flasher`): `buddy-ctl flash --board B [--port P] [--version V] [FILE.bin]`, also `--direct`; the serial transport lets go of the port meanwhile (`serial.Hold`); turns off the RTC watchdog over USB-Serial-JTAG (as esptool); keeps the settings (skips `nvs` and `nvs_keys` from the image's partition table; `--erase-settings`, checkbox in the web UI). Works on the AMOLED-1.75 over its own USB port (2026-10-29) | test on the LCD-1.46 and on a board with a USB-UART bridge |
| | mDNS: the advert runs on one interface (the system's choice, on a laptop with VMware maybe a virtual adapter) and lists every local address; announce per interface with that interface's address (and the firmware should take the address in its own subnet) |
| | Windows: `install --user` could add firewall rules for TCP 7531 and UDP 5353 (private networks); a blocked port shows only as "no answer" on the device |
| | Opening the serial port reads a burst of old hellos the device sent while no host listened; each starts a session (about 18 in a second). Ignore a burst like that |

## Goals
- Runs on **Windows, macOS and Linux** (laptop/desktop) and in a **Proxmox LXC / Docker
  container**. One static binary per OS/arch, no runtime to install.
- Serves devices over **USB serial** and **TCP** at the same time, any number of devices.
- Offers a fixed set of **sources** (weather, GitLab, calendar, …) plus a generic `http`
  source, so new data needs a device config change, not a host update.
- Works behind corporate proxies (the usual reason the laptop has internet and the
  device's WiFi doesn't).
- Small extras that only make sense on a host: its own system stats (as a source the device
  can subscribe to) and `buddy-ctl notify` for scripts.

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
    buddy-link/              daemon: run, install/uninstall, config check
    buddy-ctl/            CLI client for the control API
    buddy-sim/            simulator: `buddy-sim device` (fake ESP32) / `buddy-sim host` (fake buddy-link)
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
    tray/                tray icon and menu (Windows; macOS with cgo)
  deploy/
    Dockerfile           distroless, CGO_ENABLED=0 (Linux: no tray)
    docker-compose.yml
    99-deskbuddy.rules   optional udev rule (Linux: uaccess + /dev/deskbuddy symlink)
  .goreleaser.yaml
```

## Core design

### Session flow
```
device                                   buddy-link
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
| `geo` | – | `data` `geo.lat`, `geo.lon`, `geo.city`, `geo.tz`: the location by public IP address (ipinfo.io); the device subscribes by itself while `location.auto` is on |
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
Layered: **defaults → `config.yaml` → `BUDDY_LINK_*` env → flags**.

| OS | Path |
|---|---|
| Windows | `%AppData%\buddy-link\config.yaml` |
| macOS | `~/Library/Application Support/buddy-link/config.yaml` |
| Linux | `~/.config/buddy-link/config.yaml` or `/etc/buddy-link/config.yaml` |
| Container | `/config/config.yaml`, or env vars only |

```yaml
serial:  {enabled: true, port: auto}           # auto = VID/PID; or COM5 / /dev/cu.usbmodem1101
tcp:     {enabled: false, listen: ":7531", token: ${env:BUDDY_TOKEN}}   # token devices must send
control: {listen: 127.0.0.1:7532}              # token generated on first start
proxy:   {url: "", ca_file: ""}                # empty = env / system settings
sources: {deny: [sys]}                         # e.g. don't expose host stats from the container
log:     {level: info}
```
- `BUDDY_LINK_TCP_ENABLED=true` etc. is enough for Docker Compose.
- `buddy-link config check` prints the merged settings.
- The web UI ("Buddy Link settings") edits a few of them: TCP (on/off, address, token, mDNS),
  USB (on/off, port), proxy and log level. They are written to the config file (comments
  kept) and the affected link restarts at once; a busy address is refused and changes
  nothing. Settings set by a flag, an env var or a `${…}` reference are locked there.
- Device settings never go here. Configuring the device is done on the device itself (see the
  [firmware plan](../firmware/PLAN.md#configuration)). `buddy-link` can only forward a
  `config` message from `buddy-ctl`, as a convenience.

## Control API and `buddy-ctl`
HTTP + JSON on `127.0.0.1:7532`, `Authorization: Bearer <token>` (token file next to the
config, user-only permissions). For scripts, CI jobs, Stream Deck, tray.

| Endpoint | Purpose |
|---|---|
| `POST /v1/notify` | `{"text","icon?","mood?","sound?","dev?"}` |
| `POST /v1/mood` | `{"mood","ttl","dev?"}` |
| `POST /v1/data` | push `user.*` keys `{"kv":{…},"ttl?"}` (lost when the device disconnects) |
| `PUT/DELETE /v1/pages/{name}` | temporary custom pages |
| `GET /v1/devices` | connected devices, transport, firmware, active subscriptions (secrets masked); `?all=1` also the ones offline in the last 24 h (`online: false`, `gone_at`) |
| `POST /v1/devices/{id}/config` | forward a `config` message, returns the device `ack`/`err` |
| `GET /v1/devices/{id}/settings` | the device's settings schema and all values (secrets masked), read group by group; 501 for firmware without `schema_from` |
| `GET/PUT /v1/host/settings` | buddy-link's own editable settings (`{"set":{…}}`), written to the config file and applied at once |
| `GET /`, `GET /login?token=…` | the web UI; the login link sets a cookie (`SameSite=Strict`, 30 days). Requests with the cookie that change something must come from the page itself (`Origin`) |
| `GET /v1/events` | Server-Sent Events: device logs, events, sensor readings (`buddy-ctl monitor`) |
| `GET /v1/status` | transports, internet/proxy state, per-subscription health |
| `GET /v1/ports` | USB serial ports with VID/PID and the Blinky connected on each |
| `GET /v1/firmware/releases?board=B` | releases with a full image (first flash) and/or an app image (update) for that board |
| `POST /v1/flash` | first flash: `{"port","board","version?"}`, or the image as the body (`?port=&board=`); answers with a job |
| `POST /v1/devices/{id}/update` | update over the link: `{"version?"}`, or the app image as the body; answers with a job |
| `GET /v1/jobs/{id}` | a flash or update job: state, step, progress, log |

`buddy-ctl`: `notify TEXT [--mood M] [--sound] [--dev ID]`, `mood NAME [--ttl S]`,
`data KEY=VALUE…`, `page NAME --json FILE`, `devices`, `monitor`, `status`,
`device config KEY=VALUE…`, `device update --dev ID [--version V] [FILE.bin]`, `ports`,
`flash --board B [--port P] [--version V] [FILE.bin]`, `gui [--print]` (opens the web UI,
logged in). With `--direct`, `devices`, `device config`, `ports` and `flash` talk to a USB
device without a running buddy-link.

## Desktop integration
- **Tray icon** (Windows, macOS, Linux with StatusNotifier): device connected, internet/proxy
  state, subscription errors, "Open Buddy Link…" (the web UI), "Logs", "Quit".
- **Autostart:** on desktops `buddy-link` runs **as the user, not as a system service**, because a
  Windows service runs in session 0 and can't show a tray icon (it also wouldn't see the
  user's proxy settings). `buddy-link install --user` creates a Windows Startup entry, a macOS
  LaunchAgent or a systemd user unit.
- **Headless (container):** `buddy-link install` registers a system service, or run the Docker
  image. Linux builds have no tray (`--tray` exists on Windows and macOS only).
- **Serial specifics:**
  - Windows: COM numbers change, so always auto-detect.
  - macOS: use `/dev/cu.*`, never `tty.*`.
  - Linux: user needs `dialout`, or install the optional udev `uaccess` rule.
  - CH340 boards (Cheap Yellow Display) may need a driver on Windows and older macOS.
    The ESP32-S3's native USB-CDC needs none.
  - DTR/RTS are set low on open so the board doesn't reset. Unplug/replug is detected
    and the port reopened.

## Build and release
- Today: the hand-written `.github/workflows/release.yml`. A tag `vX.Y.Z` publishes a
  release (pre-release if its CHANGELOG heading says so); every push to main that changes
  the firmware, the host or the protocol publishes the same files as the rolling
  pre-release `latest-main`, versioned `<next patch>-dev.<commits since the tag>+<commit>`
  (a newer push cancels an older build). Its tag moves and the files are replaced in place;
  if main moved on during the build, the tag goes to main's newest commit when the code is the
  same (GitHub refuses to tag a commit whose workflow files differ from main's, 403), else the
  newer build publishes. No workflow runs for changes to Markdown files alone.
  The pages workflow puts the newest real release on the setup page, split around the
  settings partitions. The plan below (GoReleaser, packages) is still open.
- `CGO_ENABLED=0` everywhere except **macOS with the tray** (`fyne.io/systray` needs cgo
  there), which builds on a macOS GitHub runner.
- GoReleaser outputs:
  - archives for `windows/amd64,arm64`, `darwin` universal, `linux/amd64,arm64,armv7`
  - `.deb`/`.rpm`
  - Homebrew tap, Scoop bucket (winget later)
- Unsigned binaries trigger Gatekeeper / SmartScreen warnings. Document the workaround
  (Homebrew/Scoop installs avoid most of it). Code signing and notarization are an
  M10 option.

## Deployment
- **Laptop/desktop:** install via Homebrew/Scoop/.deb or unpack the archive, then
  `buddy-link install --user`. Done. No setup needed.
- **Proxmox:** `deploy/proxmox/buddy-link-lxc.sh` on the Proxmox host creates a Debian LXC
  (13, or 12 if 13 is not offered) and runs `buddy-link install` in it; the binary comes from
  the latest GitHub release. Devices find it via mDNS, or use a fixed IP / DHCP reservation.
  Later also Docker Compose with `BUDDY_LINK_TCP_ENABLED=true`.

## Milestone breakdown
| Main | Host tasks |
|---|---|
| M0 | Go module skeleton, cobra commands, `go test` + golangci-lint + GoReleaser snapshot in CI (all three OSes) |
| M3 | protocol pkg + golden tests, session + subscription scheduler, serial transport, `time` + `weather` + `sys` sources, proxy-aware HTTP client, control API, `buddy-ctl notify`, `buddy-sim device` |
| M5 | TCP server + zeroconf + token, Docker image, `buddy-link install` (service + `--user`), `host_state` / internet check |
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
- `buddy-sim device` emulates the firmware (NVS, SourceRouter, DataStore, MoodEngine, pages)
  and shows the display in the browser. It runs over TCP or a PTY (Linux/macOS). The same
  code runs in `go test` against a real session over TCP (`internal/sim/sim_test.go`).
- CI runs `go test -race ./...` on Linux, macOS and Windows runners.
