# ESP32 Desk Buddy – Project Plan

A small desk device with a display that shows animated "mood eyes", weather, indoor
temperature/humidity and other data.

**Sub-plans:** [firmware](firmware/PLAN.md) · [host service `buddyd`](host/PLAN.md) ·
[protocol](protocol/PROTOCOL.md) · [hardware](hardware/PLAN.md)

It works in three ways:

| Mode | Link | Who gets the data | Needs WiFi on the ESP32 |
|---|---|---|---|
| **Standalone** | WiFi → Internet | ESP32 fetches it (weather API, NTP) | yes (with internet) |
| **Hosted – Serial** | USB serial → laptop | `buddyd` service on the laptop | no |
| **Hosted – TCP** | WiFi/LAN → Proxmox container | `buddyd` service in the container | yes (LAN only, no internet needed) |

Modes can run together: e.g. standalone weather plus extra data (CPU load, Proxmox status,
calendar) pushed by a host. If the ESP32 has no WiFi, the host supplies everything,
including time and weather.

---

## 1. Hardware

| Part | Recommendation | Why |
|---|---|---|
| MCU | **ESP32-S3** dev board with 8 MB PSRAM (e.g. ESP32-S3-DevKitC-1 N16R8) | Native USB-CDC (serial without a UART bridge), PSRAM for display buffers, plenty of flash for OTA |
| Display | 2.8" / 3.2" **ILI9341 / ST7789 320×240 SPI TFT**, optional capacitive touch (FT6236) | Large enough for the eyes and a dashboard; well supported |
| **Recommended** | **Waveshare ESP32-S3-Touch-LCD-1.46** (round 412×412 touch, IMU, speaker + mic, RTC, battery charger) + external SHT40 | Covers ~80 % of the hardware. Round screen is ideal for eyes. Details in [hardware/PLAN.md](hardware/PLAN.md) |
| Alternative | Ready-made board: **LilyGO T-Display-S3** or **Waveshare ESP32-S3-Touch-LCD-2.8** | No wiring, nice enclosure options |
| Prototype | **"Cheap Yellow Display" ESP32-2432S028** (ESP32 + 2.8" ILI9341 320×240 resistive touch, ~12 €) | Cheap, many LVGL tutorials. Good enough for M1–M3; move to the S3 for the final build (no PSRAM, UART bridge instead of native USB) |
| Temp/Humidity | **SHT40** (I²C) – or **BME280** if pressure is wanted | Accurate and cheap. Place it away from the MCU and backlight (self-heating!) |
| Optional | BH1750 ambient light (auto-brightness/"sleepy" eyes), SCD40 CO₂, piezo buzzer, 1–2 buttons or touch, WS2812 status LED | Nice extras, all I²C/GPIO |
| Power | USB-C (5 V) | Always powered; battery not planned for v1 |

Wiring (example): SPI display on HSPI pins, I²C sensors on a shared bus (SDA/SCL), buttons on
GPIOs with internal pull-ups. Pins go into one `board_config.h` per hardware variant.

---

## 2. System architecture

```
                        ┌──────────────────────── ESP32 firmware ─────────────────────────┐
  Open-Meteo / NTP ◄───►│ StandaloneFetcher ─┐                                            │
                        │                    ├──► DataStore ──► MoodEngine ──► UI (LVGL)  │
  SHT40 / BH1750  ─────►│ SensorTask ────────┤   (key/value,        │          Eyes /    │
                        │                    │    timestamp, TTL,   │          Dashboard │
  Laptop (USB) ◄───────►│ SerialTransport ─┐ │    source priority)  │          / Pages   │
                        │                  ├─► HostLink (protocol) ─┘                     │
  Proxmox CT (TCP)◄────►│ TcpTransport ────┤    ▲ sends sensor data, events, status       │
  MQTT broker / HA ◄───►│ MqttTransport ───┘    (optional)                                │
                        │ ConfigStore (NVS) · WiFi/Provisioning · OTA · ModeManager       │
                        └─────────────────────────────────────────────────────────────────┘
```

### Key ideas
- **One DataStore**: every value (e.g. `weather.now.temp`, `indoor.humidity`,
  `host.cpu`) is stored with value, source, timestamp and TTL. The UI only reads the
  DataStore and never knows where the data came from.
- **Source priority per key**: local sensor > host > standalone fetch (configurable). If a
  value is past its TTL it is shown as stale (greyed out / "--").
- **One protocol, several transports**: serial, TCP and (optionally) MQTT carry the same
  messages. Only the byte pipe differs.
- **ModeManager** decides what is active:
  1. WiFi configured and internet reachable → start StandaloneFetcher.
  2. Serial host detected (`hello` received) → HostLink on serial.
  3. TCP host configured / found via mDNS → HostLink on TCP.
  4. MQTT broker configured → subscribe to the buddy's topics (works alongside 1–3).
  5. Nothing available → offline mode: local sensor + eyes only; time from the RTC once set.
  Host-provided data always gets merged in. When the ESP32 has no internet, the host is
  asked to provide `time` and `weather` (the device advertises what it needs).

---

## 3. Communication protocol (Device ⇄ Host)

**Format:** newline-delimited JSON (NDJSON), one message per line, UTF-8, max 2 KB per
line. Easy to debug with `screen`/`nc`, and ArduinoJson parses it on the ESP32. (CBOR+COBS
can be added later if bandwidth matters. It won't for this project.)

**Serial specifics:** USB-CDC at 115200 baud (the baud rate is effectively ignored with
native USB). Debug logs are wrapped as `{"t":"log",...}` so they never break the stream.
Lines that don't start with `{` are ignored by the host.

**TCP specifics:** the device is a **TCP client** connecting to `buddyd` (default port
`7531`). Host from config or discovered via mDNS `_deskbuddy._tcp`. Reconnects with
backoff. Optional shared-secret token in `hello` (LAN only, no TLS in v1).

**MQTT specifics (optional, like AWTRIX):** same JSON messages, one per MQTT message.
Topics: `deskbuddy/<id>/in/<type>` (host → device) and `deskbuddy/<id>/out/<type>`
(device → host), plus `deskbuddy/<id>/status` (online/offline as last will). Retained
`data` messages let Home Assistant, Node-RED or scripts feed the buddy directly, with no
`buddyd` running. Home Assistant MQTT discovery publishes the indoor sensor as entities.

### Message envelope
```json
{"t":"<type>","id":42,"v":1, ...payload}
```

### Device → Host
| type | purpose |
|---|---|
| `hello` | firmware version, device id, capabilities, `needs:["time","weather"]` |
| `sensor` | `{"temp":22.4,"hum":41.2,"lux":120}` every 30 s |
| `event` | button press, touch, page change |
| `ack` / `err` | reply to host commands with `id` |
| `log` | debug output |

### Host → Device
| type | purpose |
|---|---|
| `hello` | host name, protocol version |
| `time` | epoch + TZ string (POSIX TZ, e.g. `CET-1CEST,M3.5.0,M10.5.0/3`) |
| `weather` | current + 3-day forecast (temp, code, wind, rain probability, sunrise/sunset) |
| `data` | generic key/values: `{"host.cpu":23,"pve.vms_running":7,"cal.next":"Standup 09:30"}` |
| `mood` | override mood for N seconds (`{"mood":"happy","ttl":30}`) |
| `notify` | toast message + optional sound/eye reaction |
| `page` | define/update/remove a custom page (see 4. Pages) |
| `config` | set WiFi credentials, location, brightness, pages, TCP host… (stored in NVS) |
| `ota` | (later) trigger OTA from URL |
| `ping` | keepalive (every 10 s; link considered lost after 30 s) |

Version field `v` allows evolution; unknown types/keys are ignored on both sides.
A shared spec file `protocol/PROTOCOL.md` + JSON schemas keeps firmware and service in sync.

---

## 4. Firmware

**Stack:** PlatformIO, Arduino-ESP32 core 3.x (ESP-IDF 5 underneath), FreeRTOS tasks.
**Libraries:** LovyanGFX (display driver), LVGL 9 (widgets/pages), ArduinoJson 7,
Sensirion SHT4x / Adafruit BME280, WiFiManager (captive portal provisioning), ESPmDNS,
ArduinoOTA / HTTP OTA.

### Modules
| Module | Responsibility |
|---|---|
| `board/` | pin maps per hardware variant |
| `core/DataStore` | thread-safe key/value store with TTL, source priority, change callbacks |
| `core/ConfigStore` | NVS-backed settings (WiFi, location lat/lon, TZ, host, brightness, pages) |
| `net/WifiManager` | connect, captive portal fallback (`DeskBuddy-Setup` AP), reconnect |
| `net/StandaloneFetcher` | Open-Meteo (no API key) every 15 min, NTP sync |
| `link/Transport` | abstract `read/write/connected`; `SerialTransport`, `TcpTransport`, `MqttTransport` (PubSubClient / esp-mqtt) |
| `link/HostLink` | NDJSON framing, dispatch, keepalive, `hello` handshake |
| `sensors/SensorTask` | read SHT40 etc. every 10 s, smoothing, offset calibration |
| `mood/MoodEngine` | derives mood from data and time (see below) |
| `ui/Eyes` | procedural eye renderer (blink, saccades, expressions, transitions) |
| `ui/Pages` | LVGL pages: Eyes, Dashboard, Forecast, Indoor, Host Stats, Notifications |
| `ui/ModeManager` | page rotation, button/touch handling, auto-dim/night mode |

### Task layout
- **UI task** (core 1, high priority, ~30 FPS for the eyes)
- **Net/Link task** (core 0): WiFi, HTTP fetch, serial/TCP I/O
- **Sensor task** (core 0, low priority)
- Communication between tasks only through DataStore + a small event queue.

### Mood eyes
Rendered procedurally (two rounded rectangles + eyelids), in the style of
"RoboEyes"/Cozmo. Parameters: eye size, position, lid angle/height, pupil offset, color.
Each mood is a parameter preset, and transitions are interpolated. The design follows
[esp32-eyes](https://github.com/liunix61/esp32-eyes) (parameter-based emotions instead of
bitmaps), ported from OLED to LovyanGFX on the TFT. When idle, the buddy switches between
neutral-ish expressions by **weighted random choice** so it feels alive. Mood rules
change the weights, not just the current face.
[Grobot Animations](https://projecthub.arduino.cc/tanmay_wankar/abbc33b9-9695-4064-935e-b2d713090a80)
(TFT_eSPI, spring physics, MIT) is the fallback if the custom renderer takes too long.

| Mood | Trigger (default rules, configurable) |
|---|---|
| neutral / curious | default, random looks around |
| happy | sunny weather, comfortable indoor climate (20–24 °C, 40–60 %) |
| sleepy | night (after sunset / configured hours) or low ambient light → dims screen |
| sad | rain/snow, or host link lost |
| hot / cold | indoor temp > 26 °C or < 18 °C |
| dry / "thirsty" | humidity < 30 % |
| alert / surprised | incoming `notify` |
| angry | CO₂ > 1500 ppm (if sensor) or host-defined trigger |

Priority: host override > alert > environmental rules > time of day > neutral.

### Pages (rotate every N seconds or by button/touch)
1. **Eyes** (default/idle) with a small status bar: time, outdoor temp, link icons
2. **Dashboard**: time/date, weather icon + temp, indoor temp/humidity, mini eyes
3. **Forecast**: today + 2 days
4. **Indoor**: temp/humidity graphs (last 24 h, kept in PSRAM ring buffer)
5. **Host**: whatever the host pushes as `data` (CPU, RAM, Proxmox VMs, calendar,
   unread mails…). Generic rendering of labelled values, so new data needs no firmware change.
6. **Custom pages** (AWTRIX-style "custom apps"): the host defines a page by name with a
   small set of widgets: `value`, `bar`, `icon+text`, `list`, `sparkline`. Example:
   `{"t":"page","name":"ci","title":"GitLab","items":[{"w":"icon+text","icon":"ok","text":"main ✔"}],"ttl":600}`.
   Pages join the rotation and disappear when their TTL runs out.

Status icons: WiFi, Internet, Serial host, TCP host, data stale.

---

## 5. Host service `buddyd` (laptop and Proxmox container)

**One codebase**, Python 3.11+ asyncio. It runs on the laptop (serial) and in a Proxmox LXC
or Docker container (TCP). Both transports can be enabled at the same time.

### Structure
```
host/buddyd/
  main.py            # asyncio entry, config loading
  config.yaml        # transports, providers, intervals, mood rules
  protocol.py        # message (de)serialization, shared with tests
  transports/
    serial_link.py   # pyserial-asyncio; auto-detect by USB VID/PID (Espressif 303a:1001)
    tcp_server.py    # asyncio server on :7531, mDNS advert via zeroconf
  providers/         # plugin interface: async def poll() -> dict
    time.py
    weather.py       # Open-Meteo
    system.py        # psutil: CPU, RAM, temperature, battery
    proxmox.py       # proxmoxer: node load, VMs/CTs running, storage
    calendar.py      # CalDAV / ICS: next meeting
    homeassistant.py # optional: any HA entity
  sinks/
    influx.py / mqtt.py  # optional: store/forward the device's sensor readings
```

### Behaviour
- On device `hello`: send `time`, then `weather` if the device lists them in `needs`, then all
  cached `data`.
- Providers poll on their own intervals and push changes only.
- Device sensor readings can be forwarded to MQTT/Home Assistant/InfluxDB.
- Small CLI: `buddyctl notify "Build done"`, `buddyctl mood happy 60`, `buddyctl wifi set …`
  (talks to the running daemon via a local Unix socket), so scripts, CI hooks and cron jobs
  can trigger the buddy.

### Deployment
- Laptop: `pipx install .` + systemd **user** service; udev rule for a stable
  `/dev/deskbuddy` symlink and permissions.
- Proxmox: Debian LXC with systemd service, or `docker compose` (Dockerfile in repo).
  Static IP or mDNS name so the device can find it.

---

## 6. Repository layout

```
esp32_desk_buddy/
  PLAN.md
  README.md
  protocol/          PROTOCOL.md, JSON schemas, example messages
  firmware/          PlatformIO project (platformio.ini, src/, include/, lib/, test/)
  host/              buddyd Python package, Dockerfile, systemd units, udev rule
  tools/             serial/TCP simulator, font/icon converters
  hardware/          wiring diagram, BOM, enclosure (STL/STEP)
  .github/workflows/ CI: pio build + unit tests, pytest for host
```

---

## 7. Milestones

| # | Milestone | Done when |
|---|---|---|
| **M0** | Project setup | Repo layout, PlatformIO builds for ESP32-S3, Python package skeleton, CI green |
| **M1** | Display + eyes | Eyes animate on the TFT (blink, look around, 3 moods) at ≥ 25 FPS |
| **M2** | Local sensor | SHT40 values in DataStore, shown on the Dashboard page |
| **M3** | Protocol + serial host | `PROTOCOL.md` v1; `buddyd` on the laptop sends time/weather/data over USB; device shows it |
| **M4** | Standalone WiFi | Captive portal provisioning, Open-Meteo + NTP direct from the device, NVS config |
| **M5** | TCP host | Device connects to `buddyd` in a Proxmox container (mDNS/static), reconnect logic |
| **M6** | ModeManager + priorities | Automatic switching/merging of sources, stale indicators, status icons |
| **M7** | Mood engine + pages | Rule-based moods, page rotation, night mode/auto-brightness, notifications |
| **M8** | Host providers + custom pages | system, Proxmox, calendar, GitLab CI, `buddyctl` CLI, `page` message |
| **M9** | MQTT | `MqttTransport`, Home Assistant discovery, device usable from HA/Node-RED without `buddyd` |
| **M10** | Polish | OTA updates, **browser flashing via ESP Web Tools** (GitHub Pages, like Stack-chan), enclosure, docs, sensor calibration |

M1–M3 are the core and can be built on the Cheap Yellow Display. After M3 the device is
useful with only the laptop attached.

---

## 8. Testing

- **Firmware:** PlatformIO native unit tests for DataStore, protocol parser, MoodEngine
  (no hardware needed); on-device smoke test page.
- **Host:** pytest for protocol and providers (mocked APIs); a **device simulator**
  (`tools/fake_device.py`) that speaks the protocol over a PTY/TCP, so `buddyd` can be
  developed without hardware.
- **Integration:** `buddyd` ⇄ real device over serial and TCP; pull USB/WiFi to test
  reconnects and stale handling.

---

## 9. Prior art (checked 2026-10-25)

No existing project combines a TFT with mood eyes, local sensors, and standalone **and**
host-fed operation (serial/TCP) in one data model. That combination sets this project apart.
Related projects and what we take from them:

| Project | What it is | What we take |
|---|---|---|
| [DeskBuddy (Edison Science Corner)](https://circuitdigest.com/news/the-tiny-desk-robot-packed-with-real-time-information) | ESP32-C3 + OLED, eyes, weather, time, touch | Confirms the concept; standalone only |
| [The Mochi](https://flavortown.hackclub.com/projects/21291), [Dasai Mochi clones](https://github.com/dzonder/mochi-esp32) | OLED eyes with many expressions; dzonder's version gets expressions from a server | Host-driven expressions → our `mood` message |
| [ESP8266 Desk Buddy](https://www.electronicwings.com/users/NilutpolKashyap/projects/6253/desk-buddy--multi-function-iot-desk-companion) | Clock, weather, Pomodoro, eyes | Pomodoro timer as a feature |
| [Stack-chan](https://github.com/stack-chan/stack-chan) | Mature open-source M5Stack robot face, plugin apps, web flasher | Browser flashing, face design ideas |
| [esp32-eyes](https://github.com/liunix61/esp32-eyes) | Cozmo-style parameter eyes, 18 emotions, weighted random behaviour | Basis for our eye renderer |
| [Grobot Animations](https://projecthub.arduino.cc/tanmay_wankar/abbc33b9-9695-4064-935e-b2d713090a80) | TFT eye animations with mood state machine | Fallback library |
| [esp32-hw-monitor](https://github.com/SinyaWeo/esp32-hw-monitor) | ST7789 + LVGL, PC stats over serial | Same stack as our serial mode |
| [FireBeetle P4 telemetry](https://www.hackster.io/pradeeplogu0/real-time-system-telemetry-dashboard-with-firebeetle-esp32p4-d54b97) | NDJSON over USB serial, Python + pyserial + psutil | Confirms our protocol and host stack |
| [AWTRIX 3](https://github.com/blueforcer/awtrix3) (now AWTRIX NG) | LED matrix clock, custom apps via MQTT/HTTP | Custom pages + MQTT input |
| [Tidbyt/Pixlet](https://github.com/tidbyt/community), [Tronbyt](https://github.com/tronbyt/apps), [Pixbyt](https://github.com/hacknug/pixbyt) | Server renders the apps, device only shows images | **Not** adopted: conflicts with standalone mode |
| [Proxmox forum ESP32 screen](https://forum.proxmox.com/posts/871287/) | CPU/RAM/temp via SNMP on an OLED | We use the Proxmox API through `buddyd` instead (richer) |
| [RNT ESP32 TFT LVGL weather station](https://randomnerdtutorials.com/esp32-tft-lvgl-weather-station) | ILI9341 + LVGL + Open-Meteo + ArduinoJson | Starting point for the weather page |

---

## 10. Risks / notes

- **Sensor self-heating:** the ESP32 and backlight warm the sensor. Put it on a short cable
  or behind a separated vent, and support an offset calibration.
- **Serial port sharing:** only one process can open the port, so stop `buddyd` before
  `pio device monitor`. Use `buddyd --monitor` to view device logs.
- **USB-CDC reset:** opening the port may toggle DTR/RTS and reset the board. Disable
  DTR/RTS in pyserial.
- **TCP security:** plain TCP on the LAN, protected by an optional token. Add TLS later if
  needed.
- **LVGL memory:** use PSRAM for draw buffers and keep the eyes on a canvas or direct draw for speed.

---

## 11. Open questions

1. Which display/board: DIY SPI TFT + DevKit, or an all-in-one board (T-Display-S3,
   Waveshare 2.8" touch)?
2. Touch, buttons, or both?
3. Which "other data" matters most: PC stats, Proxmox status, calendar, Home Assistant, mail,
   CI build status?
4. Should the device's own sensor data be forwarded (MQTT / Home Assistant / InfluxDB)?
5. Firmware framework: Arduino on PlatformIO (proposed, faster to build) or pure ESP-IDF?
