# Nocturne wire schema

The device payload is **newline-delimited JSON** over TCP (PC server, port 8888)
or HTTPS (the always-on *nocturne-lite* fallback). Three codebases must agree
on it:

| Producer | What it emits | Where |
|---|---|---|
| **PC server** (`NocturneServer/monitor.py` → `payload.py`) | the full payload over TCP | `build_payload()` |
| **lite fallback** (`nocturne-lite/app.py`) | a SUBSET (weather + forest/svc + events + clk) over HTTPS | `build_payload()` |
| **firmware** (`ESP32-C6-Companion`) | the consumer — **canonical parser** | `src/net/TelemetryClient.cpp :: parsePayload()` |

> The firmware parser is the source of truth. A field rename on a producer that
> isn't mirrored here **silently desyncs the board** — especially the lite
> fallback, which the board depends on with the PC off. Keep this file in sync
> with `parsePayload()` and the two `build_payload()`s.

String fields are truncated by the firmware to the buffer sizes below (`copyStr`);
emit shorter and you're safe. Integers default to `-1`/`0` when absent.

## Blocks the **lite fallback** also emits (must match exactly)

### Weather (top-level keys)
| key | type | meaning |
|---|---|---|
| `wt` | int | current temperature °C |
| `wp` | int | precipitation probability % |
| `wd` | string | short description (`Clear`/`Rain`/…) |
| `wi` | int | WMO weather code |
| `wf` | array | up to 5 × `[tmin, tmax, wmocode]` daily forecast |

### `clk`
`"HH:MM"` string (the device also keeps its own NTP clock; payload `clk` is a
fallback). Lite emits MSK.

### `events` — `{n, top, sev, txt, list}`
| key | type | size | meaning |
|---|---|---|---|
| `n` | int | | number of firing alerts |
| `top` | string | ≤20 | highest-severity alert name (banner) |
| `sev` | string | ≤11 | its severity (`critical`/`warning`/`info`/`none`) |
| `txt` | string | ≤60 | human text (summary/description) |
| `list` | string[] | ≤4 × ≤20 | alert names for the list scene |

Severity ranking (banner pick): `critical > warning > info > none`. The PC
server builds this in `alert_events.build_events_block`; lite mirrors it from
Alertmanager v2 `/api/v2/alerts` — keep the two identical.

### `forest` — `{n, up, nodes:[…]}` (≤6 nodes)
node: `{id, name, st, cpu, ram, disk, extra}`
| key | type | size | meaning |
|---|---|---|---|
| `id` | string | ≤8 | stable node id — **`srv`** / **`pc`** are canonical; both producers must agree or the board renames its tile on failover |
| `name` | string | ≤16 | display name (Latin-safe; some node-name fonts are Latin-only) |
| `st` | string | ≤5 | `up` / `warn` / `down` (`warn` = reachable but a resource ≥90%) |
| `cpu`,`ram`,`disk` | int | | percent, **rounded** (not truncated), `-1` = unknown → the board draws `--`, never `0%` |
| `extra` | string | ≤16 | free badge text |

### `zb` — smart-home sensors `{n, list:[…]}` (≤4)
Sensor readings for the ПОГОДА tile. Since v1.14.0 **the board itself is a
Zigbee coordinator** (channel 25, +380 KB flash, single app partition — OTA was
traded away for it) and fills these readings locally from paired sensors. The
payload block still works exactly as before, so a server-side producer
(Zigbee2MQTT, Home Assistant, a Yandex-hub poller) can feed the same screen on
boards built without the stack (`nocturne-c6-nozb`).

entry: `{name, t, h, b, age, p, lux, mo}`
| key | type | size | meaning |
|---|---|---|---|
| `name` | string | ≤16 | room / sensor label, shown on the ПОГОДА tile |
| `t` | int | | temperature **×10** (23.6 °C → `236`); omit when unknown |
| `h` | int | | relative humidity %, -1 = unknown |
| `b` | int | | battery %, -1 = unknown; under 20 the bar turns red |
| `age` | int | | seconds since the sensor last reported, -1 = unknown |
| `p` | int | | atmospheric pressure **hPa**, -1 = this sensor has no barometer |
| `lux` | int | | ambient light, **raw lux**, -1 = no light sensor on this device |
| `mo` | int | | **seconds since** motion was last seen, -1 = not a motion sensor |

> `lux` and `mo` are the Aqara RTCGQ11LM. They are how a consumer tells a
> motion sensor from a climate one: the kind is read from which fields a
> device populates, not from a type byte nobody sets. A device that has
> neither has no PIR; a device that has no `t` has no thermometer.
>
> `mo` is deliberately a DURATION and never a 0/1 occupancy flag. The
> RTCGQ11LM reports when its PIR fires and stays silent otherwise — it has no
> "unoccupied" report at all — so silence means "empty room" and "someone
> sitting still" equally. A boolean would be the producer inventing a fact the
> hardware never sent. Whoever renders this picks its own quiet threshold.
>
> `lux` is raw lux, not the ZCL log encoding (`10^((raw-1)/10000)`). Aqara's
> own sensors send raw, which is also how Zigbee2MQTT reads them; applying the
> compliant formula puts a dim room at "1" and a bright one at "4". Note the
> reading is taken when the device WAKES on motion, so it is the brightness at
> the moment someone walked past, not the brightness now.

> `age` is not optional in spirit. Battery Zigbee sensors go quiet for hours;
> a last-known reading presented as current is the same lie the "no signal"
> handling exists to prevent. Over an hour old and the whole tile dims.

### `svc` — `{n, up, list:[…]}`
entry: `{name (≤18), st (up/down), ms (int, -1 = unknown)}`

### `dock` — `{n, up}`
container counts (lite may omit → device shows 0).

### `src`
producer tag, e.g. `forest-lite`; lite appends `!wx`/`!forest`/`!svc` when a
block failed to populate. Ignored by the firmware, useful in logs.

## Blocks only the **PC server** emits (no lite equivalent)

`tools/check_schema.py` fails the build if the firmware parses a key that is not
named somewhere in this file — so these tables are enumerated, not summarised.
The old "…" hid twenty-odd fields the board actually acts on.

### Hardware scalars (top level, all int unless noted)
| key | meaning | | key | meaning |
|---|---|---|---|---|
| `ct` | CPU temp °C | | `gt` | GPU temp °C |
| `cl` | CPU load % | | `gl` | GPU load % |
| `cc` | CPU clock MHz | | `pw` | CPU package power W |
| `gh` | GPU hotspot °C | | `gv` | VRAM used % |
| `gclock` | GPU core clock | | `vclock` | GPU memory clock |
| `gtdp` | GPU power % of TDP | | `ch` | chipset temp °C |
| `ru` (float) | RAM used GB | | `ra` (float) | RAM total GB |
| `vu` (float) | VRAM used GB | | `vt` (float) | VRAM total GB |
| `nd` | net down kb/s | | `nu` | net up kb/s |
| `pg` / `ping` | ping ms (either key) | | `dr` / `dw` | disk read/write MB/s |
| `cf` | CPU fan rpm | | `gf` | GPU fan rpm |
| `s1`, `s2` | case fans rpm | | | |
| `mb_sys` | board temp °C | | `mb_vrm` | VRM temp °C |
| `mb_vsoc` | SoC voltage temp | | `mb_chipset` | chipset temp °C |
| `pidle` | seconds since the owner's last input | | | |

Arrays: `fans` and `fan_controls` (≤4 ints each), `hdd` (≤4 entries of
`{n (≤2 chars), u, tot (floats, GB), t (int, °C)}`), `tp` (top CPU processes,
≤3 × `{n, c}` = name + percent), `tr` (top RAM processes, ≤2 × `{n, r}` = name
+ MB).

### `media`
`{art, trk, mp, idle, media_status, ctok, mpos, mdur}` — artist, track,
`mp` = is-playing bool, `idle`, `media_status` (`PLAYING`/`PAUSED`), `ctok` =
cover token (a change triggers a refetch), `mpos`/`mdur` = position and length
in seconds. The cover image itself is fetched separately from the control panel
as RGB565.

### Alert takeover (top level)
`alert` (`"CRITICAL"` arms it), `target_screen` (`MAIN`/`CPU`/`GPU`/`RAM`/
`DISKS`/`MEDIA`/`FANS`/`MOTHERBOARD`), `alert_metric`
(`ct`/`gt`/`cl`/`gl`/`gv`/`ram`, names the banner).

### `notif`
`{seq, app, title, body, pending, dur, led}` — Windows toast mirrored to the
device's notification card. **Capped** at app≤24 / title≤48 / body≤160 chars:
an uncapped body pushed the frame past `NOCT_TCP_LINE_MAX`.

### `rc` — remote control (the device acts once per `seq` change)
Every field is optional; the sentinel means "no change this time".

| key | type | sentinel | effect |
|---|---|---|---|
| `seq` | int | — | **required**; a change is what arms the whole block |
| `screen` | int | -1 | jump to scene N |
| `say` | string | "" | make the wolf speak this line verbatim |
| `theme` | int | -1 | theme preset index |
| `bright` | int | -1 | backlight, clamped to 30..210 |
| `led` | 0/1 | -1 | mood LED on/off |
| `ledmode` | 0..3 | -1 | idle LED style: mood / off / rainbow / candle |
| `carousel` | int | -2 | -1 = off, else interval seconds |
| `petllm` | 0/1 | -1 | let the wolf use the LLM |
| `wchat` | 0..3 | -1 | chattiness: off / rare / normal / often |
| `wtone` | 0..3 | -1 | character: normal / kind / grumpy / cheeky |
| `flip` | 0/1 | -1 | rotate the panel 180° |
| `timeout` | int | -1 | screen-dim seconds, 0 = off |
| `bgstyle` | 0..2 | -1 | background: solid / animated / grid |
| `bglight` | 0/1 | -1 | light background |
| `notif` | 0/1 | -1 | show PC notification flyovers |
| `scenemask` | int | -1 | bitmask of scenes in the ring (bit 0 forced on) |
| `uielem` | int | -1 | bitmask of optional widget classes |
| `action` | string | "" | `feed` / `play` / `pet` / `talk` |
| `chrome` | [r,g,b] | absent | override the chrome hue |
| `accent` | [r,g,b] | absent | override the accent hue |
| `color` | [role,r,g,b] | absent | set one palette role (0..9) |
| `palette` | 10 × [r,g,b] | absent | set the whole palette |
| `resetcustom` | 1 | -1 | drop the custom palette, back to the preset |
| `pin` | int | -2 | pinned "home" scene, -1 = the den |
| `slot` | 0..2 | -1 | switch the active theme slot |
| `night` | 0/1 | -1 | quiet hours on/off |
| `nightfrom`, `nightto` | 0..23 | -1 | quiet-hour bounds (wraps past midnight) |
| `zbalert` | 0/1 | -1 | climate alerts on the ForestHome sensor |
| `zbtmin`, `zbtmax` | °C | -1000 | temperature band; `-99`/`99` disable that side |
| `zbhmin`, `zbhmax` | % | -1000 | humidity band; `-1`/`101` disable that side |
| `zbbat` | % | -1000 | warn below this battery level; `0` = never |
| `zbjoin` | sec | -1 | **one-shot**: open the network for joining |
| `zbpoll` | 1 | -1 | **one-shot**: read the sensor's attributes now |
| `zbint` | sec | -1 | ask the sensor to report at least this often |
| `zbdump` | days | -1 | **one-shot**: upload that many days of the climate archive |
| `pcwake` | 0/1 | -1 | presence at the desk may wake the PC. A SETTING, not a one-shot: it must survive a board reboot |

> **On cadence:** an Aqara WSDCGQ11LM decides for itself — it reports on change
> (~±0.5 °C, ±6 % RH) plus a keep-alive roughly every 50-60 minutes, and that
> rhythm is what makes a CR2032 last two years. It listens only just after it
> transmits, so a poll at an arbitrary moment usually reaches nobody.
>
> `zbjoin`/`zbpoll` are events, not settings: the panel drops them from a
> merged command rather than re-running them on the next unrelated click.
> `zbpoll` is a *request* — an Aqara is a sleepy end device and answers on its
> own schedule — and `zbint` is a request too: Xiaomi firmware is known to
> ignore configure-reporting. The panel says both out loud rather than
> pretending they are settings.

> The climate keys use **-1000** for "not in this command", not -1: every
> ordinary value here — including 0 and negatives — is a legitimate threshold,
> so the usual sentinel would have made "warn me below 0 °C" unsayable.
> Alerts fire on the EDGE with a degree of hysteresis, and a stale sensor never
> clears one.
| `ota` | string | — | **removed in v1.14.0** — the Zigbee build has one app partition and no OTA slots; the key is ignored |

> **`ota` is the one field that can execute code.** Telemetry is unauthenticated
> plain TCP on the LAN, so the firmware does not take the URL on faith:
> `OtaManager::urlAllowed()` accepts only the configured telemetry host
> (`PC_IP`) or an RFC1918/loopback literal, and rejects any authority containing
> `@`. Anything else is refused and surfaced as a toast.

## `claude` — emitted by BOTH producers, and they carry different halves

| key | who emits it | meaning |
|---|---|---|
| `win` / `wk` | lite (real), PC server (relayed from lite) | REAL 5h / 7d utilization % from Anthropic's `anthropic-ratelimit-unified-*` headers |
| `rst` / `wrst` | same | minutes until the 5h / 7d window resets |
| `plan` | same | subscription label, e.g. `max 5x` (tier multiplier folded in from `rateLimitTier`) — badge on the scene |
| `tok` / `msg` / `tool` / `day` | **PC server only** | Claude Code transcript counters, read from `~/.claude` on the PC |
| `ok` / `stale` / `src` | both | `src` is `live` (real API) / `sessions` / `stats-cache` |

Only the machine holding a fresh Claude OAuth token can produce the real
percentages. That is **forestserver** (`monitoring/tgbot/claude_meter.py`), which
publishes them into its state file; `nocturne-lite` republishes them and the PC
server relays them via `claude_remote_url`. Never copy the credential to a second
machine — two refreshers race on refresh-token rotation.

> **The firmware MERGES this block, it does not replace it** — a key that is
> missing *or* `null` leaves the previous value alone. That is what lets one
> producer supply percentages and the other supply counters. A producer that
> wants to say "unknown" must therefore omit the key; sending `0` asserts zero.

## Oversize frames

A line longer than `NOCT_TCP_LINE_MAX` (4096) cannot be parsed. The PC server
(`encode_payload`) now sheds optional blocks in increasing order of value —
`notif`, `tp`, `tr`, `wf`, `fan_controls`, `fans`, `hdd`, `events` — until the
frame fits; `claude`, `forest`, `svc` and the hw scalars are never dropped. If
even that is not enough it sends **nothing**, so the device keeps its last good
state. (It used to replace the whole payload with `{"ct":0,…}`, which both lost
the two most valuable blocks and overwrote live hardware readings with zeros.)

## Device → server (uplink)
- `HELO`, `screen:N`, `cmd:claude|status`
- `wolf:` — pet stats (hunger/joy/energy/mood/alive/sleeping/age)
- `zbs:` — one line per locally-paired Zigbee sensor, once a minute:
  `zbs:name,temp10,humidity,battery,age_sec,pressure_hpa` (temp ×10; -1 =
  unknown; names come from `nocturne.ini` and must not contain commas). This is how the server —
  and through it a Yandex Smart Home skill — learns what the coordinator hears.
- `zbs:` gained a sixth field in v1.16: `…,age_sec,pressure_hpa` (-1 = the
  sensor has no barometer). The Aqara WSDCGQ11LM has one; the WSDCGQ01LM does
  not, so consumers must treat it as optional rather than assume zero.
- `zbs:` gained a seventh and eighth in v1.23: `…,lux,motion_age_sec`, both -1
  when the device does not measure that. Full line:
  `zbs:name,temp10,humidity,battery,age_sec,pressure_hpa,lux,motion_age_sec`.

  **Parse this line from the RIGHT.** The trailing fields are all integers and
  the name is whatever precedes them, so a consumer should peel integers off
  the end rather than split on a fixed count. The server did the latter and
  broke silently the moment the line grew: `rsplit(",", 5)` against an
  eight-field line returned the name as `ForestHome,230,56` and the
  temperature as the battery, with no error anywhere. A fixed field count is
  a promise between two repositories that nothing checks at runtime.
- `cmd:wake` — presence at the desk, and the board has decided the PC is
  AWAKE (the TCP link is up) so the thing that is dark is the monitor. Only
  something running on the PC can wake a monitor: by then the machine is
  running, the NIC never slept, and there is no packet that means "turn the
  backlight on". The server answers with a zero-delta SendInput, which counts
  as user input for the display-idle timer and moves the pointer by nothing.
  When the link is DOWN the board sends a Wake-on-LAN magic packet instead
  and this line never appears — the two cases are exclusive by construction.
- `zbs:` carries ONLY sensors this coordinator heard on its OWN radio.
  A server-relayed or console-injected sensor is never echoed back up. The
  line means "what the board hears", the server stores it as exactly that,
  and a relayed entry re-reported as local makes the board launder someone
  else's data as its own observation - which then sits in the panel as a
  device that does not exist and cannot be removed for two hours. The board
  tracks this as `ZigbeeData::localCount`, recorded BEFORE the relayed merge
  because afterwards the two are indistinguishable.
- `zbw:` — every analysis window at once, whenever the board recomputes
  (every 5 min):
  `zbw:dew10,pressPct,dP1,ok1,dP3,ok3,dP6,ok6,dP12,ok12,dP24,ok24,dT1,dT24`
  and, APPENDED in v1.26 (fields 15..27, never inserted, so a consumer that
  splits and ignores the tail keeps working):
  `absHum10,tempPct,humPct,dT3,okT3,dT6,okT6,dH1,okH1,dH3,okH3,dH24,okH24`.

  The room's own readings had no windows on this line at all — humidity none,
  temperature only 1 h and 24 h — so everything a sensor in the middle of the
  room could say about the room was computed and then dropped.

  `absHum10` is tenths of a gram of water per cubic metre, -9999 when it
  cannot be computed. It exists because RELATIVE HUMIDITY IS NOT A MEASURE OF
  WATER: it is a ratio against a capacity that roughly doubles every ten
  degrees, so it falls when the heating comes on and nothing has dried. A
  consumer wanting "did something happen in this room" must read this, not
  RH. Humidity deltas (`dH*`) are whole percent, not tenths.
  Deltas are TENTHS (of hPa, of degrees). `dew10` is the Magnus dew point,
  -9999 when it cannot be computed. `pressPct` is where the current pressure
  sits in the board's OWN recorded distribution, -1 when the archive is too
  short — an absolute level is not exported at all, because the sensor
  reports station pressure and reducing it to sea level needs an elevation
  nobody has entered.

  **Every window carries its own ok flag and a consumer must honour it.**
  "No history that old on the card yet" and "no change over that window" are
  different answers; treating the first as zero is how a chart ends up
  drawing a confident flat line across a gap it never had data for.

- `zbpat:` — one matched pattern, sent immediately after `zbw:`:
  `zbpat:id,severity,title|detail`. `id` is the `analysis::PatternId` enum,
  `severity` is 0 worth knowing / 1 worth a glance / 2 worth interrupting
  for. id and severity come first so a consumer can route on them without
  parsing the Russian text, which is written for humans only. A fresh `zbw:`
  invalidates every `zbpat:` before it: the pair is one atomic report.

- `zbcsvb:` / `zbcsv:` / `zbcsve:` — the climate archive, streamed on request
  (`zbdump`). Begin carries the day count, then one row per reading as
  `zbcsv:YYYY-MM-DD,HH:MM,temp_c,rh,bat,press_hpa`, then
  `zbcsve:rows,complete`.

  The board paces this at about 20 rows per 100 ms; it is not a blocking
  dump. `complete` is 0 when the walk was abandoned, and a consumer **must**
  discard an incomplete transfer rather than merge it — a truncated archive
  presented as whole is the kind of thing a forecast gets built on and then
  quietly disbelieved.

  Rows may repeat a timestamp: the sensor reports several attributes
  separately and each writes a full snapshot, so roughly 40% of rows share a
  minute with another. Merge by timestamp, newest wins. Validate the shape
  of the date and time fields before trusting a row — a TCP line can arrive
  torn, and a corrupt record inside an archive is a data point that never
  existed.

  **The card is the source of truth, this is a mirror.** The board writes a
  reading whether or not anything is listening; a consumer hears it only
  while it is connected. So a night with the PC off leaves a hole in the
  mirror and none on the card, and the repair is always to re-pull, never to
  reconstruct from whatever arrived live.

- `brd:` — the board's own vitals, every 15 s:
  `brd:temp,temp_max,load,fps,heap_free_kb,heap_min_kb,heap_largest_kb,
  uptime_s,cpu_mhz,rssi,boots,faults,reason`. The device watched a PC all day
  and reported nothing about ITSELF — which means you only find out about it
  when it stops. `load` is the share of the frame period the render loop
  actually spent working: an Arduino sketch has no scheduler accounting to ask,
  so this is the only figure that can be measured rather than invented, and it
  is labelled as loop duty, not CPU. `heap_min` matters more than `heap_free` —
  the low-water mark is what decides whether the next TLS handshake fits.
- `zbtr:` — barometric tendency over 3 h, every 5 min:
  `zbtr:dPress_tenths_hpa,dTemp_tenths_c,dHum_percent`, computed from the card
  archive rather than from the last two reports. Sent separately from the
  readings because **the rate is what forecasts weather; the absolute pressure
  only tells you your altitude.** Three hours is the meteorological standard —
  the tendency METAR reports, and the interval every published threshold is
  quoted against.

  > Pressure is the one reading here that is about the OUTDOORS. A building is
  > not airtight, so indoor pressure tracks the atmosphere within a fraction of
  > a hPa — which is why a barometer indoors forecasts while an indoor
  > thermometer only ever describes the radiator. The temperature and humidity
  > deltas ride along for completeness; do not build weather hints on them.
- `zbst:` — the coordinator's own account of itself, every 15 s:
  `zbst:up,channel,join_left_sec,devices,last_heard_sec` (-1 = never heard).
  A hub that is up with no sensors and a hub that is down look identical from
  outside, and they need different fixes — so the board reports which it is
  rather than leaving the panel to infer it from silence.
- `sd:` — the card's health, once a minute:
  `sd:ok,clock_hz,used_mb,total_mb,writes,slow,fails,queue,last_ms`. The board
  is the only thing that can see its own card, so without this a card that has
  quietly failed forty writes looks exactly like a healthy one until someone
  opens the archive and finds a hole in it.
- `cfg:` — CSV mirror of device settings, so the panel reflects the board.
  **Twenty fields since v1.12** — the panel's key list must match position for
  position, because a short list silently drops the tail (which is exactly what
  happened to `pinned`/`slot`/`night*` until v1.14.1).
  Fields, in order:

  `petllm, wchat, wtone, led, flip, bglight, bright, carousel, timeout,
  bgstyle, theme, uielem, scenemask, notifshow, ledmode, pin, slot, night,
  nightfrom, nightto, zbalert, zbtmin, zbtmax, zbhmin, zbhmax, zbbat,
  zbjoin, zbpoll, zbint`

  `carousel` is -1 when off; `theme` is -1 when a custom palette is active.
  Fields 16-20 were **appended** in v1.9.0 — a panel that splits by index and
  ignores the tail keeps working. Only ever append here, never reorder.

_Last synced with `parsePayload()` at firmware v1.9.0. `tools/check_schema.py` enforces that every key the firmware reads is named here._
