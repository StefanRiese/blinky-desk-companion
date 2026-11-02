# To do

<!-- All open work for Blinky in one list: firmware, Buddy Link, hardware and open questions.
     The plans (PLAN.md, firmware/PLAN.md, host/PLAN.md, hardware/PLAN.md) say how things
     work and what is done; open items live only here. Tick an item off by removing it and,
     if it is user-visible, adding a CHANGELOG entry. -->

## Firmware: to build

- [ ] AMOLED burn-in: shift eyes and status bar by a few pixels every few minutes, dim further
      when idle, offer screen off at night (`ui.dim_night = 0`)
- [ ] Speaker volume setting (`sound.volume`; the ES8311 has a volume control) in the
      registry, the emulator and the setup page
- [ ] Log lines from the WiFi driver arrive split in two (`"I (5758) wifi:"`, then the text):
      `log_vprintf` should collect up to the newline before sending a `log` message
- [ ] Internal RAM: 48 KB free while running, but once down to 7 KB during start (25902cc);
      142 KB free with `wifi.enabled = false`. Find what peaks at start before adding features
      that need internal RAM
- [ ] IMU gestures not built yet: side tap, tilt, double tap, pick up / face down
- [ ] Config export/import on the device, sensor calibration
- [ ] NVS encryption for secrets; host-only sources in the config UI
- [ ] MQTT + Home Assistant discovery
- [ ] OTA updates over WiFi: two OTA partitions with rollback, upload on the web page at
      `blinky.local` (needs a password or PIN first) and/or pull the board's release app image
      from GitHub, version check on the info screen
- [ ] If the eyes need more speed: two draw units on both cores (`LV_USE_OS` FreeRTOS), QSPI
      80 MHz, a 32 KB instruction cache (costs 16 KB internal RAM)
- [ ] EventBus: not needed so far (the parts talk through hooks); only if input and link events
      outgrow them

## Firmware: check on the board

### LCD-1.46 (`ws146`, has not run on a real board yet)
- [ ] Display comes up, rim ring is round and centred, colours right (byte order)
- [ ] Backlight works, board stays on from battery (power latch)
- [ ] First flash with `buddy-ctl flash` (also on a board with a USB-UART bridge)

### AMOLED-1.75 (`amoled175`)
- [ ] The new eye shape (85 × 136) and `ui.color`: moods on the narrower eye, colours on the
      AMOLED, frame rate; neutral eyes with `mood.ambient` off
- [ ] Colours, 6 px offset, no torn edges, AXP2101 rails (mics powered), battery percentage
      plausible, mic level calibrated (`BOARD_MIC_SPL_AT_FULL_SCALE`)

### Both boards
- [ ] SHT40 connected and readings plausible
- [ ] RTC keeps the time over a power cycle
- [ ] The setup page reads and writes settings
- [ ] Setup AP + captive portal, Improv from the setup page, web page, NTP, TCP to `buddy-link`
      (also `auto`)
- [ ] Rotation (alignment after a turn, frame rate with software rotation)
- [ ] Backlight levels, beeps loud enough, mic levels plausible (dB calibration), startle and
      voices thresholds
- [ ] IMU: axes and signs right (flick right = next page), shake and flick thresholds
      (`buddy-ctl monitor` shows each gesture)
- [ ] Firmware update through `buddy-link` (`ota` messages, rollback)

## Buddy Link (host)

- [ ] Sources `gitlab`, `calendar`, `proxmox`, `ha`
- [ ] GoReleaser and packages (archives, `.deb`/`.rpm`, Homebrew tap, Scoop bucket; see
      [host plan](host/PLAN.md#build-and-release)). Today the hand-written release workflow
      covers the Mac app and firmware
- [ ] mDNS: the advert runs on one interface (the system's choice, on a laptop with VMware
      maybe a virtual adapter) and lists every local address; announce per interface with that
      interface's address (and the firmware should take the address in its own subnet)
- [ ] Windows: `install --user` could add firewall rules for TCP 7531 and UDP 5353 (private
      networks); a blocked port shows only as "no answer" on the device
- [ ] On a real Blinky: "Update to vX", "Update to the latest build from main", the update
      banner, the buttons and the tray count with a newer build (only automated tests so far)
- [ ] The tray icon and the macOS app on real Windows and macOS desktops

## Hardware

- [ ] Where the SHT40 connects on the AMOLED-1.75 (I²C 14/15): the 8-pin header has GPIOs and
      a UART
- [ ] What the TCA9554 does on the AMOLED-1.75 (xiaozhi only sets EXIO4 as an input)
- [ ] Order CYD + SHT40 (prototype)
- [ ] Wiring diagram (`hardware/wiring.svg`)
- [ ] Order S3 parts once M3 works
- [ ] Enclosure v1, thermal test, v2
- [ ] Optional: small carrier PCB (KiCad) for S3 + sensors + button

## Open questions

- Which "other data" matters most: PC stats, Proxmox status, calendar, Home Assistant, mail,
  CI build status?
- Should the device's own sensor data be forwarded (MQTT / Home Assistant / InfluxDB)?
- Web UI mockup (`host/mockups/webui`): one shared settings form for Buddy Link and
  `blinky.local`? "Last seen" for offline devices? Erase settings by default when flashing?
  Light/teal or the current dark/cyan look?
