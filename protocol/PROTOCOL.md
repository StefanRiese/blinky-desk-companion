# Blinky Protocol v1 (draft)

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
The **device owns all configuration**. The host (`buddyd`) is a stateless gateway that
fetches what the device subscribes to (see [host plan](../host/PLAN.md#session-flow)).

1. Device → `hello`. Host → `hello` with the sources it offers (`srcs`), whether it has
   internet (`net`), and the device id it answers (`dev`).
   - **Serial:** the device may have booted before the host opened the port, so the host
     also sends a `hello` **without `dev`** when it opens the port (and every 10 s while no
     device has said hello). A device answers that with its own `hello`, unless its own
     hello is already waiting for an answer (< 5 s old).
   - A device that gets no host `hello` with its `dev` within 5 s sends `hello` again.
   - Every device `hello` starts a fresh session on the host (all subscriptions dropped).
   - **Setup page:** the [setup page](https://stefanriese.github.io/esp32_desk_buddy/) (Web
     Serial) talks to the device like a host, but its `hello` carries `"role":"setup"`. The
     device then only answers `config` get/set on that link: no `sub`, and its data sources
     are not routed through it. The device's `hello` on this link carries no link token.
   - **Finding the host over TCP:** `buddyd` announces `_deskbuddy._tcp.local` via mDNS
     (TXT `ver=…`, `proto=1`, `token=0|1`). A device with `link.tcp_host = "auto"` connects
     to the first answer.
2. Device → one `sub` per source it wants from this host. Host → `ack`, or `err` with
   `unknown_src` / `bad_params`.
3. Host sends the first result of each subscription right away, then on its interval.
   Errors and recovery are reported with `sub_state`.
4. Subscriptions live only as long as the connection. After a reconnect the device sends
   them again. A device connected to several hosts subscribes each source on **one** host only
   (ModeManager picks it) and moves it when that host goes away.
5. Each side sends `ping` after 10 s without sending anything else. The link is down after 30 s of silence.
6. TCP: if the host requires a token, it sends `err` `{"code":"auth"}` and closes on a missing or wrong token.

## 4. Messages

### Device → Host
```json
{"t":"hello","v":1,"dev":"buddy-a1b2c3","fw":"0.3.0","board":"s3",
 "caps":["eyes","touch","sht40"],"token":"…"}
{"t":"sub","id":5,"sid":"time","src":"time","every":3600}
{"t":"sub","id":6,"sid":"weather","src":"weather","every":900,"p":{"lat":51.2,"lon":6.8,"days":3}}
{"t":"sub","id":7,"sid":"ci","src":"gitlab","every":60,
 "p":{"url":"https://gitlab.example","token":"…","projects":["group/repo"]}}
{"t":"sub","id":8,"sid":"btc","src":"http","every":300,
 "p":{"url":"https://api.example/price","pick":{"eur":"/bpi/EUR/rate_float"}}}
{"t":"unsub","sid":"ci"}
{"t":"sensor","temp":22.4,"hum":41.2,"lux":120,"co2":650,"noise":42}
{"t":"event","kind":"button","name":"short"}          // short | long | double
{"t":"event","kind":"page","name":"forecast"}
{"t":"event","kind":"gesture","name":"flick_right"}   // flick_l/r, tap_l/r, double_tap, shake, pickup, face_down
{"t":"event","kind":"touch","name":"swipe_left"}      // swipe_l/r/up/down, tap, long_press
{"t":"event","kind":"pomodoro","state":"started"}     // started | break | stopped
{"t":"event","kind":"countdown","state":"done"}       // started | paused | reset | done | stopped
{"t":"event","kind":"alarm","name":"07:00","state":"ringing"}   // ringing | snoozed | stopped
{"t":"event","kind":"sound","name":"loud"}            // sudden loud noise (levels only, no audio)
{"t":"ack","id":17}
{"t":"err","id":17,"code":"bad_value","msg":"brightness out of range"}
{"t":"config","id":4,"values":{"ui.brightness":180,"wifi.ssid":"Home","wifi.pass":"***"}}   // reply to config get
{"t":"log","lvl":"I","tag":"wifi","msg":"connected"}
{"t":"ping"}
```

### Host → Device
```json
{"t":"hello","v":1,"dev":"buddy-a1b2c3","host":"buddyd@laptop","ver":"0.3.0","net":true,
 "srcs":["time","weather","aqi","http","gitlab","calendar","proxmox","ha","sys"]}
{"t":"host_state","net":false}
{"t":"sub_state","sid":"ci","state":"error","msg":"HTTP 401","retry":120}   // ok | error | backoff
{"t":"time","epoch":1793000000}
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
{"t":"config","id":4,"get":["*"]}                    // secrets come back masked
{"t":"ping"}
```

`time` carries only the epoch. The device converts it with its own TZ setting (IANA name →
POSIX string from a table built into the firmware), so the host needs no location data.
`config` messages come from the host (forwarded from `buddyctl`) or from the setup page; the
host never stores device settings.

Weather codes follow WMO codes (as used by Open-Meteo). Units: °C, %, km/h, mm, hPa, ppm.
They are always metric on the wire; the device converts for display (`units.*` settings).

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

### Subscriptions
| field | meaning |
|---|---|
| `sid` | subscription id chosen by the device, unique per connection. Also the key prefix for `data` results (`ci.main`, `btc.eur`) |
| `src` | source name, must be in the host's `srcs` |
| `every` | interval in seconds. The host clamps it to the source minimum and reports the effective value in `ack` (`{"t":"ack","id":7,"every":60}`) |
| `p` | source parameters, may contain secrets (the host keeps them in memory only and never logs them) |

Source list and parameters: [host plan](../host/PLAN.md#sources).

### Error codes
| `code` | meaning |
|---|---|
| `version` | protocol major version mismatch (host closes) |
| `auth` | TCP token missing or wrong (host closes) |
| `bad_hello` | `hello` without `dev` or not parseable |
| `no_hello` | `sub` before the handshake |
| `unknown_src` | the host doesn't offer this source (see `srcs`) |
| `bad_params` | invalid `sid` (`[a-z0-9_]{1,16}`) or source parameters |
| `too_many` | more than 32 subscriptions |
| `bad_value` | device: a `config` `set` value was rejected |

### Data key conventions
`weather.*`, `indoor.*` (device sensors), `user.*` (pushed by scripts via `buddyctl data`).
Everything else uses the subscription id as prefix. Suggested ids: `sys`, `pve`, `ci`,
`cal`, `ha`.

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
(native tests) and host (`go test`) test suites.
