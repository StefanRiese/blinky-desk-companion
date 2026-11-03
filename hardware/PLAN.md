# Hardware Plan

Part of the [main plan](../PLAN.md). The boards and what Blinky adds to them: sensors, case
and parts list. A pin-by-pin comparison of the two boards is in
[hardware-comparison.md](hardware-comparison.md).

## Boards

Blinky runs on two all-in-one round Waveshare ESP32-S3 boards (decided). Each has the S3 with
8 MB PSRAM and 16 MB flash, native USB, a round touch display, an IMU, audio, an RTC and a
battery charger. Only the temperature and humidity sensor is added. The board is chosen at
build time (`idf.py -D BOARD=…`, one source file per board in `firmware/components/board/`).

| | AMOLED-1.75 (`amoled175`) | LCD-1.46 (`ws146`) |
|---|---|---|
| Status | runs on a real board (since 2026-10-29) | built in CI, not run on a real board yet |
| Display | 1.75" AMOLED 466×466, CO5300 | 1.46" IPS LCD 412×412, SPD2010 |
| Touch | CST9217 | SPD2010 |
| Power | AXP2101 (rails, charging, battery gauge) | power latch |
| Audio | ES8311 + speaker, ES7210 + two mics | PCM5101 + speaker, I²S mic |
| I²C (SDA / SCL) | 15 / 14 | 11 / 10 |
| Add the SHT40 | on the 8-pin header (to be worked out: TODO) | on the header's shared I²C |

Both boards have PWR and BOOT buttons, which have no function in the firmware. The TF card
is not used.

### AMOLED-1.75 (466×466)
The plain version: no case, no GPS. The 1.75C has other reset pins. Chosen over the
AMOLED-1.43, which has no audio. Sources: Waveshare's wiki and xiaozhi-esp32's board file
for this board.

| Part | AMOLED-1.75 |
|---|---|
| Display | CO5300 over QSPI (CS 12, CLK 38, D0–D3 4–7, RST 39); columns start 6 px in; areas must start even and end odd |
| Brightness | display command `0x51` (no backlight) |
| Touch | CST9217 (INT 11, RST 40), mirrored X and Y |
| I²C | SDA 15, SCL 14: touch, AXP2101 (0x34), ES8311, ES7210, QMI8658, PCF85063, TCA9554 |
| Power | AXP2101: rails, battery charging and gauge, power key (hold 4 s = off) |
| Audio | ES8311 DAC + amplifier (enable GPIO 46) and ES7210 ADC with two mics, one I²S port (MCLK 42, BCLK 9, WS 45, DOUT 8, DIN 10) |
| Other | TF card (SPI: CS 41, MOSI 1, MISO 3, SCK 2), QMI8658 IMU, PCF85063 RTC, PWR and BOOT buttons, 8-pin header (3 GPIOs, UART) |

### LCD-1.46 (412×412)
Source: [product page](https://www.waveshare.com/esp32-s3-touch-lcd-1.46b.htm?sku=29565),
[wiki](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.46). Variants: 1.46B (no cover
glass), 1.46 (cover glass), 1.46C (widened cover glass), same electronics. About 25 $.

| Part | LCD-1.46 |
|---|---|
| Display | SPD2010 over QSPI (40 MHz), driven with `esp_lcd_spd2010` and `esp_lvgl_port` as in xiaozhi-esp32 |
| Brightness | PWM backlight on GPIO 5 |
| Power | power latch on GPIO 7 (holds the board on from battery) |
| I²C | SDA 11, SCL 10: touch, QMI8658, PCF85063, TCA9554 (display and touch resets); the SHT40 goes on the same bus |
| Audio | PCM5101 I²S DAC + amplifier + speaker, I²S mic on a second port |

## What the extras are used for
- **IMU (QMI8658):** flick = next or previous page, shake = dizzy eyes. Taps, tilt and pick up
  are not built yet.
- **Microphones:** noise level as `indoor.noise`, startle on a loud sound, curious on voices,
  angry when too loud (`sound.too_loud`).
- **Speaker:** countdown, alarm and notification sounds.
- **RTC (PCF85063):** keeps the time without a network.
- **Battery:** battery level and charging in the status bar (AMOLED-1.75).

## Sensors to add
All on the board's I²C bus. Check the addresses against the parts already on the bus before
adding one (SHT40 0x44, BH1750 0x23, SCD40 0x62).

| Part | Use | Status |
|---|---|---|
| Sensirion **SHT40** (0x44) | temperature and humidity, ±0.2 °C | driver built; to be wired on a cable |
| BH1750 (0x23) | ambient light: auto-brightness, sleepy mood | optional |
| SCD40 (0x62) | CO₂, the most useful indoor value | optional, about 25 € |

## Thermal design
- The board, the display and charging warm a small case by 1–4 °C. The SHT40 sits on a cable,
  in a **separate, ventilated chamber** at the bottom or back, away from the board.
- The firmware has an offset (`sensor.*_offset`). Calibrate against a reference thermometer
  after 1 h warm-up.

## Enclosure
- 3D-printed, "robot head" shape with a slight tilt (~15°) toward the user, and a chamber for
  the sensor.
- Inspiration: Stack-chan cases, MakerWorld "Companion" enclosure.
- Files will go in `hardware/enclosure/` (FreeCAD/STEP + STL).

## Parts list (approx.)
| Part | € |
|---|---|
| Waveshare LCD-1.46 board (the AMOLED-1.75 costs more) | ~25 |
| SHT40 breakout + cable | 5 |
| Filament | 3 |
| **Total** (LCD-1.46, without BH1750, SCD40 and battery) | **~33** |
