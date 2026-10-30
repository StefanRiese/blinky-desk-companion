# Changelog

## Unreleased

### On the device

- **Eyes as in the README's GIF:** upright pills (74 × 118 px with a 33 px radius, closer
  together, a little above the middle) instead of the wider, squarer eyes. The moods keep
  their shapes on the new eye. In the emulator and the screenshots too.
- **The Blinky's colour:** new setting `ui.color` (`#RRGGBB`, default cyan `#3EE8FF`) for
  the eyes and the highlights on the pages (clock hand, rings, charts, notification frame).
  Moods with a colour of their own (angry, sad, hot, love, …) keep it. It applies at once.
  The setup page and the Buddy Link web UI show six presets (cyan, mint, amber, rose,
  violet, white) and a picker for any colour; the Blinky's own page has a picker. In the
  emulator too. Editors see the new setting type `color` in the settings list.
- **Countdown and alarm on taps, not shakes:** a tap starts, pauses and stops the
  countdown, and a ringing alarm snoozes on a tap and stops on holding, also on a Blinky
  with the motion sensor. A shake only makes the eyes dizzy (and hides a notification).
  The hints on the pages say so; in the emulator too.
- **USB only:** new setting `wifi.enabled` (on by default). Off, the Blinky doesn't start
  WiFi at all: no setup access point, web page or mDNS, and the RAM WiFi needs stays free.
  Buddy Link on USB fetches everything. Changing it restarts the Blinky. In the setup page,
  the Buddy Link web UI and the emulator.
- **Weather without Buddy Link works again:** the internal RAM was full a few seconds after
  start, because every small allocation (LVGL's widgets, styles, texts) went there. So the
  device's own HTTPS requests (weather, air quality, location, `http` sources) failed with
  "request failed (ESP_ERR_…)", and a new firmware couldn't mark itself as good after an
  update. Allocations now go to PSRAM first, TLS too, and 64 KB of internal RAM stay free
  for WiFi, sockets and drivers.
- **Smoother eyes:** the glow around the eyes is gone (an LVGL shadow, computed again for
  every frame and every 40-line strip), and the screen is redrawn every 16 ms instead of
  33 ms, so the eyes' animation steps no longer drift against the refresh. In the emulator
  and the screenshots too. The firmware is now compiled for speed (`-O2`, was ESP-IDF's
  debug default `-Og`): drawing the eyes took 45 ms per frame on the AMOLED-1.75.
- **Faster drawing from PSRAM:** a 64 KB data cache with 64-byte lines, and the program
  runs from the octal PSRAM instead of the flash. The eyes waited mostly for memory.
- **Turning the picture may restart the Blinky:** the rotation buffer (one more draw buffer
  in internal RAM) is only set up when `ui.rotation` is not 0 at start, to make room for
  the bigger cache. Changing from 0 to another angle restarts the Blinky; other changes
  apply at once.
- **Idle pages stay idle:** the pages set their texts and colours on every update, and LVGL
  redrew them each time even when nothing changed (the alarm and countdown pages about 11
  times a second, 60 % CPU on the device). Unchanged texts and colours are now left alone.
- **Frame rate counter:** new setting `ui.fps` shows LVGL's frame rate and CPU load at the
  bottom of the screen, for measuring the rendering (off by default).
- **Free memory and reset reason on `/api/status`** of the device's page (`mem`,
  `reset_reason`), and in the boot log.

### Buddy Link

- **Web UI headers stay in place:** the Buddy Link header, a Blinky's header, its save bar
  and the section menu no longer scroll away with the settings. On a phone only the
  header and the save bar stay; the update buttons moved into their own "Firmware" card.
- **Latest build from main:** every push to `main` that changes the firmware or Buddy Link
  is published as the rolling pre-release `latest-main`, with versions like
  `0.2.1-dev.14+19a1106` (after the last release, before the next). Buddy Link lists it when
  flashing and offers "Update to the latest build from main" on a Blinky's page;
  `buddy-ctl flash|device update --version main`. It is never suggested as an update by
  itself, and the setup page and the Proxmox script ignore it. Changes to the documentation
  alone don't publish a new one.
- **Flashing keeps the settings:** "Flash a new device", `buddy-ctl flash` and the setup page
  no longer erase WiFi, rotation and the other settings: the settings partitions (read from
  the partition table in the image) are left as they are. `--erase-settings`, the checkbox
  in the web UI or "erase" on the setup page start from scratch. The setup page does this
  from the next release on (it serves the newest release).
- **Flashing over the board's own USB port works:** "Flash a new device" and
  `buddy-ctl flash` stopped partway ("Port has been closed"): after the reset into the
  bootloader the chip's RTC watchdog kept running and reset it in the middle of the write.
  Buddy Link now turns it off first, as esptool does. Tested on the AMOLED-1.75.
- **Clear message for old firmware:** updating a Blinky with firmware before 0.2.0 over the
  link timed out ("no reply from device"), because that firmware can't take updates. Buddy
  Link now says so at once and the web UI shows "install the new firmware once over USB"
  instead of the update button. The firmware now says in its hello that it takes updates
  (`ota` in `caps`).

## v0.2.0 – 2026-10-29

The first regular release. The firmware now runs on a real Waveshare ESP32-S3-Touch-AMOLED-1.75:
flashed from the setup page, display, touch, WiFi, dimming and the speaker work. Still open:
the LCD-1.46 on a real board, firmware updates and flashing through Buddy Link on real boards,
the IMU thresholds, and the tray icon and the macOS app on real Windows and macOS desktops.

Buddy Link and its tools have new names (`buddy-link`, `buddy-ctl`, `buddy-sim`); see
"Renamed" below before updating an existing setup.

### Downloads

| File | What it is |
|---|---|
| `blinky-firmware-ws146.bin` | Firmware for the Waveshare ESP32-S3-Touch-LCD-1.46 (412×412 LCD), one image to flash at offset 0 |
| `blinky-firmware-amoled175.bin` | Firmware for the Waveshare ESP32-S3-Touch-AMOLED-1.75 (466×466 AMOLED, speaker, two microphones, battery) |
| `blinky-app-ws146.bin`, `blinky-app-amoled175.bin` | The app alone, for updates through Buddy Link |
| `buddy-link-windows-amd64.exe`, `buddy-link-windows-arm64.exe` | Buddy Link for Windows: tray icon, no console window |
| `Buddy-Link-macos-universal.zip` | Buddy Link.app for macOS (Apple Silicon and Intel), menu bar only |
| `buddy-link-linux-amd64`, `-arm64`, `-arm` | Buddy Link for Linux (also servers, Raspberry Pi, containers) |
| `buddy-ctl-<os>-<arch>` | Command line for a running Buddy Link: notifications, moods, data, custom pages, device settings, updates, flashing |
| `SHA256SUMS` | Checksums |

The [setup page](https://stefanriese.github.io/blinky-desk-companion/) flashes the firmware
from Chrome or Edge (it asks for the board). Without Web Serial: "Flash a new device" in
Buddy Link, or `esptool.py write_flash 0 blinky-firmware-<board>.bin`.

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

- **No more flicker at the rim:** LVGL's FPS/CPU counter from the bring-up was still on. It
  sat in the bottom-right corner, mostly outside the round glass, and flashed at about 5 o'clock.
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
