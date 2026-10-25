# Firmware Plan (ESP32)

Part of the [main plan](../PLAN.md). Protocol details: [protocol/PROTOCOL.md](../protocol/PROTOCOL.md).

## Goals
- Animated mood eyes at ≥ 25 FPS on a 320×240 TFT
- Works with no link at all (eyes + local sensor), standalone over WiFi, and fed by a host
  over serial / TCP / MQTT. Several sources can run at the same time.
- All behaviour configurable at runtime (NVS) without reflashing

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
    net/                WifiService, StandaloneFetcher (Open-Meteo, NTP), Ota
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
- Source priority per key prefix (default `local > host > standalone`), configurable.
- `get()` returns value + `isStale`. Change subscribers are notified via EventBus.
- Guarded by a FreeRTOS mutex. UI reads a snapshot once per frame.

### ConfigStore (NVS)
WiFi SSID/password, location (lat/lon, name), POSIX TZ, units, host (TCP host/port, token),
MQTT (broker, user, topic prefix), brightness/night hours, page list and durations,
mood rule thresholds, sensor offsets. Set via captive portal, `config` messages, or MQTT.

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
- `StandaloneFetcher`: NTP on connect + every 6 h. Open-Meteo every 15 min: current,
  hourly precipitation for the next 2 h (rain warning), 3-day daily forecast, sunrise/sunset,
  optional air quality. Only runs when internet is reachable.
- Host data with the same keys takes priority by default (configurable).

### Sensors
- SHT40 every 10 s, moving average over 6 samples, user offset (self-heating correction).
- 24 h history ring buffer (5 min resolution = 288 points) for the trend graph.
- Optional BH1750 (auto-brightness, sleepy mood) and SCD40 (CO₂).

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
- Built-in: Eyes, Dashboard, Forecast, Indoor (trend), Host data, Pomodoro.
- Custom pages from the `page` message: widgets `value`, `bar`, `icon+text`, `list`,
  `sparkline`. Max 8 custom pages, each with a TTL.
- Rotation with per-page duration. Touch/button: next page, long press = Pomodoro start/stop.
- Night mode: dim the backlight (PWM) and show only the sleepy eyes in configured hours or in low light.
- Status bar: time, WiFi/internet, serial, TCP, MQTT icons, stale marker.

## Task layout
| Task | Core | Prio | Period |
|---|---|---|---|
| UI (eyes/LVGL) | 1 | high | 33 ms |
| Link (all transports) | 0 | mid | event-driven, 10 ms poll |
| Net (WiFi, fetcher, OTA) | 0 | mid | event-driven |
| Sensors | 0 | low | 10 s |

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
| M3 | `lib/protocol`, SerialTransport, HostLink, time/weather/data handling, status bar |
| M4 | WifiService + captive portal, StandaloneFetcher, ConfigStore |
| M5 | TcpTransport + mDNS discovery + reconnect |
| M6 | Source priority, stale handling, offline mode |
| M7 | MoodEngine + all presets, page rotation, night mode, notify, Pomodoro |
| M8 | Custom pages (`page` message) |
| M9 | MqttTransport + Home Assistant discovery |
| M10 | OTA, ESP Web Tools manifest, sensor calibration UI |

## Testing
- `pio test -e native`: DataStore (priority/TTL), protocol encode/decode (golden files
  from `protocol/examples/`), MoodEngine rules.
- On-device: hidden debug page (FPS, heap, PSRAM, link stats, last messages).
- Host simulator `tools/fake_host.py` sends scripted message sequences.
