# Firmware Plan (ESP32)

Part of the [main plan](../PLAN.md). Protocol details: [protocol/PROTOCOL.md](../protocol/PROTOCOL.md).

## Goals
- Animated mood eyes at ≥ 25 FPS on a 320×240 TFT
- Works with no link at all (eyes + local sensor), standalone over WiFi, and fed by a host
  over serial / TCP / MQTT. Several sources can run at the same time.
- **The device owns all configuration** (NVS): sources, locations, tokens, pages, moods.
  Hosts are interchangeable gateways that only fetch what the device asks for.
- All behaviour configurable at runtime without reflashing, and without any host app

## Toolchain
- PlatformIO, Arduino-ESP32 core 3.x (ESP-IDF 5.x underneath)
- Environments in `platformio.ini`:
  - `cyd` – ESP32-2432S028 "Cheap Yellow Display" (prototype, M1–M3)
  - `s3` – ESP32-S3 N16R8 + ILI9341/ST7789 (DIY target)
  - `ws146` – Waveshare ESP32-S3-Touch-LCD-1.46, round 412×412 (recommended target, see
    [hardware plan](../hardware/PLAN.md))
  - `native` – host unit tests (no Arduino)
- **Display abstraction:** `ui/Display` hides the driver: LovyanGFX for SPI TFTs, ESP-IDF
  `esp_lcd` SPD2010 (QSPI) for the round Waveshare board. The eyes renderer draws into a
  plain RGB565 buffer, so it works with both. Pages use a `Layout` (rect vs. round) so
  widgets stay inside the visible circle.
- **Board extras (ws146):** QMI8658 IMU (tap/shake/tilt events → EventBus), I²S audio
  (notify sounds, WAV from flash/TF), PCF85063 RTC (time when offline), battery level.
- Libraries: LovyanGFX, LVGL 9, ArduinoJson 7, Sensirion SHT4x, WiFiManager, ESPmDNS,
  PubSubClient (or esp-mqtt), ArduinoOTA

## Directory layout
```
firmware/
  platformio.ini
  include/board/        board_cyd.h, board_s3.h   (pins, display/touch config)
  src/
    main.cpp            setup: config → display → tasks
    core/               DataStore, ConfigStore, EventBus, Log
    net/                WifiService, StandaloneFetcher (NTP, Open-Meteo, http), WebConfig, Ota
    sources/            SourceConfig, SourceRouter (device vs. host per source), TzTable
    link/               Transport.h, SerialTransport, TcpTransport, MqttTransport, HostLink
    sensors/            SensorTask, Sht4x, Bh1750 (optional), Scd40 (optional)
    mood/               MoodEngine, MoodRules
    ui/                 Display (LovyanGFX), Eyes, Pages/*, StatusBar, PageManager
  lib/protocol/         message encode/decode — no Arduino deps, also built for `native`
  test/                 test_datastore, test_protocol, test_mood (Unity, native env)
```

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
   (<https://stefanriese.github.io/esp32_desk_buddy/>, source in [`site/`](../site/), same
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
- `TcpTransport`: client, host from config or mDNS `_deskbuddy._tcp`, backoff 1 s → 60 s.
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
- Drawn into a PSRAM sprite (S3) / partial sprite (CYD) and pushed via DMA. No LVGL in the
  eye path, for speed. LVGL is only used for the other pages.
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
| UI (eyes/LVGL) | 1 | high | 33 ms |
| Link (all transports) | 0 | mid | event-driven, 10 ms poll |
| Net (WiFi, fetcher, OTA) | 0 | mid | event-driven |
| Sensors | 0 | low | 10 s |
| Sound level (ws146 mic) | 0 | low | 100 ms |
| Motion (IMU, ws146) | 0 | mid | 10 ms |

## Memory budget (S3 target)
- Eye sprite 320×240×16 bit = 150 KB (PSRAM)
- LVGL draw buffers 2× 320×40×2 = 50 KB (internal RAM, DMA)
- JSON documents ≤ 4 KB; DataStore ≤ 128 entries
- CYD (no PSRAM): eyes drawn in horizontal bands, smaller LVGL buffer

## Milestone breakdown
| Main | Firmware tasks |
|---|---|
| M0 | PlatformIO project, `cyd` + `s3` + `native` envs, CI build, blank screen + log |
| M1 | Display driver (spike: SPD2010 QSPI FPS on ws146), Eyes renderer with blink/saccade, 3 presets, FPS counter |
| M2 | SensorTask + DataStore + Dashboard page (LVGL) |
| M3 | `lib/protocol`, SerialTransport, HostLink, ConfigStore + sources list, `sub`/`unsub` for time + weather, data handling, status bar |
| M4 | WifiService + captive portal, web config UI, StandaloneFetcher, SourceRouter (device vs. host), TZ table |
| M5 | TcpTransport + mDNS discovery + reconnect, moving subscriptions between hosts |
| M6 | Source priority, stale handling, offline mode |
| M7 | MoodEngine + all presets, page rotation, night mode, notify, Pomodoro, touch gestures (TouchInput), motion gestures (MotionTask) |
| M8 | Custom pages (`page` message), host-only sources (gitlab, calendar, proxmox) in config UI, NVS encryption for secrets |
| M9 | MqttTransport + Home Assistant discovery |
| M10 | OTA, ESP Web Tools manifest + Web Serial setup page, config export/import, sensor calibration UI |

## Testing
- `pio test -e native`: DataStore (priority/TTL), protocol encode/decode (golden files
  from `protocol/examples/`), MoodEngine rules.
- On-device: hidden debug page (FPS, heap, PSRAM, link stats, last messages).
- Host simulator `buddysim host` (from `host/cmd/buddysim`) sends scripted message sequences.
