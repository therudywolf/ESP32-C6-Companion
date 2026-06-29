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
| `id` | string | ≤? | stable node id |
| `name` | string | | display name (Latin-safe; some node-name fonts are Latin-only) |
| `st` | string | | `up` / `down` |
| `cpu`,`ram`,`disk` | int | | percent, `-1` = unknown |
| `extra` | string | ≤16 | free badge text |

### `svc` — `{n, up, list:[…]}`
entry: `{name (≤18), st (up/down), ms (int, -1 = unknown)}`

### `dock` — `{n, up}`
container counts (lite may omit → device shows 0).

### `src`
producer tag, e.g. `forest-lite`; lite appends `!wx`/`!forest`/`!svc` when a
block failed to populate. Ignored by the firmware, useful in logs.

## Blocks only the **PC server** emits (no lite equivalent)

- **HW**: `ct gt cl gl ru ra …` (CPU/GPU temps & loads, RAM, fans, disks, net) —
  see `payload.py`; the device's CPU/GPU/… scenes show no-signal without them.
- **`claude`**: Claude usage window (`available`, window %, weekly %, resets,
  tokens, `stale`, …) — `drawClaude`.
- **`media`**: `{art, trk, play, idle, media_status, pos, dur, cover_tok}`; the
  cover image is fetched separately from the control panel as RGB565.
- **`pidle`**: seconds since the owner's last input.
- **`rc`**: remote-control block `{seq, screen, theme, bright, led, …}` — the
  device acts once per `seq` change.
- **`notif`**: `{seq, app, title, body, pending, dur, led}` — Windows toast
  mirrored to the device's notification card.

## Device → server (uplink)
- `HELO`, `screen:N`, `cmd:claude|status`
- `wolf:` — pet stats (hunger/joy/energy/mood/alive/sleeping/age)
- `cfg:` — 15-field CSV mirror of device settings (panel reflects the board)

_Last synced with `parsePayload()` at firmware v1.8.4._
