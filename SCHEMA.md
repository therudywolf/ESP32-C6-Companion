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

entry: `{name, t, h, b, age}`
| key | type | size | meaning |
|---|---|---|---|
| `name` | string | ≤16 | room / sensor label, shown on the ПОГОДА tile |
| `t` | int | | temperature **×10** (23.6 °C → `236`); omit when unknown |
| `h` | int | | relative humidity %, -1 = unknown |
| `b` | int | | battery %, -1 = unknown; under 20 the bar turns red |
| `age` | int | | seconds since the sensor last reported, -1 = unknown |

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
  `zbs:name,temp10,humidity,battery,age_sec` (temp ×10; -1 = unknown; names come
  from `nocturne.ini` and must not contain commas). This is how the server —
  and through it a Yandex Smart Home skill — learns what the coordinator hears.
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
  nightfrom, nightto`

  `carousel` is -1 when off; `theme` is -1 when a custom palette is active.
  Fields 16-20 were **appended** in v1.9.0 — a panel that splits by index and
  ignores the tail keeps working. Only ever append here, never reorder.

_Last synced with `parsePayload()` at firmware v1.9.0. `tools/check_schema.py` enforces that every key the firmware reads is named here._
