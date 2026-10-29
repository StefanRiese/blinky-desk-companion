# Changelog

## Unreleased

### Renamed (existing setups need a change)

- **Buddy Link is now `buddy-link`** (was `buddyd`) on every system: `buddy-link.exe`,
  `buddy-link-linux-<arch>`, the Mac app's program, the Docker image and the Proxmox script
  (`deploy/proxmox/buddy-link-lxc.sh`). Everything named after it changed too, with no
  fallback to the old names:
  - config folder `buddy-link` (`%AppData%\buddy-link`, `~/Library/Application Support/buddy-link`,
    `~/.config/buddy-link`, `/etc/buddy-link`): move `config.yaml` and `control.token` over
    from the old `buddyd` folder, or a new token is made (log in to the web UI again);
  - environment variables `BUDDY_LINK_*` (were `BUDDYD_*`);
  - the Linux service and its user `buddy-link`: run `sudo buddyd uninstall --purge`, then
    `sudo buddy-link install`;
  - the host name devices see, `buddy-link@<computer>`.
- **`buddy-ctl` and `buddy-sim`** (were `buddyctl` and `buddysim`): the command line tool
  and the emulator follow the same naming. Scripts that call `buddyctl` need the new name.

### On the device

- **Turn the picture:** new setting `ui.rotation` (0, 90, 180 or 270 degrees clockwise) for
  a Blinky that stands on its side or with the cable at the top. Touch turns with it. In the
  setup page, the Buddy Link web UI and on `blinky.local`; the desktop simulator takes
  `--rotate 90`. The emulator keeps the setting but shows its picture unturned.
- **Page order and hidden pages on the device:** the firmware now follows `ui.pages` (setup
  page, Buddy Link web UI, `buddy-ctl device config`): only the chosen pages, in that order,
  with the info screen last. Swipe up goes home: the eyes, or the first page if they are
  hidden. A hidden alarm or countdown page still comes up while it rings.
- **Analog clock page:** a new page after the dashboard with hour, minute and second
  hands, 12 marks and the date. It is `clock` in the page list (`ui.pages`, shown by default; a
  page list saved before needs it switched on). In the emulator too.
- **Forecast, indoor trend and host data on the device:** three new pages, as in the
  emulator. The forecast shows today and the next two days (also when the device fetches
  the weather itself, which now asks Open-Meteo for the days too), the indoor page the
  temperature trend of the last half hour, the host data page the values from Buddy Link
  and scripts.
- **Custom pages on the device:** pages that scripts send through Buddy Link
  (`buddy-ctl page NAME --json FILE`) now show on the device after its own pages: a title
  and up to 8 widgets (value, bar, icon and text, list, sparkline), until they are removed
  or their `ttl` runs out.
- **Notifications on the device:** `buddy-ctl notify` (or any script through Buddy Link) now
  shows a box over every page with an icon and the text, sets its mood on the eyes, wakes
  the screen and plays a short chime when it has a sound. A tap or a shake hides it.
- **Countdown in the status bar:** a running countdown shows its time left on the other
  pages, and an alarm icon shows while an alarm is on.
- **Motion gestures:** with the QMI8658 IMU, shaking the Blinky works like the shake that a
  tap stood in for (dizzy eyes, start the countdown, stop an alarm), and a quick twist turns
  to the next or previous page. Both are reported to Buddy Link. The thresholds still need
  tuning on a real board.
- **Events and readings to Buddy Link:** the device now reports touches, page changes, the
  countdown (started, paused, reset, done, stopped) and alarms (ringing, snoozed, stopped)
  as `event` messages, and its indoor temperature, humidity and noise once a minute as
  `sensor` messages, as the emulator did. `buddy-ctl monitor` shows them.
- **Firmware updates over the link:** Buddy Link updates a connected Blinky over USB or TCP
  ("Update to vX" in the web UI, tray "Update firmware…", or `buddy-ctl device update --dev ID`),
  with the app image of the newest release or a `.bin` file. The device checks the image's
  SHA-256, restarts into it, and goes back to the old firmware if the new one doesn't come
  up. Releases now also have `blinky-app-<board>.bin`. Needs this firmware once over USB
  (for the new bootloader); not tested on real boards yet.
- **Idle eyes:** when nobody has touched the screen for 20 s, the eyes play little routines
  like the blinking GIF on the README: a slow look right and left, double blinks, a look
  up to one side. A touch brings back the usual small glances. In the emulator too.
- The eyes page no longer shows the mood's name and reason below the eyes; only the eyes
  show the mood.
- **Location by IP address:** new setting `location.auto` (on by default). The device, or
  Buddy Link when the device has no internet, looks up the location by the public IP
  address (ipinfo.io, no API key) when it gets internet and every 6 hours, and stores it as
  the location: weather, air quality and time zone follow. Choosing a place or changing a
  location field by hand turns it off. Devices with a location set before keep it.
  New source `geo` in Buddy Link and the emulator. Time zones the firmware's table doesn't
  know are not taken over.

### Buddy Link

- **Buddy Link web UI:** Buddy Link shows the connected Blinkies in the browser and edits
  their settings: grouped fields, the pages in swipe order, alarms, secrets masked, and the
  sources it fetches for each device. Open it with "Open Buddy Link…" in the tray menu,
  `buddy-ctl gui`, or `buddy-link --gui`. The form comes from the settings list the device sends
  (new `config` request `schema_from`), so it fits every firmware version; older firmware
  doesn't send the list and needs an update. In the emulator too.
- **Buddy Link settings in the web UI:** turn the network link (TCP) on or off, set its
  address, token and mDNS, the USB port, the proxy and the log level. Changes go to the
  config file (its comments stay) and apply at once, without a restart. TCP is off by
  default, so this is also where to turn it on for the emulator.
- **Open a Blinky's own page from Buddy Link:** the web UI links to the page each Blinky
  serves on its WiFi ("Open its page"), also for a Blinky on USB. The device tells Buddy
  Link the address with a new `web` message; the emulator sends the address of its own UI.
- **Web UI from the network:** `gui.listen` (e.g. `:7533`) and `gui.password` make the
  web UI reachable on the LAN, for a Buddy Link on a server; it asks for the password there.
- **Offline devices** stay in the web UI for 24 hours, with when they were last seen.
- **Sources editor** in the web UI: a table (name, source, interval, who fetches it,
  parameters) instead of the JSON field.
- **Flash a board from Buddy Link:** "Flash a new device" in the web UI and the tray, or
  `buddy-ctl flash --board ws146|amoled175`, installs the firmware over USB (first install or
  recovery) without a browser that can do Web Serial. Buddy Link downloads the release image
  only then, checks its SHA-256 and keeps it in a cache. It speaks the ESP32-S3 bootloader's
  protocol itself; not tested on real boards yet.
- **Start at login:** `buddy-link install --user` or the tray's new "Start at login" (Windows
  registry Run entry, macOS LaunchAgent, Linux systemd user unit). The tray also shows how
  many Blinkies are connected and whether there is internet.
- **System proxy:** without `proxy.url` and proxy env vars, Buddy Link uses the proxy of the
  Windows or macOS settings (PAC files are not supported).
- **Per-device tokens:** `tcp.tokens` gives a device its own TCP token (`dev: token`).
- **`buddy-ctl --direct`:** `devices`, `device config`, `ports` and `flash` work over USB
  without Buddy Link.

### Emulator and tools

- **`buddy-sim host`:** a stand-in for Buddy Link with fixed data and no internet, for
  repeatable tests of the firmware on a real board.

## v0.1.3 – 2026-10-27 (pre-release)

The first release with firmware, and the only release on GitHub (v0.1.0 to v0.1.2 were
removed; their notes stay in [CHANGELOG.md](https://github.com/StefanRiese/blinky-desk-companion/blob/main/CHANGELOG.md)). A pre-release: the firmware builds for both boards but
hasn't run on a board yet, and the tray icon and the macOS app haven't been tried on real
Windows and macOS desktops.

Blinky is a desk companion with mood eyes on a round ESP32-S3 touch display. **Buddy Link**
(`buddyd`) is the small host program that fetches data for it when its WiFi has no internet.

### Downloads

| File | What it is |
|---|---|
| `blinky-firmware-ws146.bin` | Firmware for the Waveshare ESP32-S3-Touch-LCD-1.46 (412×412 LCD), one image to flash at offset 0 |
| `blinky-firmware-amoled175.bin` | Firmware for the Waveshare ESP32-S3-Touch-AMOLED-1.75 (466×466 AMOLED, speaker, two microphones, battery) |
| `buddyd-windows-amd64.exe`, `buddyd-windows-arm64.exe` | Buddy Link for Windows: tray icon, no console window |
| `Buddy-Link-macos-universal.zip` | Buddy Link.app for macOS (Apple Silicon and Intel), menu bar only |
| `buddyd-linux-amd64`, `-arm64`, `-arm` | Buddy Link for Linux (also servers, Raspberry Pi, containers) |
| `buddyctl-<os>-<arch>` | Command line for a running Buddy Link: notifications, moods, data, custom pages, device settings, live monitor |
| `SHA256SUMS` | Checksums |

The [setup page](https://stefanriese.github.io/blinky-desk-companion/) flashes the firmware
from Chrome or Edge (it asks for the board). By hand: `esptool.py write_flash 0 blinky-firmware-<board>.bin`.

### Firmware

- **Mood eyes:** 16 moods that blink, glance around and blend into each other. The mood
  follows the room and the day: hot, cold or dry indoors, too loud, rain or snow outside,
  sleepy at night, sad when the TCP host is gone, happy in the sun. A tap makes the eyes
  happy for a moment, a bang startles them, voices make them curious, and a ringing alarm
  surprises them. The line under the eyes says why. `buddyctl mood` sets a mood for a while.
- **Pages** (swipe left and right): dashboard (clock, date, weather, indoor climate, rain in
  the next two hours), countdown with a progress ring, up to four alarms with weekdays and
  snooze, and an info screen with WiFi, the hosts and every data source and its state.
- **Status bar** on the pages: time, outdoor temperature (marked when out of date), WiFi and
  host icons, and on the AMOLED-1.75 the battery while it charges or runs low.
- **Sensors and sound:** SHT40 for indoor temperature and humidity, the RTC keeps the time
  without power. The alarm and the countdown beep on the speaker. The microphone measures the
  sound level only (`indoor.noise`; no audio is kept or sent; off with `sound.enabled = false`).
- **Backlight:** brighter by day, darker at night (`ui.night_from` … `ui.night_to`), dimmed
  after `ui.dim_after` seconds without a touch; a touch or a ringing alarm wakes it.
- **WiFi:** with internet the device fetches time (NTP), weather and air quality (Open-Meteo)
  and `http` sources itself; otherwise a connected Buddy Link does. Each source is fetched in
  one place only (the device, then USB, then TCP) and moves when a link or the internet comes
  or goes.
- **Set up without an app:** Improv Serial (the setup page sends the WiFi password right after
  flashing), the device's own web page (`http://blinky.local/`: connections, sources, all
  settings), and the access point **Blinky-Setup** with a captive portal when no network is
  reachable at boot (3 minutes).
- **Host links:** USB to Buddy Link or the setup page, and TCP (`link.tcp_host` as
  `host:port`, or `auto` to find Buddy Link via mDNS).

### Buddy Link

- Serves any number of buddies over USB serial (found automatically) and TCP. The device owns
  all its settings, so a buddy works with any computer: Buddy Link stores nothing about it.
- **Data sources:** time, weather and air quality (Open-Meteo, no API key), any JSON API
  (`http`, values picked with JSON Pointers), and the host's own CPU, RAM, uptime and
  temperature (`sys`).
- **Tray icon:** on Windows in the notification area, on macOS in the menu bar. The menu shows
  the version and has **Open config folder** (config, control token, log), **Show log** and
  **Close**. `--tray=false` turns it off. On Windows it logs to `buddyd.log` next to the
  config; the Mac app logs to `~/Library/Application Support/buddyd/buddyd.log`.
- **On a server:** `sudo buddyd install` sets it up as a systemd service on Linux (TCP on port
  7531). It announces itself via mDNS as `_blinky._tcp`, so devices with host link `auto`
  find it.
- **Docker:** [`deploy/docker/compose.yml`](https://github.com/StefanRiese/blinky-desk-companion/blob/main/deploy/docker/compose.yml) builds the image from
  the repository: `docker compose up -d`, and `docker compose up -d --build` to update. No
  image is published.
- Proxy support (`HTTPS_PROXY` or `proxy.url`), optional device token, local control API for
  scripts (`buddyctl`).

### Getting started

- **Laptop:** download Buddy Link for your system and start it, then plug the buddy in over
  USB. On a Mac, unzip `Buddy-Link-macos-universal.zip` and move **Buddy Link.app** to
  *Applications*.
- **Proxmox:** on the Proxmox host, as root:
  `bash -c "$(curl -fsSL https://raw.githubusercontent.com/StefanRiese/blinky-desk-companion/main/deploy/proxmox/buddyd-lxc.sh)"`.
  It installs the latest release, or the newest pre-release while there is no regular one
  (now: v0.1.3).
- **No hardware:** the emulator `buddysim` shows a buddy in the browser and talks to Buddy
  Link like a real one. It's a development tool, built from source:
  `cd host && go build -o bin/ ./cmd/buddysim`.

### Known limitations

- The firmware hasn't run on a board yet: display, touch, sound levels and the battery reading
  may need adjusting on real hardware.
- Not on the device yet: notifications, custom pages, forecast and indoor-trend pages, motion
  gestures, burn-in protection for the AMOLED (the emulator shows notifications and custom
  pages already).
- The Windows and macOS files aren't signed: Windows SmartScreen and macOS ask before the
  first start (on a Mac: *System Settings → Privacy & Security → Open Anyway*).
- `buddyd install` is Linux-only; on Windows and macOS start Buddy Link by hand (autostart is
  planned).
- Not built yet: GitLab, calendar, Proxmox and Home Assistant data sources, MQTT.

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
