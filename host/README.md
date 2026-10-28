# Blinky host tools

<img src="assets/buddyd.png" width="96" alt="buddyd icon" align="right">

Go module with three programs (plan: [PLAN.md](PLAN.md), protocol: [../protocol/PROTOCOL.md](../protocol/PROTOCOL.md)):

| Program | What it does |
|---|---|
| `buddyd` | **Buddy Link**, the host service. A stateless internet gateway: devices connect over USB serial or TCP, send their subscriptions, and `buddyd` fetches the data and sends it back. Stores nothing about the device. |
| `buddyctl` | CLI for a running `buddyd`: notifications, moods, data, custom pages, device settings, live monitor. |
| `buddysim` | ESP32 emulator with a browser UI. Shows the round 412×412 display (eyes, pages, notifications) and its internals (link, SourceRouter, DataStore, messages, NVS settings). For developing and testing without hardware. |

Runs on Windows, macOS and Linux. Needs Go ≥ 1.27 to build. How to try everything without
hardware: [Quick start without hardware](#quick-start-without-hardware).

## Build

```sh
cd host
go build -o bin/ ./cmd/...        # bin/buddyd, bin/buddyctl, bin/buddysim
go test -race ./...
```

Cross-compile (no cgo needed), e.g. `GOOS=windows GOARCH=amd64 go build -o bin/ ./cmd/...`
or `GOOS=darwin GOARCH=arm64 …`. Without cgo, macOS finds serial ports by name
(`/dev/cu.usbmodem*`, `cu.wchusbserial*`, …) instead of USB VID/PID.

## Quick start without hardware

```sh
# terminal 1: host service with TCP enabled (serial auto-detect stays on)
bin/buddyd --tcp 127.0.0.1:7531

# terminal 2: emulated device, connects to buddyd over TCP
bin/buddysim device --host 127.0.0.1:7531 --open
```

TCP is off by default. Without `--tcp`, turn it on in the web UI instead (`bin/buddyctl gui`,
then "Buddy Link settings"); it stays on in the config file.

Open <http://127.0.0.1:7600>. The emulated device subscribes to time, weather, air quality
and the host's system stats. Try:

- **Host link** checkbox: unplug/replug the device. Sources become unavailable, the eyes get sad.
- **Device WiFi has internet** checkbox: time, weather and AQI move from the host to the
  device itself (SourceRouter); `sys` stays on the host.
- Drag on the screen to swipe pages, click to tap ("boop"), **Shake** for dizzy eyes.
- **Settings (NVS)** tab: edit the device settings (sources, location, link) and save.
  Settings live in `buddysim-nvs.json`, like the NVS of a real device.

Then talk to the device through `buddyd`:

```sh
bin/buddyctl notify "Build done" --icon ci_ok --mood happy
bin/buddyctl mood angry --ttl 10
bin/buddyctl data user.coffee=3
bin/buddyctl page ci --json examples/page-ci.json
bin/buddyctl devices
bin/buddyctl device config --dev buddy-sim01 --get ui
bin/buddyctl device config --dev buddy-sim01 ui.night_from=23
bin/buddyctl monitor
```

### Emulating USB serial

A PTY pair works like a USB cable (Linux/macOS, needs `socat`):

```sh
socat -d -d pty,raw,echo=0,link=/tmp/ttyHOST pty,raw,echo=0,link=/tmp/ttyDEV &
bin/buddyd --serial /tmp/ttyHOST
bin/buddysim device --serial /tmp/ttyDEV --nvs sim-usb.json
```

### Several devices

Run `buddysim device` again with another `--nvs` file, `--dev` id and `--http` port.

### Data from any JSON API

Add an `http` source in the Settings tab; `pick` maps keys to JSON Pointers:

```json
{"sid": "btc", "src": "http", "every": 300,
 "p": {"url": "https://api.coinbase.com/v2/prices/BTC-EUR/spot", "pick": {"eur": "/data/amount"}}}
```

The value shows up as `btc.eur` on the Host page.

## buddyd (Buddy Link)

```
buddyd [run] [--config FILE] [--tcp ADDR] [--serial auto|PORT|off] [--log-level debug] [--tray=false]
buddyd config check
buddyd version
buddyd completion bash|zsh|fish|powershell
sudo buddyd install [--tcp :7531] [--serial auto|PORT|off]   # Linux system service
sudo buddyd uninstall [--purge]
```

All three programs have `--help` on every command. Logs are structured (`log/slog`,
`key=value` on stderr).

- **Serial:** on by default, auto-detects ESP32-S3 native USB (`303a:*`) and CH340/CH9102
  (`1a86:7523`, `1a86:55d4`). DTR/RTS are kept low so the board doesn't reset.
- **TCP:** off by default; `--tcp :7531` or `tcp.enabled: true`. Optional device token.
- **mDNS:** with TCP on, `buddyd` announces itself as `_blinky._tcp` (TXT `ver`, `proto`,
  `token=0|1`), so devices with `link.tcp_host = "auto"` find it. Off with `tcp.mdns: false`.
- **Without installing:** just run `buddyd` (e.g. `buddyd.exe` on a Windows laptop with the
  buddy on USB). Installing is only for a server that should run it all the time.
- **Tray icon:** on Windows Buddy Link shows its icon in the notification area (lower right of
  the taskbar), on macOS in the menu bar. Its menu (right click on Windows) shows the
  version and has **Open config folder** (config, control token, log), **Show log** (when it
  logs to a file) and **Close**, which stops `buddyd`. `--tray=false` turns the icon off.
  - **No console window:** the released `buddyd.exe` is a Windows GUI program
    (`-ldflags -H=windowsgui`), and on macOS it comes as **Buddy Link.app** (menu bar only,
    no Dock icon). Started that way it logs to `buddyd.log` next to the config file. Started
    from a terminal it prints there (`version`, `--help`, the log); on Windows the prompt
    returns at once, so stop it with **Close**.
  - The app is built by [`scripts/macos-app.sh`](scripts/macos-app.sh) on a Mac (the menu
    bar needs cgo; it joins the Intel and Apple Silicon builds). A plain `go build` gives a
    console program on Windows and no menu bar icon on macOS without cgo; Linux has no tray.
- **`buddyd install`** (Linux, systemd): copies the binary to `/usr/local/bin`, writes
  `/etc/buddyd/config.yaml` (kept on re-install), creates the `buddyd` user and the
  `buddyd` service and starts it. Run it again to update. `buddyctl` on that machine finds
  the service's token in `/etc/buddyd` by itself. Windows/macOS service install: not yet.
- **Docker Compose:** [`../deploy/docker/compose.yml`](../deploy/docker/compose.yml) builds
  the image from [`Dockerfile`](Dockerfile) (distroless Debian 13, non-root, amd64/arm64/arm/v7)
  and runs it; no image is published. TCP on, serial off, config and control token in the
  `/config` volume, settings via `BUDDYD_*` env vars, `buddyctl` included
  (`docker exec buddyd buddyctl devices`). Host networking for mDNS.
- **Proxmox:** [`deploy/proxmox/buddyd-lxc.sh`](../deploy/proxmox/buddyd-lxc.sh) creates a
  small Debian container and runs `buddyd install` in it (see the top-level README).
- **Control API:** `http://127.0.0.1:7532`, bearer token in `control.token` next to the
  config (created on first start). Endpoints: see [PLAN.md](PLAN.md#control-api-and-buddyctl).
- **Sources offered:** `time`, `weather` and `aqi` (Open-Meteo, no key), `http`, `sys`.
- **Proxy:** `HTTPS_PROXY`/`NO_PROXY` env vars or `proxy.url`; extra CA via `proxy.ca_file`.

The config file is optional and only about the host itself:

| OS | Path |
|---|---|
| Windows | `%AppData%\buddyd\config.yaml` |
| macOS | `~/Library/Application Support/buddyd/config.yaml` |
| Linux | `~/.config/buddyd/config.yaml` |

```yaml
serial:  {enabled: true, port: auto, vid_pid: ["303a", "1a86:7523"]}
tcp:     {enabled: false, listen: ":7531", token: ${env:BUDDY_TOKEN}, mdns: true}
control: {listen: 127.0.0.1:7532}
proxy:   {url: "", ca_file: ""}
sources: {deny: []}            # e.g. [sys] in a container
log:     {level: info}
```

Every key can be overridden with `BUDDYD_<SECTION>_<KEY>`, e.g. `BUDDYD_TCP_ENABLED=true`.
Values may reference `${env:NAME}` or `${file:PATH}`.

## Layout

```
cmd/buddyd, cmd/buddyctl, cmd/buddysim
internal/protocol    NDJSON messages, reader/writer, secret masking
internal/session     one device connection: handshake, subscriptions, keepalive
internal/source      time, weather, aqi, http, sys + shared fetch cache
internal/transport   serial (auto-detect), tcp
internal/control     local HTTP API
internal/config      host settings
internal/sim         device emulator (NVS, DataStore, SourceRouter, MoodEngine, pages, web UI)
assets/              buddyd icon (SVG, PNG, .ico), the buddy without the badge, and the
                     blinking README pictures (buddy-light/dark.gif); make_icon.py redraws them and the
                     cmd/buddyd/rsrc_windows_*.syso files that give buddyd.exe its icon
```
