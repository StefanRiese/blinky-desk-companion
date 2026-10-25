# Desk Buddy Protocol v1 (draft)

Shared between [firmware](../firmware/PLAN.md) and [host](../host/PLAN.md).
Status: **draft**. Freeze at M3.

## 1. Framing
- **Serial / TCP:** newline-delimited JSON (NDJSON). One UTF-8 JSON object per line, `\n`
  terminated, max **2048 bytes** per line. Receivers ignore lines that don't start with `{`.
- **MQTT:** one JSON object per MQTT message, same content (see §5).
- Unknown message types and unknown fields **must be ignored** (forward compatibility).

## 2. Envelope
```json
{"t":"<type>","v":1,"id":17, "...": "payload"}
```
| field | req. | meaning |
|---|---|---|
| `t` | yes | message type |
| `v` | hello only | protocol version (major). Mismatch → `err` + close |
| `id` | no | request id. If present, the receiver replies with `ack`/`err` carrying the same id |

## 3. Handshake & keepalive
1. Device → `hello`. Host → `hello`.
2. Host sends initial state (`time`, `weather` if needed, cached `data`, `page`s).
3. Each side sends `ping` after 10 s without sending anything else. The link is down after 30 s of silence.
4. TCP: if the host requires a token, it sends `err` `{"code":"auth"}` and closes on a missing or wrong token.

## 4. Messages

### Device → Host
```json
{"t":"hello","v":1,"dev":"buddy-a1b2c3","fw":"0.3.0","board":"s3",
 "caps":["eyes","touch","sht40"],"needs":["time","weather"],"token":"…"}
{"t":"sensor","temp":22.4,"hum":41.2,"lux":120,"co2":650}
{"t":"event","kind":"button","name":"short"}          // short | long | double
{"t":"event","kind":"page","name":"forecast"}
{"t":"event","kind":"gesture","name":"flick_right"}   // flick_l/r, tap_l/r, double_tap, shake, pickup, face_down
{"t":"event","kind":"touch","name":"swipe_left"}      // swipe_l/r/up/down, tap, long_press
{"t":"event","kind":"pomodoro","state":"started"}     // started | break | stopped
{"t":"ack","id":17}
{"t":"err","id":17,"code":"bad_value","msg":"brightness out of range"}
{"t":"log","lvl":"I","tag":"wifi","msg":"connected"}
{"t":"ping"}
```

### Host → Device
```json
{"t":"hello","v":1,"host":"buddyd@laptop","ver":"0.3.0"}
{"t":"time","epoch":1793000000,"tz":"CET-1CEST,M3.5.0,M10.5.0/3"}
{"t":"weather","ttl":3600,
 "now":{"temp":11.5,"feels":9.8,"hum":78,"code":61,"wind":14,"is_day":1},
 "rain_next_2h":[0,0,0.2,0.8,1.1,0.4,0,0],
 "days":[{"d":"2026-10-25","min":6,"max":13,"code":61,"pop":80,"sunrise":"07:12","sunset":"17:21"}]}
{"t":"data","ttl":120,"kv":{"sys.cpu":23,"pve.vms_up":7,"pve.vms_total":8,"cal.next":"Standup","cal.next_in":12}}
{"t":"data","del":["ci.main"]}
{"t":"mood","mood":"happy","ttl":30}
{"t":"notify","text":"Pipeline failed: main","icon":"ci_fail","mood":"sad","sound":"beep","dur":8}
{"t":"page","name":"ci","title":"GitLab","ttl":600,
 "items":[{"w":"icon+text","icon":"ok","text":"main"},
          {"w":"icon+text","icon":"fail","text":"feature/x"},
          {"w":"value","label":"MRs to review","value":2}]}
{"t":"page","name":"ci","del":true}
{"t":"config","id":3,"set":{"wifi.ssid":"Home","wifi.pass":"…","loc.lat":51.2,"loc.lon":6.8,
 "tcp.host":"buddy.lan","ui.brightness":180}}
{"t":"ping"}
```

Weather codes follow WMO codes (as used by Open-Meteo). Units: °C, %, km/h, mm, hPa, ppm.

### Custom page widgets
| `w` | fields |
|---|---|
| `value` | `label`, `value`, `unit?` |
| `bar` | `label`, `value`, `max`, `warn?`, `crit?` |
| `icon+text` | `icon`, `text` |
| `list` | `label?`, `lines[]` (max 6) |
| `sparkline` | `label`, `values[]` (max 48) |

Built-in icon names: `ok`, `fail`, `warn`, `info`, `running`, `ci_ok`, `ci_fail`, `mail`,
`cal`, `server`, `vm`, `disk`, `temp`, `drop`, `sun`, `rain`, ... (list maintained in firmware).

### Data key conventions
`weather.*`, `indoor.*` (device sensors), `sys.*` (laptop), `pve.*` (Proxmox), `ci.*`
(GitLab), `cal.*` (calendar), `ha.*` (Home Assistant), `user.*` (free use).

## 5. MQTT mapping
| Direction | Topic | Payload |
|---|---|---|
| host → device | `deskbuddy/<dev>/in/<t>` | message without `t` (or with, both accepted) |
| device → host | `deskbuddy/<dev>/out/<t>` | message |
| status | `deskbuddy/<dev>/status` | `online` / `offline` (retained, LWT) |
| broadcast | `deskbuddy/all/in/<t>` | to all devices |

`data` and `page` should be published **retained**, so a rebooted device gets them again.

## 6. Examples
`protocol/examples/*.ndjson` contain golden message sequences used by both the firmware
(native tests) and host (pytest) test suites.
