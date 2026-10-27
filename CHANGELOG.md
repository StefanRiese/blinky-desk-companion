# Changelog

## Unreleased

- **macOS:** Buddy Link comes as an app, `Buddy-Link-macos-universal.zip` with **Buddy
  Link.app** (Apple Silicon and Intel, with icon, menu bar only). It replaces
  `buddyd-macos-universal`; the program is inside the app (`Contents/MacOS/buddyd`).
- **Windows:** `buddyd.exe` never opens a console window; it only shows its tray icon and logs
  to `buddyd.log` next to the config. From a terminal it still prints there.
- **Tray menu:** **Open config folder** (config, control token, log) and **Show log**, on
  Windows and macOS.

## v0.1.3 – 2026-10-26 (pre-release)

A pre-release: the new tray icon hasn't been tried on real Windows and macOS desktops yet.
The Proxmox script keeps installing the latest regular release (v0.1.2).

### New names and a new address

- The device is called **Blinky**, the host app (`buddyd`) **Buddy Link**. Program names,
  files, the systemd service and the config folders stay as they were.
- The repository moved to
  [github.com/StefanRiese/blinky-desk-companion](https://github.com/StefanRiese/blinky-desk-companion).
  Old links and clones are redirected; the Go module path is now
  `github.com/StefanRiese/blinky-desk-companion/host`.
- The setup page is now at
  <https://stefanriese.github.io/blinky-desk-companion/> (the old address no longer works).

### Buddy Link

- **Tray icon:** on Windows in the notification area (lower right of the taskbar), on macOS in
  the menu bar. The menu shows the version and has **Close**. `--tray=false` turns it off.
- **Windows:** `buddyd.exe` has its own icon. Started by a double-click, it hides its console
  window and logs to `buddyd.log` next to the config file.
- **macOS:** `buddyd-macos-universal` is now built on a Mac, which the menu bar needs.

### Downloads

- The emulator `buddysim` is no longer part of the downloads; it's a development tool. Build
  it from source: `cd host && go build -o bin/ ./cmd/buddysim`.

### Docker

- No image is published any more, and `ghcr.io/stefanriese/buddyd` was removed.
  [`deploy/docker/compose.yml`](deploy/docker/compose.yml) builds the image from the
  repository: `docker compose up -d`, and `docker compose up -d --build` to update.

### Firmware

- First firmware for the Waveshare ESP32-S3-Touch-LCD-1.46: eyes with 16 moods, dashboard,
  countdown and alarm pages, settings in flash, SHT40 and RTC drivers, and the USB link to
  Buddy Link and the setup page.
- **Download:** `blinky-firmware-ws146.bin`, one image to flash at offset 0. The
  [setup page](https://stefanriese.github.io/blinky-desk-companion/) installs it from the
  browser (Chrome or Edge).
- It builds in CI but **hasn't run on a board yet**.

## v0.1.2 – 2026-10-26

- **Docker image** `ghcr.io/stefanriese/buddyd` for amd64, arm64 and arm/v7: small
  (distroless Debian 13), runs as non-root, TCP on, settings via `BUDDYD_*` variables,
  `buddyctl` included. Compose file in `deploy/docker/compose.yml`.

## v0.1.1 – 2026-10-26

### macOS

- **One download for every Mac:** `buddyd-macos-universal`, `buddyctl-macos-universal`
  and `buddysim-macos-universal` run on Apple Silicon (M1–M5) and Intel Macs.
- The Mac files are now named `macos` instead of `darwin` (Go's internal name for macOS).
  Only the universal file is published; the separate Intel and Apple Silicon files are gone.
- The files aren't signed by Apple yet. macOS blocks the first start: allow it once under
  *System Settings → Privacy & Security → Open Anyway*, or run
  `xattr -d com.apple.quarantine buddyd-macos-universal`.

No other changes since v0.1.0.

## v0.1.0 – 2026-10-26

First release of the host tools. The ESP32 firmware isn't written yet. Everything a device
does can already be tried with the emulator `buddysim`, and the setup page and protocol are
ready for the firmware.

### Downloads

One file per program and platform, no installation needed:

| Program | What it is |
|---|---|
| `buddyd` | Host service: fetches weather, air quality and other data for the buddies, over USB or the network |
| `buddyctl` | Command line for a running `buddyd`: notifications, moods, data, custom pages, device settings |
| `buddysim` | Device emulator with a browser UI, for trying everything without hardware |

Builds for Windows, macOS and Linux on amd64 and arm64, plus Linux arm (32-bit, e.g. older
Raspberry Pis). File names are `<program>-<os>-<arch>`, e.g. `buddyd-windows-amd64.exe` or
`buddyd-linux-arm64`. `SHA256SUMS` lists the checksums.

### Highlights

- **`buddyd` – the host service**
  - Serves any number of buddies over USB serial (found automatically) and TCP.
  - Data sources: time, weather and air quality (Open-Meteo, no API key), any JSON API
    (`http` source), and the host's own CPU/RAM/temperature.
  - The device owns all its settings, so you can plug a buddy into any computer: `buddyd`
    stores nothing about it.
  - Proxy support (`HTTPS_PROXY` or `proxy.url`), optional device token, local control API.
- **Running on a server**
  - `sudo buddyd install` sets it up as a systemd service on Linux; `buddyd uninstall` removes it.
  - mDNS: devices find `buddyd` on the network by themselves (host link `auto`).
  - Proxmox: `deploy/proxmox/buddyd-lxc.sh` creates a small Debian container with
    `buddyd` in one command.
- **Setup page** at <https://stefanriese.github.io/blinky-desk-companion/>
  - WiFi, location with place search, host link, units, brightness, pages, alarms, sound
    reactions, mood thresholds and data sources, over USB in Chrome or Edge.
  - Firmware flashing is prepared and switches on once firmware is published.
- **What the buddy can do** (in the emulator now, in the firmware later)
  - Mood eyes; pages for clock & weather, forecast, indoor climate, host data, countdown
    and alarm, plus custom pages sent by a host.
  - Countdown (swipe to set, shake to start), alarm clock with weekdays and snooze.
  - Reactions to sound, using the volume level only (startled, curious, grumpy when loud).
  - Display units (°C/°F, km/h/mph/m/s, mm/in), day/night/dimmed brightness, choice and
    order of pages.

### Getting started

- **Laptop:** download `buddyd` for your system and start it; plug the buddy in over USB.
- **Proxmox:** on the Proxmox host, run
  `bash -c "$(curl -fsSL https://raw.githubusercontent.com/StefanRiese/blinky-desk-companion/main/deploy/proxmox/buddyd-lxc.sh)"`.
- **No hardware yet:** start `buddyd --tcp 127.0.0.1:7531` and
  `buddysim device --host 127.0.0.1:7531 --open`; see the README section "Testing without hardware".

### Known limitations

- No firmware for the ESP32 yet, so nothing to flash.
- `buddyd install` is Linux-only; on Windows and macOS start `buddyd` by hand (autostart
  is planned).
- The Proxmox script and `buddyd install` on a real system haven't had a first run on real
  hardware yet: please report problems as an issue.
- The macOS builds are not signed, so macOS asks before running them the first time.
- Not built yet: GitLab, calendar, Proxmox and Home Assistant data sources, Docker image,
  tray icon, MQTT.
