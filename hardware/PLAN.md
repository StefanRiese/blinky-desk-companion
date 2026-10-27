# Hardware Plan

Part of the [main plan](../PLAN.md).

## Phase 1 – Prototype (M0–M3)
**ESP32-2432S028 "Cheap Yellow Display" (CYD)**: ESP32-WROOM, 2.8" ILI9341 320×240,
XPT2046 resistive touch, RGB LED, LDR, micro-SD, CH340 USB-UART.
- SHT40 breakout on the CYD's extended I²C header (CN1: GPIO 22/27), on a ~10 cm cable
  away from the board.
- Limits: no PSRAM, UART bridge (DTR/RTS reset on port open), few free GPIOs.

## Phase 2 – Target (M4+)
| Part | Choice | Notes |
|---|---|---|
| MCU | ESP32-S3-DevKitC-1 **N16R8** | native USB-CDC, 8 MB PSRAM, 16 MB flash (OTA ×2) |
| Display | 2.8" ILI9341 or 2.4" ST7789 SPI, 320×240, with FT6236 capacitive touch | IPS preferred (viewing angle) |
| Temp/Hum | Sensirion **SHT40** (I²C 0x44) | ±0.2 °C |
| Light | BH1750 (I²C 0x23) | optional: auto-brightness, sleepy mood |
| CO₂ | SCD40 (I²C 0x62) | optional, ~25 €, most useful indoor value |
| Input | 1 tactile button (+ touch) | short = next page, long = Pomodoro |
| Sound | passive piezo on a PWM pin | notify beep |
| Power | USB-C 5 V | ~150–250 mA with backlight |

All-in-one alternatives: Waveshare ESP32-S3-Touch-LCD-2.8, LilyGO T-Display-S3 (smaller,
170×320). These need a new `board_*.h` in the firmware.

## Candidate all-in-one: Waveshare ESP32-S3-Touch-LCD-1.46 (round, 412×412)
Source: [product page](https://www.waveshare.com/esp32-s3-touch-lcd-1.46b.htm?sku=29565),
[wiki](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.46). Variants: 1.46B (no cover
glass), 1.46 (cover glass), 1.46C (widened cover glass), same electronics. ~25 $.

| Plan requirement | Board | Coverage |
|---|---|---|
| ESP32-S3 + PSRAM + flash | ESP32-S3R8, 8 MB PSRAM, 16 MB flash | ✅ |
| WiFi standalone / TCP | 2.4 GHz WiFi + BLE 5 | ✅ |
| Serial to laptop | USB-C, native USB-CDC | ✅ |
| Display for eyes | 1.46" IPS **round** 412×412, QSPI, SPD2010 | ✅ great for eyes; dashboards need a round layout |
| Touch / buttons | SPD2010 capacitive touch + BOOT + PWR buttons | ✅ |
| Sound for notify | PCM5101 I²S DAC + amp + speaker | ✅ better than a piezo |
| Clock without network | PCF85063 RTC + backup battery header | ✅ keeps time in offline mode |
| Temp / humidity | — | ❌ **add SHT40** on the header's shared I²C (SCL GPIO10 / SDA GPIO11) |
| Ambient light (auto-dim) | — | ❌ optional BH1750 on the same I²C |
| CO₂ | — | ❌ optional SCD40 on the same I²C |
| Enclosure | cover glass included | ⚠️ still needs a stand/case with a sensor chamber |

**Extras not in the original plan:**
- **IMU (QMI8658, accel + gyro):** tap/double-tap = next page / Pomodoro, shake → "dizzy"
  eyes, pick-up → wake + "surprised", eyes tilt with the device, orientation detection.
- **Microphone:** noise level → mood (loud = annoyed, quiet at night = sleepy), clap to wake,
  later maybe wake word / voice assistant (out of scope for v1).
- **Battery (Li-Po, MX1.25) with charger:** portable use; battery level as data key + icon.
- **TF card:** sound files, sensor history, custom eye skins.
- **TCA9554 IO expander**, PWM backlight (GPIO5).

**Caveats / to verify early (spike in M1):**
- **Display driver:** SPD2010 over QSPI isn't a classic SPI TFT. Expect to use ESP-IDF's
  `esp_lcd` SPD2010 component (as in Waveshare's examples) with LVGL, **not LovyanGFX**.
  Decided: ESP-IDF 6.1 with `esp_lcd_spd2010` + `esp_lvgl_port` (proven on this board by
  xiaozhi-esp32). FPS for 412×412 is measured in the firmware's M1 spike.
- **Round UI:** usable square inside the circle is ~290×290. All pages must be designed for
  the circle (eyes, a ring gauge for the Pomodoro, arc gauges, rotating single values).
- **Self-heating:** board, backlight and charging warm a small case, so the SHT40 must sit
  outside/below on a cable.
- I²C addresses on the shared bus: check for collisions before adding sensors
  (SHT40 0x44, BH1750 0x23, SCD40 0x62).

**Verdict:** covers about 80 % of the hardware plan, plus IMU, audio, RTC and battery for
free. Only temp/humidity is missing (one SHT40 breakout). Recommended as the **main target
board**. The CYD stays an option for a cheap rectangular-screen variant.

## Second board: Waveshare ESP32-S3-Touch-AMOLED-1.43 (round, 466×466)
Planned as a second supported board (to-do list in the [firmware plan](../firmware/PLAN.md#second-board-waveshare-esp32-s3-touch-amoled-143-waiting-for-the-board)).
Sources: Waveshare's wiki (its pin table is partly garbled) and FlightDot
(github.com/mateusfrmacedo/FlightDot), which follows Waveshare's official demo. Not checked
against the schematic yet.

| Part | AMOLED-1.43 | vs. ws146 |
|---|---|---|
| MCU, memory | ESP32-S3R8, 8 MB PSRAM, 16 MB flash, native USB | same |
| Display | 1.43" AMOLED 466×466, QSPI; controller **SH8601** (panel ID 0x86) **or CO5300** (6 px column offset), depending on the batch | SPD2010 LCD 412×412 |
| Display pins | CS 9, CLK 10, D0–D3 11–14, RST 21, AMOLED_EN 42 | QSPI 40/46/45/42/41, CS 21 |
| Brightness | display command `0x51` (no backlight) | PWM on GPIO 5 |
| Touch | FT3168 at I²C 0x38; INT/RST pins not documented | SPD2010 touch |
| I²C | SDA 47, SCL 48 (touch, QMI8658 IMU, PCF85063 RTC) | SDA 11, SCL 10 |
| IO expander, power latch | none | TCA9554, latch on GPIO 7 |
| Audio | **none** | PCM5101 speaker, I²S mic |
| Other | TF card (SPI CS 38, MOSI 39, MISO 40, SCLK 41), BAT_ADC on GPIO 4, IMU INT 8, RTC INT 15, BOOT GPIO 0 | – |

Open questions (check on the board or the schematic):
- touch INT and RST pins (the demos poll the FT3168)
- which controller our unit has (log the panel ID)
- whether it stays on from battery without a power latch
- AMOLED burn-in: the eyes and the status bar are static; see the firmware to-do list

## Example pin map (S3 DevKitC + SPI TFT)
| Signal | GPIO |
|---|---|
| TFT SCK / MOSI / MISO | 12 / 11 / 13 |
| TFT CS / DC / RST / BL(PWM) | 10 / 9 / 8 / 14 |
| Touch I²C SDA / SCL / INT | shared I²C / 7 |
| I²C SDA / SCL (sensors) | 1 / 2 |
| Button | 0 (BOOT) or 4 |
| Piezo | 5 |

Final pins are set in `firmware/include/board/board_s3.h`.

## Thermal design
- The ESP32 and backlight heat the case by 1–4 °C. Put the sensor in a **separate,
  ventilated chamber** at the bottom/back, with a thermal break (slot) in the PCB/case.
- Firmware supports an offset. Calibrate against a reference thermometer after 1 h warm-up.

## Enclosure
- 3D-printed, "robot head" shape with a slight tilt (~15°) toward the user.
- Inspiration: Stack-chan cases, MakerWorld "Companion" enclosure.
- Files in `hardware/enclosure/` (FreeCAD/STEP + STL).

## BOM (target, approx.)
| Part | € |
|---|---|
| ESP32-S3 N16R8 DevKit | 10 |
| 2.8" IPS SPI + cap. touch | 15 |
| SHT40 breakout | 5 |
| BH1750 | 2 |
| Button, piezo, wires | 3 |
| Filament | 3 |
| **Total** (without SCD40) | **~38** |

## Tasks
- [ ] Order CYD + SHT40 (prototype)
- [ ] Wiring diagram (`hardware/wiring.svg`)
- [ ] Order S3 parts once M3 works
- [ ] Enclosure v1, thermal test, v2
- [ ] Optional: small carrier PCB (KiCad) for S3 + sensors + button
