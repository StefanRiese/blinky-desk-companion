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
    board_ws146/         pins, QSPI + SPD2010 + touch + TCA9554, backlight, esp_lvgl_port setup
  sim/                   desktop build of the portable parts (SDL2 window, screenshots)
  tools/                 fonts.sh (lv_font_conv), icons, merge-bin
  test/                  host unit tests (CMake + doctest), run in CI without hardware
```
**Portability rule:** `app_core`, `mood` and `ui` include only standard C++,
LVGL and ArduinoJson. Hardware is behind small interfaces (`Clock`, `Settings`, `Backlight`,
`Sound`, `Haptics`) that `board_ws146` and `sim/` implement. That is what lets the same UI
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
  (Emscripten, as in LVGL's `lv_web_emscripten`). `buddysim` will show that instead of its
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
1. **Web UI on the device** (`http://deskbuddy.local`, and on the `DeskBuddy-Setup` AP as
   captive portal): all settings, generated from the registry. Small static page in flash,
   JSON API on the device.
2. **Web Serial setup page** on the project's GitHub Pages site
   (<https://stefanriese.github.io/blinky-desk-companion/>, source in [`site/`](../site/), same
   page as the ESP Web Tools flasher). Works over USB in Chrome/Edge on any OS, even when
   the device has no WiFi. `buddyd` releases the serial port while it is open (tray
   "Configure device…"). It says `hello` with `"role":"setup"` and then uses `config`
   get/set (see PROTOCOL). Already working against the emulator (`buddysim --site`).
   - **Flashing:** ESP Web Tools reads `site/firmware/manifest.json`. The firmware release
     workflow must publish one merged image per board there (`chipFamily: "ESP32-S3"`,
     single part at offset 0, built with `esptool merge_bin`). Until then the page says
     that no firmware is published.
   - **WiFi right after flashing:** the firmware should implement **Improv Serial**, so ESP
     Web Tools offers "Connect to WiFi" at the end of the install, before step 2.
3. **Touch quick menu** for everyday things: brightness, do-not-disturb, page rotation.
4. **`config` messages** from `buddyctl device config`, MQTT or scripts.

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
  fixed, `"auto"` = find `buddyd` via mDNS (`_deskbuddy._tcp`, ESP-IDF mdns query, first
  answer with matching `proto`). The emulator implements `auto` (`buddysim --host auto`).
- `MqttTransport`: maps topics ⇄ messages (see PROTOCOL.md §MQTT), LWT for status.
- `HostLink`: one instance per active transport. Handles the handshake, ping/timeout,
  dispatch to DataStore / MoodEngine / PageManager, and `ack`/`err`.

### WiFi / Standalone
- Boot: try stored credentials (10 s). On failure, start AP `DeskBuddy-Setup` with a captive
  portal for 3 min (or when the button is held at boot), then continue offline.
- `StandaloneFetcher`: runs the device-native sources assigned to the device by the
  SourceRouter: NTP on connect + every 6 h; Open-Meteo every 15 min (current, hourly
  precipitation for the next 2 h for the rain warning, 3-day forecast, sunrise/sunset,
  optional air quality); `http` sources. Only runs when internet is reachable.

### Sensors
- SHT40 every 10 s, moving average over 6 samples, user offset (self-heating correction).
- 24 h history ring buffer (5 min resolution = 288 points) for the trend graph.
- Optional BH1750 (auto-brightness, sleepy mood) and SCD40 (CO₂).
- **Sound level** (ws146 on-board mic, I²S; emulated in `buddysim`): `sensors/SoundTask`
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
- Built-in: Eyes, Dashboard, Forecast, Indoor (trend), Host data, Countdown, Alarm, Pomodoro.
- `ui.pages` picks and orders them (setup page: checkboxes + ↑/↓). If Eyes is hidden, "swipe
  up" goes to the first page. A hidden Alarm page still shows while an alarm rings, and a
  finished countdown comes to the front the same way.
- **Countdown** (emulated in `buddysim`): swipe up/down on the minutes (left half) or seconds
  (right half) to set 00:00–99:59, values wrap. **Shake** starts, pauses and resumes; long
  press resets to the last set time. A ring along the screen edge shows the time left.
  At zero the page comes to the front, it beeps, the eyes look surprised, and it rings
  until shaken (max 1 min). The time left shows in the status bar on other pages. Events
  to the host: `{"kind":"countdown","state":"started|paused|reset|done|stopped"}`.
- **Alarm** (emulated in `buddysim`): alarms live in NVS and use the RTC, so they ring
  without WiFi or host (once the clock was set). The page shows one alarm: swipe up/down on
  the hour (left) or minute (right, 5-min steps), tap = on/off, long press = next alarm.
  Weekdays are set in the web config. When an alarm rings, the page comes to the front, it
  beeps and the eyes look surprised (also in night mode): **shake = stop, tap = snooze**
  (`alarm.snooze_min`, default 9). It stops by itself after 5 min. ⏰ in the status bar when
  an alarm is on. Events: `{"kind":"alarm","name":"07:00","state":"ringing|snoozed|stopped"}`.
- Custom pages from the `page` message: widgets `value`, `bar`, `icon+text`, `list`,
  `sparkline`. Max 8 custom pages, each with a TTL.
- Rotation with per-page duration. Touch (see below), button (short = next page, long =
  Pomodoro) and motion gestures all switch pages.
- Night mode: backlight at `ui.brightness_night` and sleepy eyes in the night hours (or in low light).
- Idle dimming: after `ui.dim_after` s without touch, the backlight drops to `ui.dim_day` /
  `ui.dim_night`. Touch, gestures, notifications and ringing alarms wake it (emulated in `buddysim`).
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
| **Tap / shake** while an alarm rings (any page) | snooze / stop |

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
- LVGL heap and widget tree in PSRAM; fonts and icons in flash (memory-mapped)
- JSON documents ≤ 4 KB; DataStore ≤ 128 entries; 24 h indoor history 288 × 2 values
- Two OTA slots of 6 MB each

## Milestone breakdown
| Main | Firmware tasks |
|---|---|
| M0 | ESP-IDF project, `board_ws146` bring-up (display, touch, backlight), LVGL "hello" on the round screen, desktop simulator, CI build (idf Docker image) + host tests |
| M1 | Eyes widget with blink/saccade and 3 presets, FPS spike (partial vs. full-frame buffers), fonts/icons pipeline, first screenshot tests |
| M2 | SensorTask + DataStore + Dashboard page; WASM build of the UI shown in `buddysim` |
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
- [ ] **On the board:** touch dot follows the finger, taps counted, coordinates not mirrored
- [ ] **On the board:** backlight works, board stays on from battery (power latch)
- [ ] Local ESP-IDF setup documented for flashing (`idf.py flash monitor`), first boot log checked

### M1 – eyes, fonts, screenshot tests
- [x] Eyes as a custom LVGL widget with the emulator's parameter model and all 16 presets (masked layer, glow, seam-free lids)
- [x] Blink and saccades (random timing), smooth interpolation between presets, dizzy circling, cold shiver
- [x] App frame: eyes screen (tap = next mood, demo) and info screen with the touch tester, swipe to switch
- [x] Emulator extras on the eyes: sweat drop (hot), "z" (sleepy), heart (love)
- [x] FPS counter (LVGL performance monitor, enabled in `sdkconfig.defaults`)
- [ ] **On the board:** FPS spike: partial buffers vs. full-frame PSRAM buffers, QSPI 40 vs. 80 MHz, glow on/off
- [x] `tools/fonts.sh`: lv_font_conv, Inter 14/20/28/40 (Latin-1, °, arrows, €) and Light digits 72/96, pinned and checksummed sources
- [x] Generated fonts committed (`components/ui/fonts/`), UI switched from Montserrat to Inter (`ui/fonts.h`)
- [x] Icon font (Material Icons subset, 24/48 px) + `ui/icons.h` with the symbols; icon row on the info screen
- [x] Screenshot tests: reference PNGs in `test/screenshots/` (all moods + info), compared in CI; `UPDATE_SCREENSHOTS=1` regenerates them

### M2 – data and first real page
- [x] `app_core`: DataStore (priority, TTL, stale, 128 entries, version counter), settings registry (types, ranges, choices, secrets) stored as one NVS blob, units, time zone table (common zones; full table in M4)
- [ ] EventBus (when input and link events need it)
- [x] SHT40 driver + sensor task (10 s, average of 6, `sensor.*_offset`), tested CRC/conversion
- [ ] **On the board:** SHT40 connected and readings plausible
- [x] Dashboard page (clock, date, weather, indoor, rain hint) bound to the DataStore with `lv_subject`; units; screenshot tests
- [x] PCF85063 RTC driver: restores the system time at boot (setting it follows with NTP/host time in M3/M4)
- [ ] **On the board:** RTC keeps the time over a power cycle
- ~~WASM build of the UI; `buddysim` shows it instead of the JavaScript drawing~~ (skipped 2026-10-26: `buddysim` keeps its JavaScript drawing; `buddy_sim` and the screenshots cover the firmware UI)

### M3–M6 – host link and data sources
- [x] Protocol in `app_core` (`protocol.h`: NDJSON framing, device messages; golden tests replay `protocol/examples/handshake.ndjson`)
- [x] `HostLink` (portable, host-tested): hello/answer/retry, ping, 30 s silence, subscriptions from `sources` (moved on `host_state`, re-sent after every hello), `time`/`weather`/`data` into the DataStore and the clock (RTC too)
- [x] USB serial transport (`serial_link`: USB-Serial-JTAG, reader task, link runs in the LVGL task, ESP log as `{"t":"log"}` while a host is connected, plug/unplug = link up/down)
- [ ] **On the board:** `buddyd` finds the device on USB, time and weather arrive, the setup page reads and writes settings
- [x] `config` get/set (typed values from the registry, secrets masked, all-or-nothing `set_many`) incl. the setup page's `role: "setup"` (config only, no token, no subs)
- [ ] Device events to the host (countdown, alarm, touch) and `sensor` messages; `mood`/`notify`/`page` from the host (with M7)
- [ ] Link state on the info screen (host, sources and their state)
- [ ] WiFi (station + setup AP), Improv Serial, web config, SNTP
- [ ] StandaloneFetcher (Open-Meteo, AQI, `http`) and SourceRouter (device vs. host)
- [ ] TCP transport with mDNS discovery (`link.tcp_host = "auto"`), reconnect, moving subscriptions
- [ ] Status bar (time, temperature, link icons, stale marker)

### M7 – behaviour (as in the emulator)
- [ ] MoodEngine with all presets and rules (hot, cold, dry, rain, night, host link lost, …)
- [ ] Pages: forecast, indoor trend, host data, custom pages from the `page` message
- [ ] `ui.pages` (selection and order), swipes left/right, swipe up = home
- [x] Countdown page (swipe minutes/seconds, start/pause, ring at zero, progress ring); until the IMU: tap = shake
- [x] Alarm page (alarms in settings, edit on the page, ring, snooze, next alarm); until the IMU: hold = stop
- [ ] Countdown/alarm sound (I²S) and the time left in a status bar on other pages
- [ ] Sound reactions from the I²S mic level (startle, curious, too loud) and `indoor.noise`
- [ ] Backlight: day/night/dim levels, idle dimming, wake on touch/notification/alarm
- [ ] Display units (°C/°F, km/h/mph/m/s, mm/in)
- [ ] Notifications with overlay and sound; touch and IMU gestures (QMI8658: shake, flick)

### M8–M10 – later
- [ ] NVS encryption for secrets; host-only sources in the config UI
- [ ] MQTT + Home Assistant discovery
- [ ] OTA updates; firmware release with `site/firmware/manifest.json` so the setup page can flash it
- [ ] Config export/import on the device, sensor calibration

## Testing
- **Host tests** (`firmware/test`, CMake + doctest, no hardware): DataStore (priority/TTL),
  protocol encode/decode against the golden files in `protocol/examples/`, MoodEngine
  rules, settings validation. Run in CI next to the Go tests.
- **Screenshot tests:** the desktop simulator renders each page with fixed data; CI compares
  them with reference PNGs.
- **Firmware build** in CI with the `espressif/idf:v6.1` image; the merged image is a
  build artifact (later published for the setup page).
- **Against hosts without hardware:** the WASM UI inside `buddysim` talks to `buddyd`
  like a device; `buddysim host` (planned) sends scripted message sequences.
- On-device: hidden debug page (FPS, heap, PSRAM, link stats, last messages).
