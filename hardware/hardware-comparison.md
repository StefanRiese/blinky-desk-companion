# Hardware Comparison: Waveshare ESP32-S3 Touch Boards

This document compares two hardware platforms that can be used for the Blinky Desk Companion:

1. [Waveshare ESP32-S3-Touch-AMOLED-1.75](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75)
2. [Waveshare ESP32-S3-Touch-LCD-1.46](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.46)

It covers their specifications, GPIO assignments, onboard peripherals, display interfaces, audio, storage, power, connectivity, and integration considerations.

> **Pinout warning:** GPIO assignments below describe the documented board connections. A GPIO connected to an onboard peripheral is not necessarily available for external use. Check the schematic for the exact hardware revision before wiring additional components.

## 1. General Specifications

| Specification | ESP32-S3-Touch-AMOLED-1.75 | ESP32-S3-Touch-LCD-1.46 |
|---|---|---|
| Manufacturer | Waveshare | Waveshare |
| Main controller | ESP32-S3R8 | ESP32-S3R8 |
| CPU architecture | Dual-core Xtensa LX7, 32-bit | Dual-core Xtensa LX7, 32-bit |
| Maximum CPU frequency | 240 MHz | 240 MHz |
| Built-in SRAM | 512 KB | 512 KB |
| ROM | 384 KB | 384 KB |
| PSRAM | 8 MB | 8 MB |
| Flash | 16 MB | 16 MB |
| Wi-Fi | 2.4 GHz, 802.11 b/g/n | 2.4 GHz, 802.11 b/g/n |
| Bluetooth | Bluetooth 5 LE | Bluetooth 5 LE |
| Wireless antenna | Onboard antenna; external antenna option | Onboard antenna; external antenna option |
| USB connector | USB Type-C | USB Type-C |
| Display technology | AMOLED | TFT LCD |
| Display size | 1.75 inches | 1.46 inches |
| Display resolution | 466 × 466 pixels | 412 × 412 pixels |
| Display colors | 16.7 million | 16.7 million |
| Touch | Capacitive, two-point support | Capacitive |
| Touch interface | I²C | I²C |
| Display interface | QSPI | QSPI |
| Motion sensor | QMI8658, 6-axis IMU | QMI8658, 6-axis IMU |
| Real-time clock | PCF85063 | PCF85063 |
| Storage expansion | TF/microSD card slot | TF/microSD card slot |
| Battery connector | MX1.25, 2-pin, single-cell 3.7 V lithium battery | MX1.25, 2-pin, single-cell 3.7 V lithium battery |
| Battery charging | AXP2101 power-management IC | Onboard charging-management circuit |
| External GPIO header | 8-pin, 2.54 mm pitch | Multifunction header |
| External UART | UART0, GPIO43/GPIO44 | TXD GPIO43, RXD GPIO44 |
| External I²C | Shared onboard I²C bus on GPIO14/GPIO15 | Shared onboard I²C bus on GPIO10/GPIO11 |
| Development frameworks | Arduino, ESP-IDF | Arduino, ESP-IDF |
| Best-known distinction | Higher-resolution AMOLED display and advanced audio hardware | TFT LCD, accessible interface pin groups, and volume adjustment knob |

Sources: [AMOLED documentation](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75), [LCD documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.46).

## 2. Display Comparison

| Feature | AMOLED 1.75 | LCD 1.46 |
|---|---|---|
| Panel technology | AMOLED | TFT LCD |
| Diagonal size | 1.75 inches | 1.46 inches |
| Resolution | 466 × 466 | 412 × 412 |
| Total pixels | 217,156 | 169,744 |
| Pixel density | Approximately 376 PPI | Approximately 398 PPI |
| Display controller | CO5300 | SPD2010 |
| Display bus | QSPI | QSPI |
| Touch controller | CST9217 | SPD2010 |
| Touch bus | I²C | I²C |
| Touch interrupt | GPIO11 | GPIO4 |
| Display reset | GPIO39 | TCA9554 EXIO2 |
| Touch reset | GPIO40 | TCA9554 EXIO1 |
| Display chip select | GPIO12 | GPIO21 |
| Display synchronization | GPIO13 (TE) | GPIO18 (TE) |
| Display clock | GPIO38 | GPIO40 |
| Display data | GPIO4–GPIO7 | GPIO41, GPIO42, GPIO45, GPIO46 |
| Backlight control | Panel commands; no separate GPIO-driven backlight | GPIO5 |
| Primary advantage | Deep blacks, high contrast, vivid colors | Conventional LCD panel with documented backlight control |

The LCD has a higher approximate pixel density because its resolution is packed into a smaller panel. The AMOLED provides more total pixels and the larger physical display.

Pixel density values are approximate calculations based on the stated diagonal and resolution.

## 3. Processor and Memory

| Feature | AMOLED 1.75 | LCD 1.46 |
|---|---|---|
| MCU | ESP32-S3R8 | ESP32-S3R8 |
| CPU cores | 2 | 2 |
| CPU architecture | Xtensa LX7 | Xtensa LX7 |
| Maximum clock | 240 MHz | 240 MHz |
| SRAM | 512 KB | 512 KB |
| ROM | 384 KB | 384 KB |
| PSRAM | 8 MB | 8 MB |
| Flash | 16 MB | 16 MB |
| Wi-Fi | Supported | Supported |
| Bluetooth LE | Supported | Supported |
| External peripherals | Multiple | Multiple |

Both devices offer broadly equivalent processing and memory resources. The display technology, board wiring, power-management implementation, and peripheral configuration are more significant differentiators than raw CPU performance.

## 4. AMOLED 1.75: GPIO Assignments

The following table lists the documented MCU GPIO connections for the standard AMOLED board. Some signals are reserved for onboard peripherals.

| GPIO | Signal | Connected peripheral | External-use considerations |
|---|---|---|---|
| GPIO0 | BOOT | BOOT button | Boot-strapping pin; use care during reset |
| GPIO1 | SD_CMD | TF/microSD | Reserved while SD card interface is active |
| GPIO2 | SD_CLK | TF/microSD | Reserved while SD card interface is active |
| GPIO3 | SD_D0 | TF/microSD | Reserved while SD card interface is active |
| GPIO4 | LCD_DATA0 | CO5300 display QSPI | Reserved for display |
| GPIO5 | LCD_DATA1 | CO5300 display QSPI | Reserved for display |
| GPIO6 | LCD_DATA2 | CO5300 display QSPI | Reserved for display |
| GPIO7 | LCD_DATA3 | CO5300 display QSPI | Reserved for display |
| GPIO8 | I2S_DOUT | ES8311 audio codec | Reserved for audio playback |
| GPIO9 | I2S_BCLK | Audio codec and ADC | Shared audio clock |
| GPIO10 | I2S_DIN | ES7210 audio ADC | Reserved for audio capture |
| GPIO11 | TP_INT | CST9217 touch controller | Touch interrupt |
| GPIO12 | LCD_CS | CO5300 display | Display chip select |
| GPIO13 | LCD_TE | CO5300 display | Display synchronization |
| GPIO14 | I2C_SCL | Shared I²C bus | Used by onboard peripherals |
| GPIO15 | I2C_SDA | Shared I²C bus | Used by onboard peripherals |
| GPIO16 | GPIO16 | Expansion header pin 8 | General-purpose 3.3 V GPIO |
| GPIO17 | GPIO17 | Expansion header pin 6 | May be shared with GNSS UART on the -G variant |
| GPIO18 | GPIO18 | Expansion header pin 7 | May be shared with GNSS UART on the -G variant |
| GPIO19 | USB_D- | Native USB | Reserved when native USB is in use |
| GPIO20 | USB_D+ | Native USB | Reserved when native USB is in use |
| GPIO21 | QMI_INT2 | QMI8658 IMU | Motion-sensor interrupt |
| GPIO38 | LCD_PCLK | CO5300 display | Display clock |
| GPIO39 | LCD_RST | CO5300 display | Display reset |
| GPIO40 | TP_RST | CST9217 touch controller | Touch reset |
| GPIO41 | SD_D3/CS | TF/microSD | Wired to card interface |
| GPIO42 | I2S_MCLK | ES8311/ES7210 | Audio master clock |
| GPIO43 | U0TXD | UART0 | Expansion header pin 5 |
| GPIO44 | U0RXD | UART0 | Expansion header pin 4 |
| GPIO45 | I2S_LRCK/WS | Audio codec and ADC | Audio word-select clock |
| GPIO46 | PA_CTRL | NS4150B amplifier circuit | Amplifier control; use the board support software |

### AMOLED: External 8-pin header

| Header pin | Signal | Description |
|---|---|---|
| 1 | VBUS | USB supply rail |
| 2 | GND | Ground |
| 3 | 3V3 | 3.3 V power rail |
| 4 | GPIO44 / U0RXD | UART0 receive |
| 5 | GPIO43 / U0TXD | UART0 transmit |
| 6 | GPIO17 | General GPIO |
| 7 | GPIO18 | General GPIO |
| 8 | GPIO16 | General GPIO |

All header GPIO signals use 3.3 V logic and are not 5 V tolerant. VBUS is a power rail, not a GPIO.

## 5. LCD 1.46: GPIO Assignments

### 5.1 External interface pins

| Header/interface label | MCU pin | Function | Notes |
|---|---|---|---|
| GND | GND | Ground | Common ground |
| 3V3 | 3V3 | 3.3 V output | Check current requirements before powering external devices |
| SCL | GPIO10 | I²C clock | Shared with onboard peripherals |
| SDA | GPIO11 | I²C data | Shared with onboard peripherals |
| TXD | GPIO43 | UART transmit | Can be configured as GPIO |
| RXD | GPIO44 | UART receive | Can be configured as GPIO |
| 5V | 5V | 5 V output | Power pin, not a GPIO |
| DN | GPIO19 | USB D- | Reusing it can affect USB functionality and flashing |
| DP | GPIO20 | USB D+ | Reusing it can affect USB functionality and flashing |

### 5.2 Display and touch

| Signal | MCU pin | Function |
|---|---|---|
| LCD_SDA0 | GPIO46 | Display QSPI data 0 |
| LCD_SDA1 | GPIO45 | Display QSPI data 1 |
| LCD_SDA2 | GPIO42 | Display QSPI data 2 |
| LCD_SDA3 | GPIO41 | Display QSPI data 3 |
| LCD_SCK | GPIO40 | Display clock |
| LCD_CS | GPIO21 | Display chip select |
| LCD_TE | GPIO18 | Display synchronization |
| LCD_RST | EXIO2 | Display reset through GPIO expander |
| LCD_BL | GPIO5 | Backlight control |
| TP_SDA | GPIO11 | Touch I²C data |
| TP_SCL | GPIO10 | Touch I²C clock |
| TP_INT | GPIO4 | Touch interrupt |
| TP_RST | EXIO1 | Touch reset through GPIO expander |

### 5.3 TF/microSD card

| Signal | MCU/expander pin | Function |
|---|---|---|
| SD_D0 / MISO | GPIO16 | SD card data |
| SD_CMD / MOSI | GPIO17 | SD card command/data |
| SD_SCK / SCLK | GPIO14 | SD card clock |
| SD_D3 / CS | EXIO3 | SD card select |
| SD_D1 | NC | Not connected |
| SD_D2 | NC | Not connected |

### 5.4 Motion sensor: QMI8658

| Signal | MCU/expander pin | Function |
|---|---|---|
| IMU_SCL | GPIO10 | I²C clock |
| IMU_SDA | GPIO11 | I²C data |
| IMU_INT1 | EXIO5 | Interrupt 1 |
| IMU_INT2 | EXIO4 | Interrupt 2 |

### 5.5 Real-time clock: PCF85063

| Signal | MCU pin | Function |
|---|---|---|
| RTC_SCL | GPIO10 | I²C clock |
| RTC_SDA | GPIO11 | I²C data |
| RTC_INT | GPIO9 | RTC interrupt |

### 5.6 Microphone

| Signal | MCU pin | Function |
|---|---|---|
| MIC_WS | GPIO2 | Audio word-select |
| MIC_SCK | GPIO15 | Audio serial clock |
| MIC_SD | GPIO39 | Microphone data |

### 5.7 Speaker audio output

| Signal | MCU pin | Function |
|---|---|---|
| Speak_DIN | GPIO47 | Audio data |
| Speak_LRCK | GPIO38 | Audio word-select |
| Speak_BCK | GPIO48 | Audio bit clock |

The LCD board also integrates the PCM5101 audio decoder, a power amplifier, and a speaker connector.

## 6. Onboard Components and Peripherals

| Component | AMOLED 1.75 | LCD 1.46 |
|---|---|---|
| Display controller | CO5300 | SPD2010 |
| Touch controller | CST9217 | SPD2010 |
| Power-management IC | AXP2101 | MP1605GTF-Z power module and onboard charging-management circuitry |
| GPIO expander | TCA9554 | TCA9554PWR |
| Motion sensor | QMI8658 | QMI8658 |
| RTC | PCF85063 | PCF85063 |
| Audio codec/decoder | ES8311 | PCM5101 |
| Audio input ADC | ES7210 | Microphone input; separate ADC model not specified in the product table |
| Power amplifier | NS4150B | Onboard amplifier |
| Microphones | Two onboard microphones | Onboard microphone |
| Speaker | External speaker connector | Speaker and speaker connector |
| Storage | TF/microSD slot | TF/microSD slot |
| Battery connector | MX1.25, 2-pin | MX1.25, 2-pin |
| Battery monitoring | AXP2101 | Verify exact implementation for board revision |
| RTC backup connection | Supported through board power circuitry | Dedicated RTC battery header documented |
| Volume adjustment knob | Not listed in the standard feature description | Present |
| BOOT button | Present | Present |
| PWR button | Present | Present |
| External antenna option | IPEX connector; resistor configuration required | IPEX connector; resistor configuration required |

## 7. Power and Battery

| Feature | AMOLED 1.75 | LCD 1.46 |
|---|---|---|
| Battery type | Single-cell lithium battery | Single-cell lithium battery |
| Nominal battery voltage | 3.7 V | 3.7 V |
| Battery connector | MX1.25, 2-pin | MX1.25, 2-pin |
| Charging | Integrated AXP2101 power-management system | Integrated charging-management circuitry |
| Battery discharge support | Supported | Supported |
| Battery monitoring | Supported through AXP2101 | Confirm details for the specific revision |
| USB power/data connector | USB Type-C | USB Type-C |
| Regulated 3.3 V header | Yes | Yes |
| External 5 V header | VBUS | Documented 5 V output |
| Published typical current | Not specified here | Not specified here |
| Published peak current | Not specified here | Not specified here |
| Published deep-sleep current | Not specified here | Not specified here |

Power consumption depends on display brightness, wireless activity, audio playback, SD-card activity, and firmware. Do not assume that either board has better battery life without measurements under equivalent workloads.

## 8. Connectivity and Interfaces

| Interface | AMOLED 1.75 | LCD 1.46 |
|---|---|---|
| Wi-Fi | 2.4 GHz 802.11 b/g/n | 2.4 GHz 802.11 b/g/n |
| Bluetooth | Bluetooth 5 LE | Bluetooth 5 LE |
| USB | Native USB Type-C | USB Type-C |
| UART | GPIO43/GPIO44 on header | GPIO43/GPIO44 on header |
| I²C | GPIO14/GPIO15 | GPIO10/GPIO11 |
| Display bus | QSPI | QSPI |
| Touch bus | I²C | I²C |
| SD-card bus | SDMMC, documented 1-bit configuration | SD-card interface; verify firmware configuration |
| Audio bus | I²S/TDM | I²S-based audio |
| Expansion GPIO | GPIO16/GPIO17/GPIO18 | Multiple GPIOs through multifunction header |
| External antenna | Supported through hardware modification | Supported through hardware modification |

## 9. Software Development

| Feature | AMOLED 1.75 | LCD 1.46 |
|---|---|---|
| Arduino IDE | Supported | Supported |
| ESP-IDF | Supported | Supported |
| LVGL | Suitable for graphical UI applications | Explicitly documented as suitable for LVGL |
| USB firmware flashing | Supported | Supported |
| Serial debugging | Supported | Supported |
| Wi-Fi applications | Supported | Supported |
| Bluetooth LE applications | Supported | Supported |
| Custom display drivers | CO5300-specific integration | SPD2010-specific integration |
| Touch driver | CST9217 | SPD2010 |
| Power-management integration | AXP2101 | Board-specific power circuitry |
| GPIO expansion | Limited exposed header pins | Multifunction header |

Although the boards share an MCU family, their display controllers, touch controllers, GPIO wiring, and power-management implementations differ. Firmware written for one should not be assumed to run unchanged on the other.

## 10. Advantages and Limitations

| Category | AMOLED 1.75 | LCD 1.46 |
|---|---|---|
| Display quality | Excellent contrast and deep blacks | Conventional LCD rendering |
| Display resolution | Higher total resolution | Lower total resolution |
| Physical display size | Larger | Smaller |
| External GPIO access | Three general GPIOs on expansion header | Multiple GPIOs through multifunction header |
| Audio hardware | ES8311 codec, ES7210 ADC, dual microphones, amplifier | PCM5101 decoder, microphone, amplifier, volume knob |
| Power management | Integrated AXP2101 PMIC | Integrated charging/power circuitry |
| Storage | TF/microSD | TF/microSD |
| Touch functionality | Two-point capacitive touch | Capacitive touch |
| Hardware integration | Highly integrated wearable/compact UI-style platform | Highly integrated LCD UI platform |
| Main limitation | Most GPIOs are assigned to onboard hardware | Most GPIOs are also assigned to onboard hardware |
| Main integration concern | Shared I²C, audio, display, and SD signals | Shared I²C, audio, display, and SD signals |

## 11. Suitability for Blinky Desk Companion

| Requirement | Better starting point | Reason |
|---|---|---|
| Rich visual interface | AMOLED 1.75 | Larger panel, higher total resolution, high contrast |
| Round display UI | Either | Both have round displays |
| Animations and graphical widgets | AMOLED 1.75 | More pixels and AMOLED contrast; both have the same MCU and memory capacity |
| Physical volume adjustment | LCD 1.46 | Onboard volume adjustment knob |
| Audio input and output | Depends on firmware needs | Both provide audio hardware; their codecs and microphone configurations differ |
| Battery-powered operation | Neither can be selected on specs alone | Measure actual current draw |
| Connecting external sensors | Depends on required pins | Compare the expansion header and reserved GPIO assignments |
| Easy reuse of existing firmware | Depends on the chosen display driver | Display and touch controllers differ |
| Higher contrast and vivid display | AMOLED 1.75 | AMOLED panel |
| Compact display footprint | LCD 1.46 | Smaller diagonal |

## 12. Recommendation

For a desk companion where the primary goal is an attractive, expressive graphical interface, the **ESP32-S3-Touch-AMOLED-1.75** is the stronger starting point because of its larger, higher-resolution AMOLED screen and integrated audio hardware.

The **ESP32-S3-Touch-LCD-1.46** is a good alternative when the smaller LCD form factor or onboard volume adjustment knob is more useful.

Both boards use the same ESP32-S3R8 processor and have 8 MB PSRAM and 16 MB flash, so the decision should primarily depend on display quality, audio requirements, accessible GPIOs, power consumption, and the software integration effort.

Before finalizing the hardware choice, verify the precise board revision and measure power consumption with the intended firmware.

## 13. Official References

- [Waveshare ESP32-S3-Touch-AMOLED-1.75 documentation](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75)
- [Waveshare ESP32-S3-Touch-LCD-1.46 documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.46)
- [AMOLED hardware reference and GPIO assignments](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md)

**Revision note:** Board variants exist for both product families. Verify the actual SKU and schematic before treating any pin assignment or peripheral feature as universal across all variants.
