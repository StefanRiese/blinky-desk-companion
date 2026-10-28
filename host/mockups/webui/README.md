# Web UI mockup

A first mockup of the planned `buddyd` web UI and of the Blinky's own settings page, for review
before implementing (see the GUI items in `host/PLAN.md`). It is a design canvas, not code:
the pages are Design Component files (`*.dc.html`) and only render in the canvas.

Canvas: https://claude.ai/artifact/QLZZZhF2Du8D6Y57E76pED (private until shared)

- `Main.dc.html`: all connected Blinkies, host status, update hint
- `Device.dc.html`: one Blinky's settings (from the device's schema), sources, firmware update
- `Flash.dc.html`: first flash over USB (port, board, release, progress, then WiFi)
- `Blinky.dc.html`: the device's own page at `blinky.local` on a phone, in setup mode
- `canvas.json`: the layout of the canvas
