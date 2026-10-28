# Hardware

This directory contains hardware documentation and resources for the **Blinky Desk Companion**, including a comparison of the supported display boards.

## Supported Hardware

The project currently evaluates two ESP32-S3-based boards from Waveshare.

| Board | Display | Resolution | Main features | Documentation |
|---|---|---|---|---|
| **ESP32-S3-Touch-AMOLED-1.75** | 1.75" AMOLED | 466 × 466 | Capacitive touch, Wi-Fi, Bluetooth LE, audio hardware, motion sensor, microSD | [Official documentation](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75) |
| **ESP32-S3-Touch-LCD-1.46** | 1.46" TFT LCD | 412 × 412 | Capacitive touch, Wi-Fi, Bluetooth LE, audio hardware, motion sensor, microSD, volume adjustment knob | [Official documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.46) |

Both boards use the ESP32-S3R8 microcontroller and provide wireless connectivity, onboard touch input, and a round display. However, their display controllers, GPIO assignments, and peripheral implementations differ.

## Hardware Comparison

For a detailed technical comparison, see:

**[Hardware Comparison — `hardware-comparison.md`](hardware-comparison.md)**

The comparison covers:

- General specifications and memory
- Display and touch controllers
- GPIO assignments and external headers
- I²C, UART, USB, and display interfaces
- Audio hardware, microphones, and speaker connections
- Motion sensors and real-time clocks
- Battery, charging, and power-management hardware
- Storage expansion and wireless connectivity
- Software compatibility and integration considerations

## Choosing a Board

### ESP32-S3-Touch-AMOLED-1.75

Consider this board when the priority is a larger, high-contrast display for expressive animations and a visually rich user interface.

- AMOLED panel with 466 × 466 resolution
- Capacitive touch input
- Integrated audio codec and microphone hardware
- ESP32-S3 processing and wireless connectivity

[View the official AMOLED documentation](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75)

### ESP32-S3-Touch-LCD-1.46

Consider this board when a smaller LCD display or the onboard volume adjustment knob better suits the intended interaction design.

- TFT LCD panel with 412 × 412 resolution
- Capacitive touch input
- Integrated audio hardware
- Onboard volume adjustment knob
- ESP32-S3 processing and wireless connectivity

[View the official LCD documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.46)

## Development and Integration

Both boards are based on the ESP32-S3 platform and can be developed using supported ESP32 toolchains, including Arduino and ESP-IDF.

When integrating either board with the Blinky Desk Companion firmware:

1. Confirm the exact board model and hardware revision.
2. Use the appropriate display and touch-controller drivers.
3. Verify the GPIO assignments against the board's official schematic.
4. Check for conflicts with onboard peripherals before connecting external hardware.
5. Test USB flashing, serial communication, audio, touch input, and display rendering.
6. Measure power consumption using the intended firmware before estimating battery life.

> **Important:** GPIO assignments are board-specific. Pins connected to the display, touch controller, audio hardware, storage, or other onboard peripherals may not be available for general-purpose use. Verify all electrical connections against the official documentation before wiring external components.

## Official Resources

- [Waveshare ESP32-S3-Touch-AMOLED-1.75](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75)
- [Waveshare ESP32-S3-Touch-LCD-1.46](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.46)
- [Detailed hardware comparison](hardware-comparison.md)

## Status

The hardware comparison documents the two candidate boards and their integration considerations. Final board selection should be based on the project's display requirements, peripheral needs, available GPIOs, firmware compatibility, and measured power consumption.
