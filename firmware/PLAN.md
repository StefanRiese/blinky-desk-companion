# Firmware Plan (ESP32)

Part of the [main plan](../PLAN.md). Protocol details: [protocol/PROTOCOL.md](../protocol/PROTOCOL.md).

## Goals
- Animated mood eyes at ≥ 25 FPS on the round 412×412 display (Waveshare ESP32-S3-Touch-LCD-1.46)
- Works with no link at all (eyes + local sensor), standalone over WiFi, and fed by a host
  over serial / TCP / MQTT. Several sources can run at the same time.
- **The device owns all configuration** (NVS): sources, locations, tokens, pages, moods.
  Hosts are interchangeable gateways that only fetch what the device asks for.
- All behaviour configurable at runtime without reflashing, and without any host app

## Toolchain (decided 2026-10-26)
**ESP-IDF 6.1 with CMake (`idf.py`), C++20, LVGL 9.6 through `esp_lvgl_port`.** Not
Arduino on PlatformIO (the earlier proposal), because:
- Every part of the recommended board has a maintained Espressif component (table below).
  [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) runs this exact stack on this board
  (ESP-IDF 6, LVGL 9.5, `esp_lvgl_port` 2.9, `esp_lcd_spd2010` 2.0), so it is proven.
- Waveshare's Arduino SPD2010 driver is only tested with Arduino-ESP32 3.0–3.1 and LVGL 8.3,
  and official PlatformIO still ships only Arduino core 2.x (core 3 needs the community
  pioarduino fork).
- Everything else is built into ESP-IDF: NVS with encryption, `esp_http_client` with the
  certificate bundle, SNTP, HTTP server (web config), OTA, USB-Serial-JTAG, I²S, LEDC.
- CI builds with the `espressif/idf:v6.1` Docker image; `idf.py merge-bin` makes the single
  image the setup page flashes (ESP Web Tools).

| Part (ws146 board) | Component / approach |
|---|---|
| Display SPD2010, QSPI, 412×412 RGB565 | `espressif/esp_lcd_spd2010` ^2.0 |
| Touch SPD2010 (I²C) | `espressif/esp_lcd_touch_spd2010` ^2.1 |
| IO expander TCA9554 (display/touch reset) | `espressif/esp_io_expander_tca9554` ^2.0 |
| LVGL glue (task, flush, touch input, rotation) | `espressif/esp_lvgl_port` ^2.9 |
| GUI | `lvgl/lvgl` ~9.6 |
| Backlight | LEDC PWM |
| Microphone (sound level) / speaker | I²S MEMS mic and I²S amplifier, no codec (mic GPIO 2/15/39, amp 47/48/38) |
| IMU QMI8658, RTC PCF85063, SHT40 | small own I²C drivers (a few registers each) |
| JSON | ArduinoJson 7 (header-only, needs no Arduino; same code in host tests) |
| mDNS / MQTT | `espressif/mdns` ^1.14 / `esp-mqtt` (built in) |
| WiFi right after flashing | Improv Serial (small own implementation) |

Versions are pinned in `main/idf_component.yml` and `dependencies.lock`. Other boards (CYD,
DIY S3 + ILI9341/ST7789) can follow later as another `board_*` component with an `esp_lcd`
driver; the UI adapts its layout to rectangular screens.

## Directory layout
```
firmware/
  CMakeLists.txt         ESP-IDF project
  sdkconfig.defaults     octal PSRAM, 16 MB flash, LVGL options
  partitions.csv         nvs, nvs_keys, otadata, ota_0, ota_1
  main/                  app_main.cpp: board → settings → tasks; idf_component.yml (pinned)
  components/
    app_core/            DataStore, settings registry, protocol (NDJSON), HostLink,   ┐ portable C++:
                         countdown/alarms, units, time zones                            │ no ESP-IDF headers,
    mood/                MoodEngine, rules                                              │ built for the device,
    ui/                  LVGL UI: eyes widget, pages, status bar, page manager,         │ the desktop and the
                         round layout, theme, fonts, icons                              ┘ host tests
    sources/             SourceRouter, StandaloneFetcher (esp_http_client), TzTable
    serial_link/         USB transport (USB-Serial-JTAG) for the HostLink; tcp, mqtt later
    net/                 WiFi (station + setup AP), captive portal, web config, mDNS, SNTP, OTA, Improv
    sensors/             SHT4x, QMI8658 (gestures), PCF85063, mic level (I²S), battery
    audio/               beeps and sounds over I²S
    board/               one API (board.h), one file per board: ws146 (SPD2010, TCA9554, PWM
                         backlight, I²S audio), amoled175 (CO5300, CST9217, AXP2101, ES8311/ES7210)
  sim/                   desktop build of the portable parts (SDL2 window, screenshots)
  tools/                 fonts.sh (lv_font_conv), icons, merge-bin
  test/                  host unit tests (CMake + doctest), run in CI without hardware
```
**Portability rule:** `app_core`, `mood` and `ui` include only standard C++,
LVGL and ArduinoJson. Hardware is behind small interfaces (`Clock`, `Settings`, `Backlight`,
`Sound`, `Haptics`) that `board` and `sim/` implement. That is what lets the same UI
code run in the desktop simulator and the host tests.

## GUI
### Rendering
- **LVGL 9.6 draws everything, including the eyes.** The eyes become a custom LVGL widget
  that draws rounded rectangles, lids and arcs with the `lv_draw` API and invalidates only its
  own area. One renderer means page slide animations, overlays and the simulator work the
  same for eyes and pages. Fallback if the M1 spike is too slow: draw the eyes into an
  `lv_canvas` with a small own rasterizer.
- **Buffers:** `esp_lvgl_port` with two partial draw buffers in internal DMA RAM
  (412×40×2 bytes each, 66 KB in total) to start; full-frame double buffering in PSRAM
  (2 × 340 KB) if partial updates tear. The SPD2010 needs a *rounder* callback that widens
  invalidated areas to its alignment (as xiaozhi does).
- **Target:** ≥ 25 FPS for the eye area (~300×180 px) over QSPI, measured in the M1 spike
  with an FPS counter on the debug page.

### Layout for the round screen
- `RoundLayout` gives the usable width at each height; text stays in the inner ~290×290 px.
  The rim is used for rings (`lv_arc`: countdown progress, brightness, timers), the status
  line at the top and page dots at the bottom, as in the emulator.
- On rectangular boards the same pages use `Layout::rect`.
- **Theme:** black background, light text, one accent colour (cyan), mood colours for the
  eyes. Colours and sizes are tokens in `ui/theme.h`, matching the emulator.

### Fonts and icons
- Bitmap fonts made with **lv_font_conv** (MIT) from **Inter** (OFL): 14/20/28/40 px with
  Latin-1 (umlauts, °), plus digits-only 72/96 px for the clock and countdown. 4 bpp.
- Icons (weather, WiFi/USB/TCP, alarm, battery) as an icon font from a **Material Icons**
  subset (Apache-2.0), also via lv_font_conv, so they scale and take the text colour.
- `tools/fonts.sh` regenerates everything; the generated C files are committed, so building
  the firmware doesn't need Node.js.

### Data, input and pages
- Widgets bind to DataStore values with LVGL 9 **observers** (`lv_subject`), so a label
  only redraws when its value changes.
- Touch goes through `esp_lvgl_port`'s input device into an own gesture recognizer that
  produces the same actions as the emulator (`swipe_up` with the start position, `tap`,
  `long_press`, …). IMU gestures (shake, flick) enter the same queue.
- Custom pages from the `page` message are built at runtime from the widget list
  (`value`, `bar`, `icon+text`, `list`, `sparkline` → labels, `lv_bar`, `lv_chart`).

### Tools for building the GUI
- **Code first, no designer as the source of truth.** There are few pages, most are
  data-driven, custom pages are built at runtime, and C++ is easy to review and diff.
- **Design loop:** change the C++ → run the **desktop simulator** (`sim/`, SDL2, rebuilds
  in seconds) → check it in the browser emulator.
- **Browser emulator with the real UI:** `sim/` also builds the UI to WebAssembly
  (Emscripten, as in LVGL's `lv_web_emscripten`). `buddy-sim` will show that instead of its
  current JavaScript drawing, so the emulator shows exactly the device's pixels and the JS
  renderer goes away. Interface: JS passes the device state (`ui_set_state(json)`) and input
  (`ui_input(action, x)`); the WASM module draws into the canvas.
- **Screenshot tests:** the desktop simulator renders every page with fixed data to PNG
  (`lv_snapshot`); CI compares them with committed reference images to catch layout
  regressions on the round screen.
- **Optional designers**, for sketching a page, not for the code we keep:
  - LVGL Pro Editor (XML, Figma import, exports plain LVGL C; free for non-commercial
    use, loading XML on the device needs a paid licence, so we would only export C)
  - EEZ Studio (open source, LVGL 9)
  - Not SquareLine Studio: commercial, its partnership with LVGL ended and it lags behind
    LVGL versions.

## Core components

### DataStore
- Fixed-capacity map `key (string) → {variant value, source, ts, ttl}`; values: int,
  float, string, small JSON blob (forecast arrays).
- Source priority per key prefix (default: local sensor > fetched source, whether the device
  or a host fetched it > values pushed by scripts), configurable.
- `get()` returns value + `isStale`. Change subscribers are notified via EventBus.
- Guarded by a FreeRTOS mutex. UI reads a snapshot once per frame.

### Configuration
The device is the single source of truth. A host can be replaced at any time and gets
everything it needs from the device when it connects.

**ConfigStore (NVS)** holds:
- WiFi, location (name, lat/lon), time zone (IANA name), units
- **sources**: list of `{sid, src, every, params, via}` (e.g. weather, GitLab projects +
  token, calendar URL, Proxmox token, `http` URLs with JSON Pointer picks)
- links: TCP host/port/token (or mDNS), MQTT broker
- display/pages: `ui.pages` = which built-in pages to show, in swipe order (default all;
  custom pages from a host are added after them), night hours (`ui.night_from/to`);
  later per-page durations for auto-rotation
- backlight (0–255): `ui.brightness_day`, `ui.brightness_night`, and after
  `ui.dim_after` seconds without touch (0 = never) `ui.dim_day` / `ui.dim_night`
- display units: `units.temp` (C/F), `units.wind` (kmh/mph/ms), `units.rain` (mm/in).
  Data and thresholds stay metric (`mood.hot` is °C); only the display converts.
- alarms: `alarm.list` (max 4 × `{at:"HH:MM", days:["mon",…], on}`; no days = every day),
  `alarm.snooze_min`; sound reactions: `sound.enabled`, `sound.loud`, `sound.too_loud`
- mood rules, gesture mapping, sensor offsets

Settings are defined in one registry (key, type, range/enum, default, `secret` flag). All
editors below are driven by it, and `config get` returns it as a schema.

**Secrets** (WiFi password, API tokens) use NVS encryption. On the S3 the key comes from an
HMAC eFuse key, so no full flash encryption is needed. They are never shown again after
saving (masked as `***`) and are only sent to a host inside `sub` params.

**Ways to edit (no host app needed):**
1. **Web UI on the device** (`http://blinky.local`, and on the `Blinky-Setup` AP as
   captive portal): all settings, generated from the registry. Small static page in flash,
   JSON API on the device.
2. **Web Serial setup page** on the project's GitHub Pages site
   (<https://stefanriese.github.io/blinky-desk-companion/>, source in [`site/`](../site/), same
   page as the ESP Web Tools flasher). Works over USB in Chrome/Edge on any OS, even when
   the device has no WiFi. `buddy-link` releases the serial port while it is open (tray
   "Configure device…"). It says `hello` with `"role":"setup"` and then uses `config`
   get/set (see PROTOCOL). Already working against the emulator (`buddy-sim --site`).
   - **Flashing:** ESP Web Tools reads `site/firmware/manifest.json`. The firmware release
     workflow must publish one merged image per board there (`chipFamily: "ESP32-S3"`,
     single part at offset 0, built with `esptool merge_bin`). Until then the page says
     that no firmware is published.
   - **WiFi right after flashing:** the firmware should implement **Improv Serial**, so ESP
     Web Tools offers "Connect to WiFi" at the end of the install, before step 2.
3. **Touch quick menu** for everyday things: brightness, do-not-disturb, page rotation.
4. **`config` messages** from `buddy-ctl device config`, MQTT or scripts.

**Backup/restore:** export/import all settings as JSON (secrets optional, off by default)
from the web UI or the Web Serial page. Used to move settings to a new board.

### SourceRouter
Decides per configured source who fetches it:
- `via: auto` (default): the **device itself** when WiFi has internet and the source is
  device-native; otherwise a **connected host** that lists the source in `srcs` (and has
  `net:true` for internet sources). `via: device` / `via: host` force one side.
- Device-native sources: `time` (NTP), `weather`, `aqi`, `http` (JSON ≤ 16 KB, TLS with
  the ESP-IDF certificate bundle). Host-only: `gitlab`, `calendar`, `proxmox`, `ha`, `sys`.
- With several hosts connected (USB laptop + Proxmox over TCP), each source is subscribed on
  one host only. Preference: serial host first, then TCP. On link loss or `host_state
  {net:false}` the source moves to the next option. The DataStore TTL bridges the gap.
- Results land in the same DataStore keys no matter who fetched them.

### Time zone
The firmware embeds an IANA → POSIX TZ table (e.g. from `posix_tz_db`, ~20 KB). The
config stores the IANA name. NTP or the host's `time` message only provide the epoch.

### Transports & HostLink
- `Transport` interface: `begin()`, `available()`, `readLine()`, `writeLine()`, `connected()`, `name()`.
- `SerialTransport`: USB-CDC (S3) / UART0 (CYD). Lines not starting with `{` are ignored.
  All logs are emitted as `{"t":"log"}` while a host is connected.
- `TcpTransport`: client, backoff 1 s → 60 s. `link.tcp_host`: `""` = no TCP, `"host:port"` =
  fixed, `"auto"` = find `buddy-link` via mDNS (`_blinky._tcp`, ESP-IDF mdns query, first
  answer with matching `proto`). The emulator implements `auto` (`buddy-sim --host auto`).
- `MqttTransport`: maps topics ⇄ messages (see PROTOCOL.md §MQTT), LWT for status.
- `HostLink`: one instance per active transport. Handles the handshake, ping/timeout,
  dispatch to DataStore / MoodEngine / PageManager, and `ack`/`err`.

### WiFi / Standalone
- Boot: try stored credentials (10 s). On failure, start AP `Blinky-Setup` with a captive
  portal for 3 min (or when the button is held at boot), then continue offline.
- `StandaloneFetcher`: runs the device-native sources assigned to the device by the
  SourceRouter: NTP on connect + every 6 h; Open-Meteo every 15 min (current, hourly
  precipitation for the next 2 h for the rain warning, 3-day forecast, sunrise/sunset,
  optional air quality); `http` sources. Only runs when internet is reachable.

### Sensors
- SHT40 every 10 s, moving average over 6 samples, user offset (self-heating correction).
- 24 h history ring buffer (5 min resolution = 288 points) for the trend graph.
- Optional BH1750 (auto-brightness, sleepy mood) and SCD40 (CO₂).
- **Sound level** (ws146 on-board mic, I²S; emulated in `buddy-sim`): `sensors/SoundTask`
  reads the mic at 16 kHz and computes the level 10× per second (RMS → dB, calibrated
  roughly to dB SPL). **Only the level is used; no audio is stored or sent.** Disabled with
  `sound.enabled = false`.
  - sudden loud noise (≥ `sound.loud`, default 80 dB, and ≥ 20 dB above the room) → startled
    ("surprised") for 1.5 s, event `{"kind":"sound","name":"loud"}`; 3 s cooldown
  - voices (≥ 10 dB above the room for 0.8 s, pauses < 0.4 s allowed) → "curious" for 2 s
  - lasting noise (30 s average ≥ `sound.too_loud`, default 70 dB) → "angry" mood
  - the room baseline falls fast and rises over ~1 min, so steady noise becomes normal
  - `indoor.noise` (30 s average) in the DataStore and in the `sensor` message

### Eyes renderer
- Based on the [esp32-eyes](https://github.com/liunix61/esp32-eyes) concept: each eye =
  rounded rect + upper/lower lid shapes. Parameters: width, height, x/y offset, corner
  radius, lid top/bottom angle and height, color.
- `EyePreset` per expression (~15: neutral, happy, sad, sleepy, angry, surprised, curious,
  focused, tired, hot, cold, love, skeptical, scared, blink).
- Interpolate between presets (ease-in-out, 150–400 ms). Blink every 2–6 s at random.
  Saccades (look around) with small random offsets.
- A custom LVGL widget (see GUI → Rendering), with the parameter model of
  [esp32-eyes](https://github.com/liunix61/esp32-eyes) and the same presets as the emulator.
- Mini-eyes variant (scaled) for the dashboard corner.

### MoodEngine
- Inputs: DataStore values, time of day, link state, events (notify, button).
- Output: target expression + weights for idle random choice (weighted roulette as in
  esp32-eyes).
- Priority: host override (`mood` with ttl) > alert (notify) > rules > time of day > idle.
- Rules are a table of `{key, op, threshold, mood, weight}` stored in config. Defaults
  are in the main plan §4.

### Pages (PageManager)
- Built-in: Eyes, Dashboard, Clock (analog), Forecast, Indoor (trend), Host data, Countdown, Alarm, Pomodoro.
- `ui.pages` picks and orders them (setup page: checkboxes + ↑/↓). If Eyes is hidden, "swipe
  up" goes to the first page. A hidden Alarm page still shows while an alarm rings, and a
  finished countdown comes to the front the same way.
- **Countdown** (emulated in `buddy-sim`): swipe up/down on the minutes (left half) or seconds
  (right half) to set 00:00–99:59, values wrap. **Tap** starts, pauses and resumes; long
  press resets to the last set time. A ring along the screen edge shows the time left.
  At zero the page comes to the front, it beeps, the eyes look surprised, and it rings
  until tapped (max 1 min); a tap on any page stops it. The time left shows in the status bar on other pages. Events
  to the host: `{"kind":"countdown","state":"started|paused|reset|done|stopped"}`.
- **Alarm** (emulated in `buddy-sim`): alarms live in NVS and use the RTC, so they ring
  without WiFi or host (once the clock was set). The page shows one alarm: swipe up/down on
  the hour (left) or minute (right, 5-min steps), tap = on/off, long press = next alarm.
  Weekdays are set in the web config. When an alarm rings, the page comes to the front, it
  beeps and the eyes look surprised (also in night mode): **tap = snooze, hold = stop**
  (`alarm.snooze_min`, default 9). It stops by itself after 5 min. ⏰ in the status bar when
  an alarm is on. Events: `{"kind":"alarm","name":"07:00","state":"ringing|snoozed|stopped"}`.
- Custom pages from the `page` message: widgets `value`, `bar`, `icon+text`, `list`,
  `sparkline`. Max 8 custom pages, each with a TTL.
- Rotation with per-page duration. Touch (see below), button (short = next page, long =
  Pomodoro) and motion gestures all switch pages.
- Night mode: backlight at `ui.brightness_night` and sleepy eyes in the night hours (or in low light).
- Idle dimming: after `ui.dim_after` s without touch, the backlight drops to `ui.dim_day` /
  `ui.dim_night`. Touch, gestures, notifications and ringing alarms wake it (emulated in `buddy-sim`).
- Status bar: time, WiFi/internet, serial, TCP, MQTT icons, stale marker.

### Touch gestures (ws146: SPD2010 capacitive touch; CYD: XPT2046 resistive)
For use while the buddy sits on the desk. Handled by `ui/TouchInput` (LVGL indev + own
gesture recognizer), which emits the same `GestureEvent`s as the motion gestures, so
PageManager handles both the same way.

| Gesture | Action |
|---|---|
| **Swipe left / right** | next / previous page (page slides along, smartwatch-style) |
| **Tap** on the Eyes page | eyes react (blink, happy, "boop") |
| **Tap** on an item | open details (e.g. forecast day, VM list, CI pipeline) |
| **Long press** (> 800 ms) | Pomodoro start/stop (on the Eyes/Pomodoro page) |
| **Long press** elsewhere | quick menu: brightness, do-not-disturb, page rotation on/off |
| **Swipe down** | notification history (last 10) |
| **Swipe up** | back to the Eyes page |
| **Swipe up / down** on the Countdown / Alarm page | set minutes / hour (left half) or seconds / minutes (right half) instead |
| **Tap / hold** while an alarm rings (any page) | snooze / stop |
| **Shake** (IMU) | dizzy eyes, hides a notification; never starts or stops the countdown or an alarm |

- Round screen: swipes are recognized anywhere in the circle. Tap targets are ≥ 48 px and
  kept inside the inner ~290×290 area.
- Any touch pauses auto-rotation for 30 s and wakes the screen from night mode.
- Reported to the host as `event` `{"kind":"touch","name":"swipe_left"}`.

### Motion gestures (ws146, QMI8658 IMU)
Switch pages by moving the device. Touch and buttons still work.

| Gesture | Detection | Action |
|---|---|---|
| **Flick / twist** right or left (quick rotation about the screen axis and back, like turning a dial) | gyro Z > ~150 °/s, then return to start within 400 ms | next / previous page |
| **Side tap** (nudge the case left/right) | accel spike on X axis > ~1.5 g, sign = direction | next / previous page |
| **Tilt & hold** (when held in hand) | gravity vector tilted > 25° for > 300 ms | scroll in that direction; the eyes look that way first |
| **Double tap** on top/front | 2 spikes within 300 ms (or the IMU's built-in tap engine, if usable) | Pomodoro start/stop or back to the Eyes page |
| **Shake** | > 3 direction changes within 1 s | "dizzy" eyes, dismiss notification; on the Countdown page: start / pause / stop alarm |
| **Pick up / face down** | motion after rest / Z gravity < −0.8 g | wake from night mode / screen off (do-not-disturb) |

- `sensors/MotionTask`: reads the IMU at 100 Hz (or uses its FIFO), low-pass filter for gravity,
  high-pass for taps. Emits `GestureEvent`s to the EventBus, and PageManager and MoodEngine subscribe.
- **False triggers:** 600 ms cooldown after a gesture, the device must return to rest before the
  next gesture, thresholds adjustable in config, and gestures are ignored while the device
  is being carried (continuous motion). Desk vibration from typing stays well below the tap threshold.
  This needs tuning on real hardware.
- **Feedback:** the eyes glance in the gesture direction, and the page slides in from that side.
  An optional short click sound plays.
- Gestures are reported to the host as `event` `{"kind":"gesture","name":"flick_right"}`, so host
  scripts can react too.
- Config: each gesture can be turned on/off and remapped (`gesture.flick_right = next_page`).

## Task layout
| Task | Core | Prio | Period |
|---|---|---|---|
| UI (LVGL task from `esp_lvgl_port`) | 1 | high | 33 ms |
| Link (all transports) | 0 | mid | event-driven, 10 ms poll |
| Net (WiFi, fetcher, OTA) | 0 | mid | event-driven |
| Sensors | 0 | low | 10 s |
| Sound level (ws146 mic) | 0 | low | 100 ms |
| Motion (IMU, ws146) | 0 | mid | 10 ms |

## Memory budget (ws146: 8 MB octal PSRAM, 16 MB flash)
- LVGL draw buffers 2 × 412×40×2 = 66 KB (internal DMA RAM); optional full frames
  2 × 340 KB in PSRAM
- amoled175: 2 × 466×40×2 = 75 KB internal DMA RAM, plus the rotation buffer with `ui.rotation`
- Internal RAM is scarce (about 310 KB at boot): malloc() goes to PSRAM first
  (`SPIRAM_MALLOC_ALWAYSINTERNAL=0`), 64 KB stay reserved for internal/DMA requests, TLS in
  PSRAM. With the default (below 16 KB internal) LVGL filled it within 3 s on the AMOLED-1.75
- LVGL heap and widget tree in PSRAM; fonts and icons in flash (memory-mapped)
- JSON documents ≤ 4 KB; DataStore ≤ 128 entries; 24 h indoor history 288 × 2 values
- Two OTA slots of 6 MB each

## Milestone breakdown
| Main | Firmware tasks |
|---|---|
| M0 | ESP-IDF project, `board_ws146` bring-up (display, touch, backlight), LVGL "hello" on the round screen, desktop simulator, CI build (idf Docker image) + host tests |
| M1 | Eyes widget with blink/saccade and 3 presets, FPS spike (partial vs. full-frame buffers), fonts/icons pipeline, first screenshot tests |
| M2 | SensorTask + DataStore + Dashboard page; WASM build of the UI shown in `buddy-sim` |
| M3 | `protocol` component, USB serial transport, HostLink, ConfigStore + sources list, `sub`/`unsub` for time + weather, data handling, status bar |
| M4 | WifiService + captive portal, web config UI, StandaloneFetcher, SourceRouter (device vs. host), TZ table |
| M5 | TcpTransport + mDNS discovery + reconnect, moving subscriptions between hosts |
| M6 | Source priority, stale handling, offline mode |
| M7 | MoodEngine + all presets, page rotation, night mode, notify, Pomodoro, touch gestures (TouchInput), motion gestures (MotionTask) |
| M8 | Custom pages (`page` message), host-only sources (gitlab, calendar, proxmox) in config UI, NVS encryption for secrets |
| M9 | MqttTransport + Home Assistant discovery |
| M10 | OTA, ESP Web Tools manifest + Web Serial setup page, config export/import, sensor calibration UI |

## To do
Status of the firmware work by milestone: `[x]` = done, `[ ]` = open (GitHub shows them as checkboxes).

### M0 – project and board bring-up
- [x] ESP-IDF 6.1 project, partition table (2 × 6 MB OTA), `sdkconfig.defaults` (octal PSRAM, 16 MB flash)
- [x] `board_ws146`: power latch, I²C, TCA9554 resets, SPD2010 display (QSPI 40 MHz), touch, backlight, `esp_lvgl_port` + rounder
- [x] Portable `ui` component with theme tokens, round-screen geometry and the hello screen (rim ring, eyes, version, touch tester)
- [x] Desktop simulator (SDL2), headless screenshot tool, host tests (doctest)
- [x] CI: host tests + screenshots; ESP32-S3 build in `espressif/idf:v6.1` with merged flash image; `dependencies.lock`
- [ ] **On the board:** display comes up, rim ring is round and centred, colours right (byte order)
- [x] **On the board:** touch works without problems (AMOLED-1.75, checked 2026-10-29)
- [ ] **On the board:** backlight works, board stays on from battery (power latch)
- [x] Local ESP-IDF setup documented for flashing (`idf.py flash monitor`, firmware/README.md)
- [ ] **On the board:** first boot log checked

### M1 – eyes, fonts, screenshot tests
- [x] Eyes as a custom LVGL widget with the emulator's parameter model and all 16 presets (masked layer, glow, seam-free lids)
- [x] Blink and saccades (random timing), smooth interpolation between presets, dizzy circling, cold shiver
- [x] App frame: eyes screen (tap = next mood, demo) and info screen with the touch tester, swipe to switch
- [x] Emulator extras on the eyes: sweat drop (hot), "z" (sleepy), heart (love)
- [x] FPS counter (LVGL performance monitor, enabled in `sdkconfig.defaults`)
- [x] Glow off (an LVGL shadow, recomputed every frame and every 40-line strip), redraw every 16 ms (was 33 ms, drifting against the eyes' 33 ms timer), FPS/CPU counter with `ui.fps`
- [ ] **On the board:** the eyes were a little laggy on the AMOLED-1.75 (2026-10-29): with glow off and 16 ms: 10-20 FPS, CPU near 100 %, 49 ms per frame (45 render, 5 flush), although only ~42,000 px per frame change. Build was `-Og` (ESP-IDF's default); now `-O2`: measure again. Idle pages (alarm, countdown) redrew about 11 times a second (60 % CPU), because setting a label's text or colour redraws it even when unchanged; fixed with the setters in `ui/src/set.h`. At -O2 and idle-fixed: eyes 19 FPS, 41 ms (38 render | 3 flush), other pages near 0 % CPU. Next: data cache 64 KB / 64-byte lines and XIP from PSRAM (rotation buffer only when ui.rotation is not 0 at start, so the cache fits): eyes 33-40 FPS, 22 ms (21 render | 1 flush), CPU 60-75 % (AMOLED-1.75, 25902cc, 2026-10-29). Internal RAM 48 KB free, but once down to 7 KB (at start, not again later): watch it. Possible next step: two draw units on both cores. Hardware rotation is no option: the SPD2010 driver can't swap X/Y (no 90°), the CO5300 driver has MADCTL MV but the 6 px gap would need handling and it's untested. If drawing stays slow: LVGL's objects and helper buffers are in PSRAM since `SPIRAM_MALLOC_ALWAYSINTERNAL=0`, bigger caches (data 64 KB, instructions 32 KB) cost internal RAM. Next steps if needed: two draw units on both cores (`LV_USE_OS` FreeRTOS), QSPI 80 MHz, bigger draw buffers in PSRAM (internal RAM has only ~40 KB free)
- [x] `tools/fonts.sh`: lv_font_conv, Inter 14/20/28/40 (Latin-1, °, arrows, €) and Light digits 72/96, pinned and checksummed sources
- [x] Generated fonts committed (`components/ui/fonts/`), UI switched from Montserrat to Inter (`ui/fonts.h`)
- [x] Icon font (Material Icons subset, 24/48 px) + `ui/icons.h` with the symbols; icon row on the info screen
- [x] Screenshot tests: reference PNGs in `test/screenshots/` (all moods + info), compared in CI; `UPDATE_SCREENSHOTS=1` regenerates them

### M2 – data and first real page
- [x] `app_core`: DataStore (priority, TTL, stale, 128 entries, version counter), settings registry (types, ranges, choices, secrets) stored as one NVS blob, units, time zone table (common zones; full table in M4)
- [ ] EventBus: not needed so far (the parts talk through hooks); only if input and link events outgrow them
- [x] SHT40 driver + sensor task (10 s, average of 6, `sensor.*_offset`), tested CRC/conversion
- [ ] **On the board:** SHT40 connected and readings plausible
- [x] Dashboard page (clock, date, weather, indoor, rain hint) bound to the DataStore with `lv_subject`; units; screenshot tests
- [x] PCF85063 RTC driver: restores the system time at boot (setting it follows with NTP/host time in M3/M4)
- [ ] **On the board:** RTC keeps the time over a power cycle
- ~~WASM build of the UI; `buddy-sim` shows it instead of the JavaScript drawing~~ (skipped 2026-10-26: `buddy-sim` keeps its JavaScript drawing; `buddy_sim` and the screenshots cover the firmware UI)

### M3–M6 – host link and data sources
- [x] Protocol in `app_core` (`protocol.h`: NDJSON framing, device messages; golden tests replay `protocol/examples/handshake.ndjson`)
- [x] `HostLink` (portable, host-tested): hello/answer/retry, ping, 30 s silence, subscriptions from `sources` (moved on `host_state`, re-sent after every hello), `time`/`weather`/`data` into the DataStore and the clock (RTC too)
- [x] USB serial transport (`serial_link`: USB-Serial-JTAG, reader task, link runs in the LVGL task, ESP log as `{"t":"log"}` while a host is connected, plug/unplug = link up/down)
- [x] **On the board:** `buddy-link` finds the device on USB and gets its subscriptions, weather arrives through it; `buddy-ctl --direct device config` reads and writes settings (AMOLED-1.75, checked 2026-10-29)
- [ ] **On the board:** the setup page reads and writes settings
- [ ] Opening the port gives a burst of old hellos (sent every 5 s while no host listened, kept in the USB buffer): `buddy-link` starts a session for each, about 18 in one second. Harmless, but the host could ignore a burst like that
- [ ] Log lines from the WiFi driver arrive split in two (`"I (5758) wifi:"`, then the text): `log_vprintf` should collect up to the newline before sending a `log` message
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
- [ ] **On the board:** setup AP + captive portal, Improv from the setup page, web page, NTP, TCP to `buddy-link` (also `auto`)
- [x] **On the board:** Open-Meteo over HTTPS works: weather, AQI and location without a host (AMOLED-1.75, d45b484, checked 2026-10-29; 40 KB internal RAM free after boot, 34 KB at least). Failed on the AMOLED-1.75 with v0.2.0 (`mbedtls_ssl_setup` -0x008D = alloc failed, "Failed to create socket", `esp_mmu_map_virt: no mem` 3 s after boot): the internal RAM was full, because malloc() below 16 KB stayed internal and LVGL makes many small allocations. TLS in PSRAM alone didn't help; now `SPIRAM_MALLOC_ALWAYSINTERNAL=0` and 64 KB reserved
- [ ] **On the board:** sometimes reboots with a white screen (AMOLED-1.75, seen 2026-10-29); cause unknown, not in a 2-minute log. Capture the USB log while it happens (reset reason, backtrace); check free internal RAM

### M7 – behaviour (as in the emulator)
- [x] MoodEngine (`mood` component) with the emulator's rules: reactions (boop, shaken, loud noise, voices) > alarm > countdown > host `mood` > hot/cold > dry > too loud > rain/snow > night > TCP host lost > sunny > neutral
- [x] Pages: forecast (`weather.day0…2`, also from the device's own Open-Meteo request), indoor trend (a sample every 10 s, half an hour), host data (all but `weather.*`/`indoor.*`); screenshot tests
- [x] Custom pages from the `page` message (`core::CustomPages`: up to 8 pages of up to 8 widgets: value, bar, icon+text, list, sparkline; `ttl`, `del`), after the built-in pages, before the info screen; screenshot test
- [x] Analog clock page (`ui::ClockPage`, `clock` in `ui.pages`): 12 marks, hour, minute and second hands, date; no status bar; also in the emulator
- [x] `ui.pages` (selection and order), swipes left/right, swipe up = home: `core::pages`/`check_pages` (validated in the settings, as in the emulator), `ui::App` swipes through them with the info screen last, starts on and goes "home" to the eyes (or the first page), keeps a hidden countdown/alarm page while it runs or rings; pages the firmware lacks would be skipped
- [x] Countdown page (swipe minutes/seconds, tap = start/pause, ring at zero, progress ring)
- [x] Alarm page (alarms in settings, edit on the page, ring, tap = snooze, hold = stop, next alarm)
- [x] Countdown/alarm beeps on the speaker (PCM5101 over I²S, `audio` component)
- [x] The countdown's time left in the status bar on other pages (and the alarm icon while an alarm is on)
- [x] Sound reactions from the I²S mic level (startle, curious, too loud), `indoor.noise`, `sound` event to the host
- [x] Backlight: day/night/dim levels, idle dimming, wake on touch and while something rings (notifications follow with them)
- [x] Display units (°C/°F, km/h/mph/m/s, mm/in)
- [x] Display rotation in 90° steps (`ui.rotation`, degrees clockwise): software rotation in `esp_lvgl_port` (`sw_rotate`, rounders aligned on both axes), LVGL turns the touch points; applied at boot and when the setting changes; `buddy_sim --rotate`; screenshot and touch test (the emulator stores it but doesn't turn its picture)
- [ ] **On the board:** rotation on both panels (alignment after a turn, frame rate with software rotation)
- [x] **On the board:** the speaker beeps at the end of the countdown (AMOLED-1.75, checked 2026-10-29)
- [ ] **On the board:** backlight levels (dimming works on the AMOLED-1.75), beeps loud enough, mic levels plausible (dB calibration), startle/voices thresholds
- [x] Notifications with overlay and sound: `notify` from a host shows a box over every page for `dur` seconds (icon, two lines), sets its mood, wakes the backlight, plays a chime (`Tune::Notify`) if it has a sound; a tap or a shake hides it
- [x] Touch and IMU gestures: touch events to the host; QMI8658 driver (100 Hz) + `core::MotionDetector` (shake: ≥ 4 direction changes > 1 g within 1 s; flick: gyro Z > 150 °/s back within 400 ms; 600 ms cooldown), flick = next/previous page, shake = dizzy eyes only (the countdown and alarms run on taps, since 2026-10-30), reported as `gesture` events
- [ ] **On the board:** QMI8658 found (address, WHO_AM_I), axes and signs right (flick right = next page), shake and flick thresholds; side tap, tilt, double tap, pick up / face down are not built yet

### M8–M10 – later
- [ ] NVS encryption for secrets; host-only sources in the config UI
- [ ] MQTT + Home Assistant discovery
- [ ] OTA updates over WiFi: two OTA partitions with rollback, upload on the web page at `blinky.local` (needs a password or PIN first) and/or pull the board's release app image from GitHub, version check on the info screen
- [x] OTA through `buddy-link` (USB or TCP): `ota` messages (begin with size, SHA-256 and version, numbered Base64 blocks of 1 KiB each acked, end → check, boot partition, restart); `core::OtaReceiver` (portable, tested) + `esp_ota_*` in `app_main`; rollback (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, marked valid once the UI runs); also in the emulator. **On the board:** not tested yet. Firmware before 0.2.0 has no `ota` and ignores it (update timed out, "no reply from device"); `buddy-link` now refuses that with a hint to install once over USB, and the firmware announces `ota` in `caps`
- [x] Release also the app image per board (`blinky-app-<board>.bin`, next to the merged `blinky-firmware-<board>.bin`) for OTA over WiFi and through `buddy-link`
- [x] Firmware release so the setup page can flash it: the pages workflow puts the newest release's image and one ESP Web Tools manifest per board (`site/firmware/manifest-<board>.json`) on the setup page
- [x] **On the board:** `buddy-ctl --direct flash` over the board's own USB port works, after turning off the RTC watchdog (AMOLED-1.75, checked 2026-10-29). Flashing the full image erases the settings (NVS lies in the flashed range)
- [x] **On the board:** flashing from the setup page on GitHub Pages (ESP Web Tools, release image) works without problems (AMOLED-1.75, checked 2026-10-29)
- [ ] Config export/import on the device, sensor calibration
- [x] Settings schema over the link, so `buddy-link` can show a settings page for a device on USB or TCP: `{"t":"config","id":…,"schema_from":0}`, answered page by page (one line holds 2048 bytes) with `settings_schema_page()`; in `protocol/PROTOCOL.md`, a golden example, the emulator and the firmware tests
- [x] Web page address to the host: `{"t":"web","url":"http://<ip>/"}` after the hello and when the WiFi address changes (`""` = none), so the `buddy-link` web UI links to the device's own page, also over USB; in `protocol/PROTOCOL.md`, a golden example, the emulator and the firmware tests

### Second board: Waveshare ESP32-S3-Touch-AMOLED-1.75 (plain, no GPS)
Replaces the AMOLED-1.43 plan. Hardware facts and open questions:
[hardware plan](../hardware/PLAN.md#second-board-waveshare-esp32-s3-touch-amoled-175-round-466466).
- [x] Layout from the screen size: designs stay in 412 px coordinates and `ui::place()` keeps them centred (same pixel density, so same sizes); screenshot tests at 466 px
- [x] Board choice at build time: one `board` component (Kconfig "Blinky board", one source file per board), `idf.py -D BOARD=amoled175`, `boards/<board>.defaults`, one build folder per board
- [x] `board_amoled175.c`: AXP2101 (DC1 and ALDO1 at 3.3 V, charging), CO5300 over QSPI (init as in xiaozhi-esp32, 6 px column gap, even/odd rounder), brightness with command `0x51`, CST9217 touch (mirrored X and Y)
- [x] Audio through the board API (`board_speaker_*`, `board_mic_*`, 16-bit mono at 16 kHz on both boards): ES8311 + ES7210 with `esp_codec_dev` on the 1.75, the I²S code of the ws146 moved into its board file
- [x] Battery level and charging from the AXP2101 in the status bar (icon while charging or below 20 %) and on the web page
- [x] CI and release build both boards (`blinky-firmware-<board>.bin`); the setup page asks for the board (`firmware/boards.json`, one ESP Web Tools manifest per board)
- [ ] AMOLED burn-in: shift eyes and status bar by a few pixels every few minutes, dim further when idle, offer screen off at night (`ui.dim_night = 0`)
- [ ] PWR and BOOT buttons (short = next page, …); switch off through the AXP2101
- [ ] Speaker volume setting (`sound.volume`; the ES8311 has a volume control) in the registry, the emulator and the setup page
- [x] **On the board:** flashed from the setup page, display comes up, touch works (not mirrored), WiFi connects, the speaker beeps, dimming works (checked 2026-10-29); the microphones work: about 52 dB in the room, "loud" sound events (checked 2026-10-30). One I²C read from the ES7210 fails at each start, just before the input is opened a second time; the open succeeds
- [x] **On the board:** white flashing at the rim at about 5 o'clock is gone (it was LVGL's performance monitor, now off; checked 2026-10-29)
- [ ] **On the board:** colours, 6 px offset, no torn edges, AXP2101 rails (mics powered), battery percentage plausible, mic level calibrated (`BOARD_MIC_SPL_AT_FULL_SCALE`), where to connect the SHT40 (I²C 14/15)

## Testing
- **Host tests** (`firmware/test`, CMake + doctest, no hardware): DataStore (priority/TTL),
  protocol encode/decode against the golden files in `protocol/examples/`, MoodEngine
  rules, settings validation. Run in CI next to the Go tests.
- **Screenshot tests:** the desktop simulator renders each page with fixed data; CI compares
  them with reference PNGs.
- **Firmware build** in CI with the `espressif/idf:v6.1` image; the merged image is a
  build artifact (later published for the setup page).
- **Against hosts without hardware:** the WASM UI inside `buddy-sim` talks to `buddy-link`
  like a device; `buddy-sim host` (planned) sends scripted message sequences.
- On-device: hidden debug page (FPS, heap, PSRAM, link stats, last messages).
