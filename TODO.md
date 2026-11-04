# To do

<!-- All work for Blinky in one list: what is open (firmware, Buddy Link, hardware,
     questions), then what is done. When an item is done, tick it and move it to "Done";
     a user-visible change also gets a CHANGELOG entry. -->

## Firmware: to build

- [ ] IMU gestures not built yet: side tap, tilt, double tap, pick up / face down
- [ ] Sensor calibration
- [ ] Host-only sources in the config UI
- [ ] MQTT + Home Assistant discovery
- [ ] OTA updates over WiFi (the partitions and rollback exist already, see OTA through
      `buddy-link`): upload on the web page at `blinky.local` (needs a password or PIN first)
      and/or pull the board's release app image from GitHub, version check on the info screen
- [ ] If the eyes need more speed: two draw units on both cores (`LV_USE_OS` FreeRTOS), QSPI
      80 MHz, a 32 KB instruction cache (costs 16 KB internal RAM)
- [ ] EventBus: not needed so far (the parts talk through hooks); only if input and link events
      outgrow them

## Firmware: check on the board

### LCD-1.46 (`ws146`, has not run on a real board yet)
- [ ] Display comes up, rim ring is round and centred, colours right (byte order)
- [ ] Backlight works, board stays on from battery (power latch)
- [ ] First flash with `buddy-ctl flash` (also on a board with a USB-UART bridge)

### AMOLED-1.75 (`amoled175`)
- [ ] The new eye shape (85 × 136) and `ui.color`: moods on the narrower eye, colours on the
      AMOLED, frame rate; neutral eyes with `mood.ambient` off
- [ ] Colours, 6 px offset, no torn edges, AXP2101 rails (mics powered), battery percentage
      plausible, mic level calibrated (`BOARD_MIC_SPL_AT_FULL_SCALE`)

### Both boards
- [ ] SHT40 connected and readings plausible
- [ ] RTC keeps the time over a power cycle
- [ ] The setup page reads and writes settings
- [ ] Setup AP + captive portal, Improv from the setup page, web page, NTP, TCP to `buddy-link`
      (also `auto`)
- [ ] Rotation (alignment after a turn, frame rate with software rotation)
- [ ] Backlight levels, beeps loud enough, mic levels plausible (dB calibration), startle and
      voices thresholds
- [ ] IMU: axes and signs right (flick right = next page), shake and flick thresholds
      (`buddy-ctl monitor` shows each gesture)
- [ ] Firmware update through `buddy-link`: works on the AMOLED-1.75 (2026-11-02); still to check
      on the LCD-1.46, and the rollback when new firmware doesn't start

## Buddy Link (host)

- [ ] Sources `gitlab`, `calendar`, `proxmox`, `ha`
- [ ] `.deb`/`.rpm` packages for `buddy-link` and `buddy-ctl`: built with nFPM as one more step in
      the release workflow (no GoReleaser); with the systemd unit, a default config and a
      `buddy-link` user. Self-update stays off for packaged installs. Homebrew and Scoop only if
      users ask
- [ ] mDNS: the advert runs on one interface (the system's choice, on a laptop with VMware
      maybe a virtual adapter) and lists every local address; announce per interface with that
      interface's address (and the firmware should take the address in its own subnet)
- [ ] Windows: `install --user` could add firewall rules for TCP 7531 and UDP 5353 (private
      networks); a blocked port shows only as "no answer" on the device
- [ ] "Update Buddy Link" from the web UI on Windows (the new process starts, the tray icon comes back,
      `buddy-link.exe.old` is removed); on Linux it updated from v0.2.0 to the build from main (2026-11-02)

## Hardware

- [ ] Where the SHT40 connects on the AMOLED-1.75 (I²C 14/15): the 8-pin header has GPIOs and
      a UART
- [ ] What the TCA9554 does on the AMOLED-1.75 (xiaozhi only sets EXIO4 as an input)
- [ ] Wiring diagram (`hardware/wiring.svg`)
- [ ] Enclosure v1, thermal test, v2

## Open questions

- Which "other data" matters most: PC stats, Proxmox status, calendar, Home Assistant, mail,
  CI build status?
- Should the device's own sensor data be forwarded (MQTT / Home Assistant / InfluxDB)?
- Web UI mockup (`host/mockups/webui`): one shared settings form for Buddy Link and
  `blinky.local`? "Last seen" for offline devices? Erase settings by default when flashing?
  Light/teal or the current dark/cyan look?

## Done

### Decisions

- [x] Which display/board: all-in-one round Waveshare ESP32-S3 boards, LCD-1.46 and
      AMOLED-1.75.
- [x] Touch, buttons, or both: touch and motion gestures; the PWR and BOOT buttons have no
      function in the firmware (decided 2026-11-02).
- [x] Firmware framework: decided 2026-10-26, ESP-IDF 6.1 + LVGL 9.6.
- ~~NVS encryption for secrets~~ (skipped 2026-11-03: no critical data to protect; secrets
  stay masked as `***` over the link and on the web page)
- ~~CYD prototype, separate S3 parts, carrier PCB for S3 + sensors + button~~ (dropped
  2026-11-03: the all-in-one Waveshare boards have the S3, display, IMU, audio and RTC; only
  the SHT40 is added)

### Firmware

#### M0 – project and board bring-up
- [x] ESP-IDF 6.1 project, partition table (2 × 6 MB OTA), `sdkconfig.defaults` (octal PSRAM, 16 MB flash)
- [x] `board_ws146`: power latch, I²C, TCA9554 resets, SPD2010 display (QSPI 40 MHz), touch, backlight, `esp_lvgl_port` + rounder
- [x] Portable `ui` component with theme tokens, round-screen geometry and the hello screen (rim ring, eyes, version, touch tester)
- [x] Desktop simulator (SDL2), headless screenshot tool, host tests (doctest)
- [x] CI: host tests + screenshots; ESP32-S3 build in `espressif/idf:v6.1` with merged flash image; `dependencies.lock`
- [x] **On the board:** touch works without problems (AMOLED-1.75, checked 2026-10-29)
- [x] Local ESP-IDF setup documented for flashing (`idf.py flash monitor`, firmware/README.md)
- [x] **On the board:** first boot log checked (AMOLED-1.75, 2026-10-29: PSRAM 8 MB found and tested, codecs, touch, QMI8658; it showed the full internal RAM)

#### M1 – eyes, fonts, screenshot tests
- [x] Eyes as a custom LVGL widget with the emulator's parameter model and all 16 presets (masked layer, seam-free lids; the glow is off since 2026-10-29, too slow)
- [x] Eye geometry as in the README's GIF and the app icon (2026-10-30): upright pills, since 2026-10-30 15 % larger than the GIF: 85 × 136, radius 38, ±68 from the middle, 15 above it (`kEye*` in `ui/eye_model.h`, same in the emulator); were 108 × 132, radius 30, ±92, 12 below
- [x] Blink and saccades (random timing), smooth interpolation between presets, dizzy circling, cold shiver
- [x] App frame: eyes screen (tap = next mood, demo) and info screen with the touch tester, swipe to switch
- [x] Emulator extras on the eyes: sweat drop (hot), "z" (sleepy), heart (love)
- [x] FPS counter (LVGL performance monitor, enabled in `sdkconfig.defaults`)
- [x] Glow off (an LVGL shadow, recomputed every frame and every 40-line strip), redraw every 16 ms (was 33 ms, drifting against the eyes' 33 ms timer), FPS/CPU counter with `ui.fps`
- [x] **On the board:** smooth eyes on the AMOLED-1.75 (2026-10-29): 33-40 FPS, 22 ms per frame (21 render | 1 flush), CPU 60-75 %; idle pages near 0 %. Was 10-20 FPS at 49 ms (45 | 5). What helped: glow off, redraw every 16 ms, `-O2` instead of ESP-IDF's `-Og`, unchanged texts and colours left alone (`ui/src/set.h`; idle pages redrew 11 times a second), and above all a 64 KB data cache with 64-byte lines plus XIP from PSRAM (LVGL's objects live in PSRAM since `SPIRAM_MALLOC_ALWAYSINTERNAL=0`). Hardware rotation is no option: the SPD2010 driver can't swap X/Y, the CO5300's MADCTL MV is untested and moves the 6 px gap
- [x] `tools/fonts.sh`: lv_font_conv, Inter 14/20/28/40 (Latin-1, °, arrows, €) and Light digits 72/96, pinned and checksummed sources
- [x] Generated fonts committed (`components/ui/fonts/`), UI switched from Montserrat to Inter (`ui/fonts.h`)
- [x] Icon font (Material Icons subset, 24/48 px) + `ui/icons.h` with the symbols; icon row on the info screen
- [x] Screenshot tests: reference PNGs in `test/screenshots/` (all moods + info), compared in CI; `UPDATE_SCREENSHOTS=1` regenerates them

#### M2 – data and first real page
- [x] `app_core`: DataStore (priority, TTL, stale, 128 entries, version counter), settings registry (types, ranges, choices, secrets) stored as one NVS blob, units, time zone table (common zones; full table in M4)
- [x] SHT40 driver + sensor task (10 s, average of 6, `sensor.*_offset`), tested CRC/conversion
- [x] Dashboard page (clock, date, weather, indoor, rain hint) bound to the DataStore with `lv_subject`; units; screenshot tests
- [x] PCF85063 RTC driver: restores the system time at boot (setting it follows with NTP/host time in M3/M4)
- ~~WASM build of the UI; `buddy-sim` shows it instead of the JavaScript drawing~~ (skipped 2026-10-26: `buddy-sim` keeps its JavaScript drawing; `buddy_sim` and the screenshots cover the firmware UI)

#### M3–M6 – host link and data sources
- [x] Protocol in `app_core` (`protocol.h`: NDJSON framing, device messages; golden tests replay `protocol/examples/handshake.ndjson`)
- [x] `HostLink` (portable, host-tested): hello/answer/retry, ping, 30 s silence, subscriptions from `sources` (moved on `host_state`, re-sent after every hello), `time`/`weather`/`data` into the DataStore and the clock (RTC too)
- [x] USB serial transport (`serial_link`: USB-Serial-JTAG, reader task, link runs in the LVGL task, ESP log as `{"t":"log"}` while a host is connected, plug/unplug = link up/down)
- [x] **On the board:** `buddy-link` finds the device on USB and gets its subscriptions, weather arrives through it; `buddy-ctl --direct device config` reads and writes settings (AMOLED-1.75, checked 2026-10-29)
- [x] Opening the port gives a burst of old hellos (sent every 5 s while no host listened, kept in the USB buffer). `buddy-link` now answers only the first: a repeat with the same `dev` and `fw` within 2 s is ignored (2026-11-02)
- [x] Log lines from the WiFi driver arrived split in two (`"I (5758) wifi:"`, then the text). `log_vprintf` now collects output up to the newline (`core::LogLines`, tested; an unfinished line goes out after 100 ms or when another task logs) before it sends a `log` message (2026-11-02)
- [x] Speaker volume `sound.volume` (0 … 100 %, default 50 = the loudness before; square curve in `core::speaker_level`, in software on both boards) in the registry, the emulator, the setup page, the Buddy Link web UI and the device's page; each has a "Test" button (`config` `test_sound`) that plays the chime once without saving (2026-11-02)
- [x] `config` get/set (typed values from the registry, secrets masked, all-or-nothing `set_many`) incl. the setup page's `role: "setup"` (config only, no token, no subs)
- [x] Device events to the host (touch, page, countdown, alarm; sound as before) and `sensor` messages once a minute (temp, hum, noise); `mood`/`notify`/`page` from the host
- [x] Link state on the info screen: WiFi, USB and TCP host, every source with who fetches it and its state
- [x] WiFi: station (reconnects by itself), setup AP `Blinky-Setup` with captive portal (no network set, or not reachable 10 s after boot; closes after 3 min), Improv Serial, web config page (`http://blinky.local/`, generated from the registry), SNTP, internet check
- [x] DeviceFetcher (time over NTP, Open-Meteo weather and AQI, `http` with JSON Pointer picks; host-tested) and SourceRouter (device vs. USB host vs. TCP host, as in the emulator)
- [x] TCP transport with mDNS discovery (`link.tcp_host = "auto"`), reconnect with backoff, subscriptions move between hosts
- [x] Status bar (time, temperature, WiFi/USB/TCP icons, stale marker) on every page but the info screen
- [x] **On the board:** WiFi connects (AMOLED-1.75, checked 2026-10-29)
- [x] `wifi.enabled` (default on): off = USB only, `net::start` leaves WiFi, setup AP, web page and mDNS out; changing it restarts. Emulator: device-native sources need it on
- [x] **On the board:** `wifi.enabled = false`: no WiFi; the weather, AQI and location subscriptions go to Buddy Link on USB; 142 KB internal RAM free after start instead of 48 KB (AMOLED-1.75, checked 2026-10-30)
- [x] **On the board:** Open-Meteo over HTTPS works: weather, AQI and location without a host (AMOLED-1.75, d45b484, checked 2026-10-29; 40 KB internal RAM free after boot, 34 KB at least). Failed on the AMOLED-1.75 with v0.2.0 (`mbedtls_ssl_setup` -0x008D = alloc failed, "Failed to create socket", `esp_mmu_map_virt: no mem` 3 s after boot): the internal RAM was full, because malloc() below 16 KB stayed internal and LVGL makes many small allocations. TLS in PSRAM alone didn't help; now `SPIRAM_MALLOC_ALWAYSINTERNAL=0` and 64 KB reserved
- [x] **On the board:** sometimes rebooted with a white screen (AMOLED-1.75, seen 2026-10-29). Cause: the connection with `buddy-link` (opening the USB port resets the chip, reset reason 11). Closed 2026-11-02. The reset reason stays on `/api/status` and in the boot log

#### M7 – behaviour (as in the emulator)
- [x] MoodEngine (`mood` component) with the emulator's rules: reactions (boop, shaken, loud noise, voices) > alarm > countdown > host `mood` > hot/cold > dry > too loud > rain/snow > night > TCP host lost > sunny > neutral; everything after the host `mood` only with `mood.ambient` (off by default), night with its own `mood.night` (on by default)
- [x] Pages: forecast (`weather.day0…2`, also from the device's own Open-Meteo request), indoor trend (a sample every 10 s, half an hour), host data (all but `weather.*`/`indoor.*`); screenshot tests
- [x] Custom pages from the `page` message (`core::CustomPages`: up to 8 pages of up to 8 widgets: value, bar, icon+text, list, sparkline; `ttl`, `del`), after the built-in pages, before the info screen; screenshot test
- [x] Analog clock page (`ui::ClockPage`, `clock` in `ui.pages`): 12 marks, hour, minute and second hands, date; no status bar; also in the emulator
- [x] `ui.pages` (selection and order), swipes left/right, swipe up = home: `core::pages`/`check_pages` (validated in the settings, as in the emulator), `ui::App` swipes through them with the info screen last, starts on and goes "home" to the eyes (or the first page), keeps a hidden countdown/alarm page while it runs or rings; pages the firmware lacks would be skipped
- [x] Countdown page (swipe minutes/seconds, ring at zero, progress ring); since 2026-11-02 hold = start/resume, tap = pause, tap while paused = reset, so a swipe taken for a tap can't start it
- [x] Alarm page (alarms in settings, edit on the page, ring, tap = snooze, hold = stop, next alarm)
- [x] Countdown/alarm beeps on the speaker (PCM5101 over I²S, `audio` component)
- [x] The countdown's time left in the status bar on other pages (and the alarm icon while an alarm is on)
- [x] Sound reactions from the I²S mic level (startle, curious, too loud), `indoor.noise`, `sound` event to the host
- [x] Backlight: day/night/dim levels, idle dimming, wake on touch and while something rings (notifications follow with them)
- [x] AMOLED burn-in: the eyes keep moving (glances, idle routines), data pages go back to the eyes after `ui.home_after`, idle dimming, screen off at night with `ui.dim_night = 0`, no status bar while dimmed (2026-11-02). No pixel shift needed
- [x] Display units (°C/°F, km/h/mph/m/s, mm/in)
- [x] Display rotation in 90° steps (`ui.rotation`, degrees clockwise): software rotation in `esp_lvgl_port` (`sw_rotate`, rounders aligned on both axes), LVGL turns the touch points; applied at boot and when the setting changes; `buddy_sim --rotate`; screenshot and touch test (the emulator stores it but doesn't turn its picture)
- [x] **On the board:** the speaker beeps at the end of the countdown (AMOLED-1.75, checked 2026-10-29)
- [x] Notifications with overlay and sound: `notify` from a host shows a box over every page for `dur` seconds (icon, two lines), sets its mood, wakes the backlight, plays a chime (`Tune::Notify`) if it has a sound; a tap or a shake hides it
- [x] Touch and IMU gestures: touch events to the host; QMI8658 driver (100 Hz) + `core::MotionDetector` (shake: ≥ 4 direction changes > 1 g within 1 s; flick: gyro Z > 150 °/s back within 400 ms; 600 ms cooldown), flick = next/previous page, shake = dizzy eyes only (the countdown and alarms run on taps, since 2026-10-30), reported as `gesture` events
- [x] **On the board:** QMI8658 found at 0x6b on the AMOLED-1.75 ("gestures on" in the boot log)

#### M8–M10 – later
- [x] OTA through `buddy-link` (USB or TCP): `ota` messages (begin with size, SHA-256 and version, numbered Base64 blocks of 1 KiB each acked, end → check, boot partition, restart); `core::OtaReceiver` (portable, tested) + `esp_ota_*` in `app_main`; rollback (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, marked valid once the UI runs); also in the emulator. **On the board:** an update through `buddy-link` works (AMOLED-1.75, 2026-11-02); the rollback is not tested yet. Firmware before 0.2.0 has no `ota` and ignores it (update timed out, "no reply from device"); `buddy-link` now refuses that with a hint to install once over USB, and the firmware announces `ota` in `caps`
- [x] Release also the app image per board (`blinky-app-<board>.bin`, next to the merged `blinky-firmware-<board>.bin`) for OTA over WiFi and through `buddy-link`
- [x] Firmware release so the setup page can flash it: the pages workflow puts the newest release's image and one ESP Web Tools manifest per board (`site/firmware/manifest-<board>.json`) on the setup page
- [x] **On the board:** `buddy-ctl --direct flash` over the board's own USB port works, after turning off the RTC watchdog (AMOLED-1.75, checked 2026-10-29). Flashing kept the settings only since 2026-10-30: the flasher and the setup page skip the nvs and nvs_keys partitions (from the partition table in the image)
- [x] **On the board:** flashing from the setup page on GitHub Pages (ESP Web Tools, release image) works without problems (AMOLED-1.75, checked 2026-10-29)
- [x] Settings schema over the link, so `buddy-link` can show a settings page for a device on USB or TCP: `{"t":"config","id":…,"schema_from":0}`, answered page by page (one line holds 2048 bytes) with `settings_schema_page()`; in `protocol/PROTOCOL.md`, a golden example, the emulator and the firmware tests
- [x] Web page address to the host: `{"t":"web","url":"http://<ip>/"}` after the hello and when the WiFi address changes (`""` = none), so the `buddy-link` web UI links to the device's own page, also over USB; in `protocol/PROTOCOL.md`, a golden example, the emulator and the firmware tests

#### Second board: Waveshare ESP32-S3-Touch-AMOLED-1.75 (plain, no GPS)
- [x] Layout from the screen size: designs stay in 412 px coordinates and `ui::place()` keeps them centred (same pixel density, so same sizes); screenshot tests at 466 px
- [x] Board choice at build time: one `board` component (Kconfig "Blinky board", one source file per board), `idf.py -D BOARD=amoled175`, `boards/<board>.defaults`, one build folder per board
- [x] `board_amoled175.c`: AXP2101 (DC1 and ALDO1 at 3.3 V, charging), CO5300 over QSPI (init as in xiaozhi-esp32, 6 px column gap, even/odd rounder), brightness with command `0x51`, CST9217 touch (mirrored X and Y)
- [x] Audio through the board API (`board_speaker_*`, `board_mic_*`, 16-bit mono at 16 kHz on both boards): ES8311 + ES7210 with `esp_codec_dev` on the 1.75, the I²S code of the ws146 moved into its board file
- [x] Battery level and charging from the AXP2101 in the status bar (icon while charging or below 20 %) and on the web page
- [x] CI and release build both boards (`blinky-firmware-<board>.bin`); the setup page asks for the board (`firmware/boards.json`, one ESP Web Tools manifest per board)
- [x] `ui.color`: the eyes' colour (moods with their own keep it) and the accent on the pages, applied at once; schema type `color` with presets; setup page, Buddy Link web UI, the device's page, emulator
- [x] PWR and BOOT buttons: no function in the firmware (decided 2026-11-02). Only the AXP2101 itself switches off when PWR is held 4 s
- [x] **On the board:** flashed from the setup page, display comes up, touch works (not mirrored), WiFi connects, the speaker beeps, dimming works (checked 2026-10-29); the microphones work: about 52 dB in the room, "loud" sound events (checked 2026-10-30). One I²C read from the ES7210 fails at each start, just before the input is opened a second time; the open succeeds
- [x] **On the board:** white flashing at the rim at about 5 o'clock is gone (it was LVGL's performance monitor, now off; checked 2026-10-29)

- [x] **On the board:** internal RAM at start (AMOLED-1.75, WiFi on, `ui.rotation = 180`,
      checked 2026-11-04): no peak at start. The "7 KB during start" was ESP-IDF's cumulative
      minimum: the 64 KB reserved DMA/internal pool reports `min_free 0` before `app_main`, so
      that figure is 63 KB too low. The boot log now shows the low of each start step (local
      minimum); the real low is 36 KB (audio step), and 43 KB stay free while running. Was 8-11 KB
      running with this setup; won back with the WiFi code out of IRAM (19 KB) and 8 WiFi static
      RX buffers instead of 16 (13 KB). Where it goes: LVGL buffers 109 KB (2 draw buffers plus the
      rotation buffer, which stays: 90° and 180° are needed), WiFi 79 KB, audio 23 KB (I²S RX
      DMA 11 KB for 4 TDM slots of which one is used, TX 6 KB). Hardware 180° in the CO5300
      (MADCTL 0x02|0x05) tried and reverted: picture upside down and shifted

### Buddy Link (host)

- [x] protocol package + golden examples, session (handshake, subscriptions, keepalive, request/reply)
- [x] serial transport (VID/PID auto-detect, PTY/explicit port), TCP transport + token, per-device tokens (`tcp.tokens`)
- [x] sources `time`, `weather`, `aqi`, `geo`, `http`, `sys`, shared fetch cache
- [x] control API + `buddy-ctl` (notify, mood, data, page, devices, device config, monitor, status); `--direct` for devices, device config, ports and flash over USB without buddy-link
- [x] tray icon (Windows notification area, macOS menu bar): version, status line, web UI, config folder, log, Start at login, Close; console hidden on double-click
- [x] **On Windows:** the tray icon works (checked 2026-11-02)
- [x] **On macOS:** the tray icon and the macOS app work (checked 2026-11-03)
- [x] `Dockerfile` (distroless, multi-arch) and a Compose file that builds it (no published image)
- [x] mDNS advert `_blinky._tcp` (hashicorp/mdns), `buddy-link install` (Linux/systemd), `install --user` (start at login on Windows, macOS, Linux), Proxmox LXC script, release workflow
- [x] host config (YAML + env + flags, `${env:}`/`${file:}`); proxy from `proxy.url`, env vars or the Windows/macOS settings
- [x] CI: test on Linux/Windows/macOS, golangci-lint, cross-build artifacts
- [x] burst of old hellos when the serial port opens (sent while no host listened, about 18 in a second): a hello with the same `dev` and `fw` less than 2 s after the previous one is ignored (`session.HelloBurst`), so the burst is one connection
- [x] `buddy-sim device`: emulator with browser UI (display, SourceRouter, NVS, failover toggles); `buddy-sim host`: stand-in host with fixed data for firmware tests
- [x] web UI (`internal/control/web`): embedded page on the control port, login link with the token (cookie), `buddy-link --gui`, `buddy-ctl gui`, tray "Open Buddy Link…"; lists the connected devices and edits their settings from the device's schema (`config` `schema_from`); links to each device's own page (`web` message); the header, a device's header, its save bar and section menu stay in place while scrolling (on phones only the header and the save bar); "Buddy Link settings" edits TCP, USB, proxy and log level, written to the config file and applied at once; `gui.listen` + `gui.password` for the network (password login, session cookie); offline devices for 24 h (`/v1/devices?all=1`); sources editor
- [x] settings export/import (2026-11-02), only in Buddy Link, not in the firmware: `buddy-ctl device export` / `device import` (also `--direct`), "Export settings" / "Import settings…" in the web UI (as unsaved changes); without secrets, keeps the device's name, skips keys the device doesn't know
- [x] firmware update through `buddy-link` (`flasher.Updater`): `buddy-ctl device update --dev ID [--version V] [FILE.bin]`, `POST /v1/devices/{id}/update`, the release app image (`blinky-app-<board>.bin`) as `ota` messages over USB or TCP, then waits for the device to come back with the new version; refuses firmware before 0.2.0 at once (it has no `ota`; the device announces `ota` in `caps` since then)
- [x] "Flash a new device" in the web UI (port, board, release or `.bin`, progress), tray "Flash new device…"; "Update to vX" per device (newest release with an app image) and from a `.bin`, tray "Update firmware…"; endpoints `/v1/ports`, `/v1/firmware/releases`, `/v1/flash`, `/v1/devices/{id}/update`, `/v1/jobs/{id}`
- [x] firmware images downloaded on demand from the GitHub releases (`internal/firmware`), checked against the SHA-256 GitHub lists, newest per file kept in `<user cache>/buddy-link/firmware`; `--version`, `FILE.bin` offline; the rolling pre-release `latest-main` ("Latest build from main", `--version main`), never picked as "latest"; first one published 2026-10-30 (0.2.1-dev.20+4efd9d8, 17 files)
- [x] update offers (`firmware.OffersFor`, `Server.WatchReleases`): the release list every 10 min, compared per device by semantic versioning: newer release, newer pre-release, the build from main (`on_main`, `main_newer`); `updates` in `/v1/devices`, buttons on the cards and pages, a banner over the device list, "Update firmware… (N)" in the tray
- [x] buddy-link updates itself from the web UI (2026-11-02, `internal/selfupdate`, `/v1/host/update`): release, pre-release or build from main; SHA-256 and a test run of the new program, swap, restart (exec on Linux, new process on Windows); not for the macOS app, containers or builds from source
- [x] **On a real Blinky:** updating through `buddy-link` works without problems (AMOLED-1.75, checked 2026-11-02)
- [x] first flash over USB (`internal/espflash`: the ROM loader protocol in Go, zlib blocks, MD5 check; `internal/flasher`): `buddy-ctl flash --board B [--port P] [--version V] [FILE.bin]`, also `--direct`; the serial transport lets go of the port meanwhile (`serial.Hold`); turns off the RTC watchdog over USB-Serial-JTAG (as esptool); keeps the settings (skips `nvs` and `nvs_keys` from the image's partition table; `--erase-settings`, checkbox in the web UI). Works on the AMOLED-1.75 over its own USB port (2026-10-29)
