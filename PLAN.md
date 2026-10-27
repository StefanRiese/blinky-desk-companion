# Blinky – Project Plan

A small desk device with a display that shows animated "mood eyes", weather, indoor
temperature/humidity and other data.

**Sub-plans:** [firmware](firmware/PLAN.md) · [host service `buddyd`](host/PLAN.md) ·
[protocol](protocol/PROTOCOL.md) · [hardware](hardware/PLAN.md)

It works in three ways:

| Mode | Link | Who gets the data | Needs WiFi on the ESP32 |
|---|---|---|---|
| **Standalone** | WiFi → Internet | ESP32 fetches it (weather API, NTP) | yes (with internet) |
| **Hosted – Serial** | USB serial → laptop | `buddyd` on the laptop fetches what the device asks for | no |
| **Hosted – TCP** | WiFi/LAN → Proxmox container | `buddyd` in the container fetches what the device asks for | yes (LAN only, no internet needed) |

**All configuration lives on the ESP32.** `buddyd` is a stateless internet gateway: the device
tells it which sources to fetch (with locations, URLs, tokens) each time it connects. So
the host can change at any time (another laptop, the container) without any setup.

Modes can run together: e.g. weather fetched by the device over WiFi plus GitLab/calendar
fetched through a host. If the ESP32 has no WiFi, the host fetches everything, including
time and weather.

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
- **Source priority per key**: local sensor > fetched source (by device or host) > pushed
  by scripts (configurable). If a
  value is past its TTL it is shown as stale (greyed out / "--").
- **Device-owned configuration**: all settings (sources, tokens, pages, moods) are in NVS
  and edited on the device: its web UI, a Web Serial setup page, the touch quick menu or
  `config` messages. Hosts keep nothing.
- **SourceRouter**: for each configured source, the device either fetches it itself
  (when WiFi has internet and the source is device-native) or subscribes to it on a
  connected host. Results land in the same keys either way.
- **One protocol, several transports**: serial, TCP and (optionally) MQTT carry the same
  messages. Only the byte pipe differs.
- **ModeManager** decides what is active:
  1. WiFi configured and internet reachable → start StandaloneFetcher.
  2. Serial host detected (`hello` received) → HostLink on serial.
  3. TCP host configured / found via mDNS → HostLink on TCP.
  4. MQTT broker configured → subscribe to the buddy's topics (works alongside 1–3).
  5. Nothing available → offline mode: local sensor + eyes only; time from the RTC once set.
  The SourceRouter then assigns each source to the device or to one connected host, and
  moves it when a link or the internet comes or goes. Data pushed by hosts, MQTT or
  scripts always gets merged in.

---

## 3. Communication protocol (Device ⇄ Host)

**Format:** newline-delimited JSON (NDJSON), one message per line, UTF-8, max 2 KB per
line. Easy to debug with `screen`/`nc`, and ArduinoJson parses it on the ESP32. (CBOR+COBS
can be added later if bandwidth matters. It won't for this project.)

**Serial specifics:** USB-CDC at 115200 baud (the baud rate is effectively ignored with
native USB). Debug logs are wrapped as `{"t":"log",...}` so they never break the stream.
Lines that don't start with `{` are ignored by the host.

**TCP specifics:** the device is a **TCP client** connecting to `buddyd` (default port
`7531`). Host from config or discovered via mDNS `_blinky._tcp`. Reconnects with
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
| `hello` | firmware version, device id, capabilities |
| `sub` / `unsub` | subscribe to a host source: `{"sid":"ci","src":"gitlab","every":60,"p":{…}}` |
| `sensor` | `{"temp":22.4,"hum":41.2,"lux":120}` every 30 s |
| `event` | button press, touch, page change |
| `ack` / `err` | reply to host commands with `id` |
| `config` | reply to `config get` (secrets masked) |
| `log` | debug output |

### Host → Device
| type | purpose |
|---|---|
| `hello` | host name, protocol version, offered sources `srcs`, internet state `net` |
| `host_state` | internet state changed |
| `sub_state` | subscription ok / error / backoff |
| `time` | epoch (the device applies its own time zone) |
| `weather` | current + 3-day forecast (temp, code, wind, rain probability, sunrise/sunset) |
| `data` | generic key/values: `{"host.cpu":23,"pve.vms_running":7,"cal.next":"Standup 09:30"}` |
| `mood` | override mood for N seconds (`{"mood":"happy","ttl":30}`) |
| `notify` | toast message + optional sound/eye reaction |
| `page` | define/update/remove a custom page (see 4. Pages) |
| `config` | `set` / `get` device settings (forwarded from `buddyctl`; stored only in NVS) |
| `ota` | (later) trigger OTA from URL |
| `ping` | keepalive (every 10 s; link considered lost after 30 s) |

Version field `v` allows evolution; unknown types/keys are ignored on both sides.
A shared spec file `protocol/PROTOCOL.md` + JSON schemas keeps firmware and service in sync.

---

## 4. Firmware

**Stack:** ESP-IDF 6.1 (CMake, C++20), FreeRTOS tasks, LVGL 9.6 via `esp_lvgl_port` for the
whole GUI including the eyes. Espressif components for the ws146 board (SPD2010 display and
touch over QSPI/I²C, TCA9554), ArduinoJson 7 (header-only), mDNS, esp-mqtt, OTA. The UI code
is portable, so it also runs in a desktop simulator and, as WebAssembly, in the browser
emulator. Details and reasons: [firmware plan → Toolchain and GUI](firmware/PLAN.md#toolchain-decided-2026-10-26).

### Modules
| Module | Responsibility |
|---|---|
| `board/` | pin maps per hardware variant |
| `core/DataStore` | thread-safe key/value store with TTL, source priority, change callbacks |
| `core/ConfigStore` | NVS-backed settings (WiFi, location, TZ, sources + tokens, links, pages, moods); encrypted secrets |
| `net/WifiManager` | connect, captive portal fallback (`Blinky-Setup` AP), reconnect |
| `net/WebConfig` | settings web UI on the device + JSON API, export/import |
| `net/StandaloneFetcher` | device-native sources: NTP, Open-Meteo (no API key), `http` |
| `sources/SourceRouter` | per source: fetch on the device or `sub` on a host; failover |
| `link/Transport` | abstract `read/write/connected`; `SerialTransport`, `TcpTransport`, `MqttTransport` (esp-mqtt) |
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
bitmaps), as a custom LVGL widget on the round display. When idle, the buddy switches between
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

## 5. Host service `buddyd` (Windows, macOS, Linux, container)

**A stateless internet gateway, written in Go** and shipped as a single static binary per
OS/arch. Details and rationale are in [host/PLAN.md](host/PLAN.md).

- **The device owns all configuration.** After `hello`, the device sends one `sub` per
  source it wants from this host (weather for its location, GitLab projects + token, a
  calendar URL, …). The host fetches on the requested interval and sends the results back.
  Nothing is stored on the host. If you plug the buddy into a different laptop, it works
  immediately.
- **Sources:** `time`, `weather`, `aqi`, generic `http` (URL + JSON Pointer picks, so new
  data is a device-config change), `gitlab`, `calendar`, `proxmox`, `ha`, `sys` (the
  host's own CPU/RAM/battery).
- **Transports:** USB serial (auto-detect by VID/PID) and TCP `:7531` with mDNS, both at
  once. The same binary runs on a laptop and in the Proxmox container.
- **Network:** honours `HTTPS_PROXY` and the Windows/macOS system proxy, plus an optional
  corporate CA. Reports its internet state (`host_state`) so the device can switch to
  fetching over its own WiFi.
- **Host config is optional** and only covers the host itself (ports, TCP token, proxy, log
  level, denied sources).
- **For scripts:** local HTTP control API + `buddyctl notify "Build done"`,
  `buddyctl mood happy 60`, `buddyctl data user.x=1`.
- **Desktop:** tray icon (link + internet state, "Configure device…"), autostart as the
  user via `buddyd install --user`. Install via Homebrew / Scoop / .deb or archive.
- **Proxmox:** Debian LXC with `buddyd install`, or Docker Compose (builds the image from
  the repository) with `BUDDYD_TCP_ENABLED=true`.

---

## 6. Repository layout

```
blinky-desk-companion/
  PLAN.md
  README.md
  protocol/          PROTOCOL.md, JSON schemas, example messages
  firmware/          ESP-IDF project (main/, components/, sim/, tools/, test/)
  host/              buddyd Go module (daemon, buddyctl, buddysim), Dockerfile, GoReleaser config
  tools/             font/icon converters
  hardware/          wiring diagram, BOM, enclosure (STL/STEP)
  .github/workflows/ CI: pio build + unit tests; go test on Linux/macOS/Windows, releases
```

---

## 7. Milestones

| # | Milestone | Done when |
|---|---|---|
| **M0** | Project setup | Repo layout, ESP-IDF build for the ws146 board in CI, desktop simulator, Go module, CI green |
| **M1** | Display + eyes | Eyes animate on the TFT (blink, look around, 3 moods) at ≥ 25 FPS |
| **M2** | Local sensor | SHT40 values in DataStore, shown on the Dashboard page |
| **M3** | Protocol + serial host | `PROTOCOL.md` v1; device subscribes time/weather on `buddyd` over USB; device shows it |
| **M4** | Standalone WiFi + config | Captive portal, device web config UI, Open-Meteo + NTP direct from the device, SourceRouter |
| **M5** | TCP host | Device connects to `buddyd` in a Proxmox container (mDNS/static), reconnect logic, subscriptions move between hosts |
| **M6** | ModeManager + priorities | Automatic switching/merging of sources, stale indicators, status icons |
| **M7** | Mood engine + pages | Rule-based moods, page rotation, night mode/auto-brightness, notifications |
| **M8** | Host providers + custom pages | `http`, Proxmox, calendar, GitLab CI sources; `page` message; encrypted secrets on the device |
| **M9** | MQTT | `MqttTransport`, Home Assistant discovery, device usable from HA/Node-RED without `buddyd` |
| **M10** | Polish | OTA updates, **browser flashing via ESP Web Tools** (GitHub Pages, like Stack-chan), enclosure, docs, sensor calibration |

M1–M3 are the core and can be built on the Cheap Yellow Display. After M3 the device is
useful with only the laptop attached.

---

## 8. Testing

- **Firmware:** host unit tests (CMake + doctest) for DataStore, protocol parser, MoodEngine
  (no hardware needed); on-device smoke test page.
- **Host:** `go test -race` on Linux, macOS and Windows for protocol, subscription
  scheduler and sources (recorded HTTP responses); a **device simulator** (`buddysim
  device`) that sends hello + subs over TCP or a PTY, so `buddyd` can be developed without hardware.
- **Integration:** `buddyd` ⇄ real device over serial (on all three OSes) and TCP; swap
  hosts, pull USB/WiFi, cut the host's internet to test failover between device and host
  fetching and stale handling.

---

## 9. Prior art (checked 2026-10-25)

No existing project combines a TFT with mood eyes, local sensors, and standalone **and**
host-fed operation (serial/TCP) in one data model. That combination sets this project apart.
Related projects and what we take from them:

| Project | What it is | What we take |
|---|---|---|
| [DeskBuddy (Edison Science Corner)](https://circuitdigest.com/news/the-tiny-desk-robot-packed-with-real-time-information) | ESP32-C3 + OLED, eyes, weather, time, touch | Confirms the concept; standalone only |
| [The Mochi](https://flavortown.hackclub.com/projects/21291), [Dasai Mochi clones](https://github.com/dzonder/mochi-esp32) | OLED eyes with many expressions; dzonder's version gets expressions from a server | Host-driven expressions → our `mood` message |
| [ESP8266 Blinky](https://www.electronicwings.com/users/NilutpolKashyap/projects/6253/desk-buddy--multi-function-iot-desk-companion) | Clock, weather, Pomodoro, eyes | Pomodoro timer as a feature |
| [Stack-chan](https://github.com/stack-chan/stack-chan) | Mature open-source M5Stack robot face, plugin apps, web flasher | Browser flashing, face design ideas |
| [esp32-eyes](https://github.com/liunix61/esp32-eyes) | Cozmo-style parameter eyes, 18 emotions, weighted random behaviour | Basis for our eye renderer |
| [Grobot Animations](https://projecthub.arduino.cc/tanmay_wankar/abbc33b9-9695-4064-935e-b2d713090a80) | TFT eye animations with mood state machine | Fallback library |
| [esp32-hw-monitor](https://github.com/SinyaWeo/esp32-hw-monitor) | ST7789 + LVGL, PC stats over serial | Same stack as our serial mode |
| [FireBeetle P4 telemetry](https://www.hackster.io/pradeeplogu0/real-time-system-telemetry-dashboard-with-firebeetle-esp32p4-d54b97) | NDJSON over USB serial, Python + pyserial + psutil | Confirms our protocol (our host uses Go for cross-platform binaries) |
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
  DTR/RTS when `buddyd` opens the port.
- **TCP security:** plain TCP on the LAN, protected by an optional token. Because the device
  sends API tokens inside `sub` params, use read-only tokens, prefer serial on untrusted
  networks, and add TLS (device → `buddyd`) once host-only sources with secrets are used over TCP.
- **Secrets on the device:** tokens live in encrypted NVS and are masked in every editor and
  export. A lost device means rotating those tokens.
- **Web Serial vs. `buddyd`:** only one program can open the port. The tray's "Configure
  device…" releases it while the setup page is open.
- **LVGL memory:** use PSRAM for draw buffers and keep the eyes on a canvas or direct draw for speed.

---

## 11. Open questions

1. Which display/board: DIY SPI TFT + DevKit, or an all-in-one board (T-Display-S3,
   Waveshare 2.8" touch)?
2. Touch, buttons, or both?
3. Which "other data" matters most: PC stats, Proxmox status, calendar, Home Assistant, mail,
   CI build status?
4. Should the device's own sensor data be forwarded (MQTT / Home Assistant / InfluxDB)?
5. ~~Firmware framework~~: decided 2026-10-26, ESP-IDF 6.1 + LVGL 9.6 (see the firmware plan).
