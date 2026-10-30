# Blinky firmware

ESP-IDF 6.1 firmware for two round Waveshare boards, chosen at build time:

| Board | `BOARD` | Screen | Audio | Battery level |
|---|---|---|---|---|
| ESP32-S3-Touch-LCD-1.46 | `ws146` (default) | 412×412 LCD | PCM5101 speaker, I²S mic | – |
| ESP32-S3-Touch-AMOLED-1.75 | `amoled175` | 466×466 AMOLED | ES8311 speaker, ES7210 mics | AXP2101 |

Both screens have about the same pixel density, so the UI keeps its sizes and stays centred on
the larger one. Plan and decisions: [PLAN.md](PLAN.md).

**Status: M0–M7 built (host link over USB and TCP, WiFi, fetching on the device, all pages,
notifications, gestures, updates over the link) and tested on the desktop. Runs on the
AMOLED-1.75: display, touch, WiFi with weather from the device, Buddy Link over USB, speaker,
microphones and dimming checked; the LCD-1.46 hasn't run yet** (to-do list: [PLAN.md](PLAN.md#to-do)).

- **Screens** (swipe left/right, or a twist with the IMU; swipe up = home): eyes with 16 moods
  (blink, glances, idle routines), dashboard (clock, weather, indoor climate), analog clock,
  forecast, indoor trend, host data, countdown, alarm, custom pages from hosts, and an info
  screen with a touch tester. `ui.pages` picks and orders them; `ui.rotation` turns the picture in 90° steps
  (from 0° it restarts); `ui.fps` shows the frame rate and CPU load.
- **Notifications** from hosts over every page (icon, text, mood, chime); the status bar shows
  a running countdown and an alarm that is on.
- **Gestures:** flick (next/previous page) and shake (dizzy eyes) from the QMI8658 IMU,
  thresholds still to be tuned on the board. The countdown and alarms run on taps.
- **USB only:** `wifi.enabled = false` leaves WiFi out; Buddy Link on USB fetches everything.
- **Moods:** the eyes react to the room and the weather (hot, cold, dry air, too loud, rain,
  snow, night, sunny), to a ringing alarm, to a tap (happy) and to sounds (startled by a
  bang, curious when someone talks). A host can set a mood for a while.
- **Backlight** for day and night, dimmed after a minute without a touch.
- **Sound:** the alarm and the countdown beep on the speaker; the microphone measures only
  the level (no audio is kept or sent).
- **Data and settings:** a data store with priorities and expiry; all settings in one
  registry, saved in flash (NVS).
- **Sensors:** SHT40 temperature and humidity, PCF85063 RTC (restores the time at boot).
- **Host links over USB and TCP:** talk to `buddy-link` as in [the protocol](../protocol/PROTOCOL.md):
  subscriptions for the configured sources, results for time, weather and data; events
  (touch, gesture, page, countdown, alarm) and readings once a minute back; the settings
  list for buddy-link's web UI; firmware updates (`ota` messages) into the other app
  partition, rolled back if the new firmware doesn't start. TCP goes to
  `link.tcp_host` (`host:port`, or `auto` to find `buddy-link` via mDNS) and reconnects by itself.
  The [setup page](https://stefanriese.github.io/blinky-desk-companion/) reads and writes settings
  over USB, and after flashing it can hand over the WiFi credentials (Improv Serial).
- **WiFi:** the device fetches time (NTP), weather and air quality (Open-Meteo) and `http`
  sources itself when its WiFi has internet; otherwise a host does. The info screen shows who
  fetches what. Without a stored network (or when it isn't reachable at boot) the device opens
  the access point **Blinky-Setup** for 3 minutes; joining it opens the settings page.
- **Web page:** `http://blinky.local/` (or the device's IP) shows the links and sources and
  edits all settings.

Screenshots of every screen: [`test/screenshots/`](test/screenshots/).

## Layout

```
main/                 app_main.cpp: settings → board → clock, sensors → host link → UI
components/
  app_core/           portable core (C++ and ArduinoJson only): data store, settings,
                      protocol, host link, source router, device fetcher, Improv,
                      countdown, alarms, units, time zones, page list (ui.pages),
                      custom pages, OTA receiver (SHA-256), motion gestures
  mood/               portable MoodEngine (C++ and app_core only)
  ui/                 portable UI (C++ and LVGL only): theme, round-screen geometry, eyes,
                      dashboard, clock, forecast, indoor and host data pages, countdown and
                      alarm pages, custom pages, notification, info screen, status bar,
                      app navigation, fonts, icons
  board/              one API (board.h), one file per board (Kconfig "Blinky board"): display,
                      touch, brightness, I²C, speaker and mic, battery; esp_lvgl_port (C, ESP-IDF only)
  sensors/            SHT40 climate task, PCF85063 RTC, QMI8658 motion task (ESP-IDF only)
  audio/              speaker (beeps) and microphone level through the board (ESP-IDF only)
  serial_link/        USB-Serial-JTAG transport for the host link, Improv Serial bytes (ESP-IDF only)
  tcp_link/           TCP transport for the host link: host:port or mDNS, reconnect (ESP-IDF only)
  net/                WiFi station + setup AP, captive portal, web page, mDNS, NTP, HTTPS (ESP-IDF only)
sim/                  desktop builds of the portable parts: simulator, screenshots, tests
test/                 host unit tests (doctest) and the screenshot references
tools/                fonts.sh: generates the fonts in components/ui/fonts/ (lv_font_conv)
```

## Without hardware: simulator, screenshots, tests

Needs CMake, a C++20 compiler and, for the window, SDL2 (`apt install libsdl2-dev`).
LVGL, ArduinoJson and doctest are fetched by CMake.

```sh
cmake -S firmware/sim -B build/fw-sim -G Ninja
cmake --build build/fw-sim
build/fw-sim/buddy_sim                  # the UI in a window; the mouse is the finger
build/fw-sim/buddy_shot shots/          # renders the screens to PNG without a window
ctest --test-dir build/fw-sim --output-on-failure
UPDATE_SCREENSHOTS=1 ctest --test-dir build/fw-sim   # after an intended UI change
```

## Device

Needs [ESP-IDF 6.1](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/get-started/)
(or the `espressif/idf:v6.1` Docker image). The component manager fetches LVGL, ArduinoJson and
the board drivers on the first build (versions pinned in `dependencies.lock`).

```sh
cd firmware
idf.py set-target esp32s3
idf.py build flash monitor              # ws146; USB-C, the board shows up as a USB serial/JTAG device

# AMOLED-1.75: its own build folder (each folder keeps its board's sdkconfig)
idf.py -B build-amoled175 -D BOARD=amoled175 set-target esp32s3
idf.py -B build-amoled175 -D BOARD=amoled175 build flash monitor
```

After flashing, close the monitor and start `buddy-link`: it detects the board on USB (VID
`303a`), and the dashboard should fill with time and weather (not yet tried on a real board).

`idf.py merge-bin -o blinky-firmware-ws146.bin` (with `-B build-amoled175` for the other
board) makes a single image to flash at offset 0. CI builds both on every change and keeps
them as downloads.

**Without ESP-IDF:** each release has `blinky-firmware-ws146.bin` and
`blinky-firmware-amoled175.bin`; the
[setup page](https://stefanriese.github.io/blinky-desk-companion/) asks for the board and
flashes the newest one from the browser (Chrome or Edge). By hand:
`esptool.py write_flash 0 blinky-firmware-<board>.bin`.
