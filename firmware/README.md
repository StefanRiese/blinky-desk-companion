# Desk Buddy firmware

ESP-IDF 6.1 firmware for the Waveshare ESP32-S3-Touch-LCD-1.46 (round 412×412). Plan and
decisions: [PLAN.md](PLAN.md).

**Status: M0.** The board starts, the display and touch work through LVGL 9.6, and a
bring-up screen shows a rim ring, two eyes, the firmware version and a touch tester (a dot
follows your finger, taps are counted).

## Layout

```
main/                 app_main.cpp: board → UI
components/
  board_ws146/        power latch, I²C, TCA9554 resets, SPD2010 display (QSPI) and touch,
                      backlight, esp_lvgl_port (C, ESP-IDF only)
  ui/                 portable UI (C++ and LVGL only): theme, round-screen geometry, screens
sim/                  desktop builds of the portable parts: simulator, screenshots, tests
test/                 host unit tests (doctest)
```

## Without hardware: simulator, screenshots, tests

Needs CMake, a C++20 compiler and, for the window, SDL2 (`apt install libsdl2-dev`).
LVGL and doctest are fetched by CMake.

```sh
cmake -S firmware/sim -B build/fw-sim -G Ninja
cmake --build build/fw-sim
build/fw-sim/buddy_sim                  # the UI in a window; the mouse is the finger
build/fw-sim/buddy_shot shots/          # renders the screens to PNG without a window
ctest --test-dir build/fw-sim --output-on-failure
```

## Device

Needs [ESP-IDF 6.1](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/get-started/)
(or the `espressif/idf:v6.1` Docker image). The component manager fetches LVGL and the
board drivers on the first build.

```sh
cd firmware
idf.py set-target esp32s3
idf.py build flash monitor              # USB-C; the board shows up as a USB serial/JTAG device
```

`idf.py merge-bin -o deskbuddy-ws146.bin` makes a single image to flash at offset 0 (what
the setup page will use). CI builds it on every change and keeps it as a download.
