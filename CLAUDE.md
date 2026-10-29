# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Blinky is a desk companion: mood eyes, weather, indoor climate and data pages on a round
ESP32-S3 touch display (Waveshare LCD-1.46 `ws146` or AMOLED-1.75 `amoled175`). There are three parts:
- `firmware/`: ESP-IDF 6.1 + LVGL, C++20
- `host/`: one Go module with `buddy-link` (Buddy Link, the host service), `buddy-ctl` (CLI) and `buddy-sim` (device emulator)
- `site/`: the setup page on GitHub Pages; it flashes the firmware and edits settings over Web Serial

Plans and specs: `PLAN.md`, `firmware/PLAN.md`, `host/PLAN.md`, `protocol/PROTOCOL.md`, `hardware/PLAN.md`.

## Commands

Host (Go ≥ 1.27, no cgo), run in `host/`:
```sh
go build -o bin/ ./cmd/...              # buddy-link, buddy-ctl, buddy-sim
go vet ./... && go test -race -count=1 ./...
go test ./internal/sim -run TestNVSSecretsAndSet   # single test
go mod tidy -diff                        # CI checks this
```
Try everything without hardware: `bin/buddy-link --tcp 127.0.0.1:7531`, then
`bin/buddy-sim device --host 127.0.0.1:7531 --open` (UI on http://127.0.0.1:7600).

Firmware desktop build (portable parts, LVGL in an SDL window, doctest unit tests; needs SDL2 and Ninja), run from the repo root:
```sh
cmake -S firmware/sim -B build/fw-sim -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/fw-sim
ctest --test-dir build/fw-sim --output-on-failure
build/fw-sim/test/firmware_tests -tc="SourceRouter*"     # single doctest case
UPDATE_SCREENSHOTS=1 ctest --test-dir build/fw-sim       # regenerate firmware/test/screenshots/ after an intended UI change
build/fw-sim/buddy_sim        # UI in a window;  build/fw-sim/buddy_shot <dir>  writes PNGs
```
The screenshot PNGs are test references and also appear in the README. A failing comparison writes `*.actual.png`.

ESP32 firmware (CI uses the `espressif/idf:v6.1` container), run in `firmware/`:
```sh
idf.py -B build-ws146 -D BOARD=ws146 set-target esp32s3
idf.py -B build-ws146 -D BOARD=ws146 build
```
`main/app_main.cpp` and the `board`, `net`, `sensors`, `audio`, `serial_link` and `tcp_link` components are built only by ESP-IDF, not by the desktop build. Changes there are compiled only by `idf.py` or the firmware CI.

## Architecture

**The device owns all configuration; `buddy-link` is stateless.** At each connection, the device sends its subscriptions (`sub` with sid, src, interval and params such as lat/lon or URLs and tokens) over NDJSON (`protocol/PROTOCOL.md`). The host fetches and sends back `time`, `weather` and `data` messages. Any host can therefore serve any device.

**Source routing** decides, for each configured source, who fetches it:
- the device itself, if its WiFi has internet and the source is device-native (`device_native()` in `app_core/src/sources.cpp`)
- otherwise the first host link that offers it (USB before TCP)

`via: device|host` forces one side. Each source is fetched in exactly one place and moves when WiFi or a host comes or goes. Results land in the **DataStore** (key/value with origin priority and TTL), and the UI and MoodEngine read from it. Data keys are `<sid>.<key>`; exceptions are `weather.*`, `indoor.*` and `user.*`.

**Firmware layering:** `components/app_core` (pure C++ plus ArduinoJson: settings registry, DataStore, protocol, HostLink, SourceRouter, DeviceFetcher, time zones), `mood` and `ui` (C++ plus LVGL) are portable. They're unit-tested on the desktop through `firmware/sim` and `firmware/test`. Hardware and network access go through hooks (HTTP get, NTP, send, set_time), which the tests replace with fakes. In `main/app_main.cpp`, the host links, router and UI run in the LVGL task; the fetcher runs in the network task.

**Settings:** one registry in `app_core/src/settings.cpp` (key, type, default, range, secret) drives every editor:
- the device's own page at `blinky.local` builds its form from the schema
- `config` get/set messages from `buddy-ctl` and the setup page

Secrets come back masked as `***`, and sending `***` back keeps the stored value.

**Everything is implemented twice: keep both sides in sync.** `host/internal/sim` (the Go emulator) mirrors the firmware logic: NVS defaults in `nvs.go`, SourceRouter and fetching in `device.go`, the DataStore, moods and pages. Firmware headers say which Go file they mirror. A behavior change in the firmware (settings, routing, device-native sources, data keys) usually needs the same change in the emulator. It may also need a change in `host/internal/source` (the host's sources), in `site/index.html` (setup page fields) and in the docs or protocol. `protocol/examples/*.ndjson` are golden lines that tests on both sides check (`firmware/test/test_host_link.cpp`, `host/internal/protocol/protocol_test.go`).

## Conventions

- Comments and docs use short, plain English sentences. Each file starts with a header comment saying what it is and how it fits in.
- User-visible changes get an entry in `CHANGELOG.md`, under an "Unreleased" section until a release.
