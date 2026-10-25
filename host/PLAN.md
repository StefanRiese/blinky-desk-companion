# Host Service Plan (`buddyd`)

Part of the [main plan](../PLAN.md). Protocol details: [protocol/PROTOCOL.md](../protocol/PROTOCOL.md).

## Goals
- One program for the **laptop** (USB serial) and the **Proxmox container** (TCP server).
  Both transports can be active at once, and several devices can connect.
- Supplies everything the device can't get itself (time, weather) plus extra data
  (system, Proxmox, GitLab CI, calendar, …).
- Easy to extend: a new data source = one provider file.
- Scriptable: `buddyctl notify "Build done"`.

## Stack
- Python ≥ 3.11, asyncio
- `pyserial-asyncio-fast`, `zeroconf`, `httpx`, `psutil`, `pydantic` (config + message
  models), `typer` (CLI), optional: `proxmoxer`, `python-gitlab`, `caldav`/`icalendar`,
  `aiomqtt`
- Packaging: `pyproject.toml` (hatch), installable with `pipx`; Docker image for the container

## Directory layout
```
host/
  pyproject.toml
  buddyd/
    __main__.py          entry: load config, start transports + providers
    config.py            pydantic settings from config.yaml (+ env overrides)
    protocol.py          message models, NDJSON encode/decode (shared golden tests)
    hub.py               Hub: device sessions, data cache, fan-out of updates
    control.py           Unix socket API for buddyctl
    transports/
      serial_link.py     auto-detect 303a:1001 (S3) / CH340 (CYD) or fixed port; DTR/RTS off
      tcp_server.py      asyncio server :7531, token check, zeroconf advert
      mqtt_bridge.py     optional: publish to deskbuddy/<id>/in/*, subscribe out/*
    providers/
      base.py            class Provider: name, interval, async poll() -> dict | Page
      time.py            time + POSIX TZ
      weather.py         Open-Meteo (current, hourly rain, 3-day, sunrise/sunset, AQI)
      system.py          psutil: CPU, RAM, temps, battery
      proxmox.py         node load, VMs/CTs up/down, storage, last backup status
      gitlab.py          pipeline status for configured projects, MRs waiting for review
      calendar.py        CalDAV/ICS: next meeting + countdown
      homeassistant.py   selected entity states
    sinks/
      mqtt.py, influx.py forward device sensor readings
  buddyctl/
    __main__.py          CLI: notify, mood, page, data, config wifi, devices, monitor
  deploy/
    Dockerfile, docker-compose.yml
    buddyd.service       systemd (system, for LXC)
    buddyd-user.service  systemd --user (laptop)
    99-deskbuddy.rules   udev: /dev/deskbuddy symlink + uaccess
  config.example.yaml
  tests/
```

## Core design

### Hub
- Keeps a **data cache** (latest value of every key, with TTL) and a list of **sessions**
  (one per connected device, any transport).
- On device `hello`: register the session, then send `time`, plus `weather` if listed in
  `needs`, then the whole cache and all active custom pages.
- Provider results → cache → diff → `data`/`page` messages to every session (or only
  matching device ids, if configured).
- Device `sensor` messages → sinks + cache (`indoor.*`), so other devices or HA can use them.

### Providers
```python
class Provider(Protocol):
    name: str
    interval: timedelta
    async def poll(self) -> ProviderResult  # data dict, optional pages, optional mood/notify
```
- Each provider runs in its own task with jitter and backoff on errors. One failing
  provider never stops the others.
- Keys are namespaced: `weather.*`, `sys.*`, `pve.*`, `ci.*`, `cal.*`, `ha.*`.
- Providers can emit **events** (e.g. pipeline failed → `notify` + mood `sad`).

### Config (`config.yaml`)
```yaml
location: {name: Home, lat: 51.2, lon: 6.8, tz: "CET-1CEST,M3.5.0,M10.5.0/3"}
transports:
  serial: {enabled: true, port: auto}
  tcp:    {enabled: false, bind: 0.0.0.0, port: 7531, token: !env BUDDY_TOKEN}
  mqtt:   {enabled: false, url: mqtt://broker:1883}
providers:
  weather: {interval: 15m}
  system:  {interval: 10s}
  proxmox: {interval: 30s, host: pve.lan, token_id: !env PVE_TOKEN_ID, token_secret: !env PVE_TOKEN_SECRET}
  gitlab:  {interval: 60s, url: https://gitlab.example, token: !env GITLAB_TOKEN, projects: [group/repo]}
  calendar: {interval: 5m, ics_url: !env CAL_URL}
```
Secrets only come from env vars or files, never committed.

### buddyctl
`notify TEXT [--mood M] [--sound]`, `mood NAME [--ttl S]`, `data KEY=VALUE…`,
`page NAME --json FILE`, `config wifi SSID PASS`, `devices`, `monitor` (show device logs).
Talks to the daemon through `$XDG_RUNTIME_DIR/buddyd.sock`. If no daemon is running and a
device is on serial, it falls back to direct serial access.

## Deployment
- **Laptop:** `pipx install ./host`, copy the udev rule, `systemctl --user enable --now buddyd`.
- **Proxmox:** Debian 12 LXC (unprivileged) with the systemd unit, or Docker Compose.
  Static IP/DNS name. Read-only PVE API token (`PVEAuditor` role).
- The device finds the container via mDNS (`_deskbuddy._tcp`) or the configured host.

## Milestone breakdown
| Main | Host tasks |
|---|---|
| M0 | Package skeleton, config loading, pytest + ruff in CI |
| M3 | protocol.py, Hub, serial transport, time + weather + system providers, `buddyctl notify` |
| M5 | TCP server + zeroconf, token auth, Dockerfile, LXC systemd unit |
| M7 | mood/notify events from providers |
| M8 | proxmox, gitlab, calendar providers; custom pages; sinks |
| M9 | MQTT bridge, Home Assistant provider |

## Testing
- Unit tests for protocol (golden files from `protocol/examples/`), Hub cache/diff,
  each provider with recorded HTTP responses (`respx`).
- `tools/fake_device.py`: speaks the device side over a PTY or TCP, so the whole daemon
  can be tested end-to-end in CI without hardware.
