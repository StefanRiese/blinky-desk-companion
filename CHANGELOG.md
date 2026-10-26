# Changelog

## v0.1.1 – 2026-10-26

### macOS

- **One download for every Mac:** `buddyd-macos-universal`, `buddyctl-macos-universal`
  and `buddysim-macos-universal` run on Apple Silicon (M1–M5) and Intel Macs.
- The Mac files are now named `macos` instead of `darwin` (Go's internal name for macOS):
  `<program>-macos-arm64`, `<program>-macos-amd64` and `<program>-macos-universal`.
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
- **Setup page** at <https://stefanriese.github.io/esp32_desk_buddy/>
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
  `bash -c "$(curl -fsSL https://raw.githubusercontent.com/StefanRiese/esp32_desk_buddy/main/deploy/proxmox/buddyd-lxc.sh)"`.
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
