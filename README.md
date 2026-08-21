# 🐺 Nocturne C6 — Wolf Companion

> A Flipper-Zero-style companion device on an **ESP32-C6 + 1.47" colour LCD**:
> live PC-telemetry mirror, an adaptive Forza racing HUD, and **Ноктюрн** — an
> LLM-voiced wolf pet. Wire-compatible with the Nocturne PC monitoring server,
> and fully customisable from the device *or* a companion web panel.

Hardware: **Waveshare ESP32-C6-LCD-1.47** (172×320 ST7789V3, microSD, WS2812,
single BOOT button). Ported from the Heltec ESP32-S3 mono-OLED original.

## Highlights

- **Ноктюрн, the wolf** — a tamagotchi core (hunger / joy / energy, autonomous
  sleep, NVS persistence) that *speaks*. PetBrain watches the pet's stats **and**
  PC telemetry (app launches, alerts, music, load, weather, server events) and
  asks **LM Studio** (`google/gemma-4-e2b`) for short, in-character one-liners —
  naming the actual app / track / alert it reacts to. Offline or mid-game it
  falls back to a ~95-line phrase cache, so it's never silent.
  - **Pet it**: feed / play / **pet** / talk, from the button or the web panel,
    with heart / spark / note particle bursts.
  - **Wolf settings**: chattiness (off → often) and character (normal / kind /
    grumpy / cheeky).
- **Adaptive Forza HUD** — auto-launches on UDP "Data Out" traffic and reshapes
  itself to the situation: **race** (position + lap times + live place-change
  flashes), **drift** (G-force friction circle + lateral-G + drift timer), and
  **free-ride** (cruise stats). Shift lights, gear-roll animation, pedals, slip,
  a session-summary card, and a green/red LED blip on overtake/loss.
  *(Forza's telemetry is ego-only — it carries no opponent gap, so place-changes
  and lap deltas are the faithful substitute for "time to next car".)*
- **16 telemetry scenes** + Forza: ЛОГОВО, ОБЗОР, CPU, GPU, ПАМЯТЬ, ДИСКИ,
  КУЛЕРЫ, ПЛАТА, СЕТЬ, МЕДИА, ПОГОДА, CLAUDE, ЛЕС (nodes), СЕРВИСЫ, СОБЫТИЯ
  (alerts), ИСТОРИЯ (on-device graphs; long-press cycles **last hour → last
  24 h → the card's archive by day**, and the first two survive a reboot).
  Trend carets, reactive
  backgrounds that speed up under load and turn red on alerts.
- **A real archive** — with a card in, every minute is appended to
  `/logs/YYYY-MM-DD.csv`: ~43 KB a day, 16 MB a year against 7.4 GB of card. The
  ИСТОРИЯ screen has a third scale that reads the card back, so a month of days
  is browsable on the device itself. `/logs/boot.jsonl` records every restart
  with its reason, and a panic is copied out of flash to `/logs/coreNN.elf` for
  `esp-coredump` — crashes on a self-healing device happen when nobody is
  watching, by definition.
- **The board notices things.** `daily.csv` keeps the average **idle**
  temperature per day. Load temperature says what you were doing; idle
  temperature says what the cooling can still do, and it creeps up as dust
  collects. After two weeks the board has its own baseline and the wolf will
  tell you, in its own words, that the GPU idles six degrees hotter than it used
  to — months before anything throttles.
- **The wolf keeps a journal.** Once a day it asks the model to write the day up
  in its own voice and files it under `/wolf/journal/`. The newest entry rides in
  every later prompt, so its memory is measured in days rather than in the last
  six things that happened.
- **The card is a two-way channel.** `/nocturne.ini` overrides WiFi, the server
  address and the LM Studio endpoint without a rebuild — take the board
  somewhere else, edit a text file on any laptop, put the card back.
  `/themes/*.thm` add palettes, `/skins/*.wolf` change what the wolf looks like,
  and a `/firmware.bin` in the root is installed at the next boot: the recovery
  path when WiFi is down and USB is far away. See
  [`examples/card/`](examples/card/).
- **Deep customisation** — 12 themes **plus** an on-device colour editor
  (per-element R/G/B), 3 saved theme slots, animated/light backgrounds,
  **screen composition** (which scenes ride the ring) and **element composition**
  (hide sparklines / trend carets / decorations / etc.) — all on-device or web.
  **Quiet hours** dim the panel and kill the LED overnight (own NTP clock, so it
  works with the PC off); a hardware alert still overrides them. A guarded
  factory reset lives in Меню → Система — it clears settings only, never the
  wolf.
- **One button**: short = next scene · double = menu · triple = home (the den)
  · long = contextual action · **hold = fast-scroll** inside lists and the colour
  editor. The menu is grouped into five categories (Экран / Волк / Состав /
  Сигналы / Система) so nothing is more than a few presses deep. Direction-aware
  scene transitions.
- **WS2812 mood light** + **multi-WiFi** (ranks known SSIDs by RSSI).

## Companion web panel 🎛️

Served by the Nocturne PC server (`http://<pc>:8899/`), it drives the device
without touching the button and mirrors its live state — PC clock, temps/loads,
RAM, weather, media, **and the wolf's face/stats reported back from the
device**. Remote pet actions, jump to any screen, the full theme gallery +
colour editor, screen/element composition, wolf settings. Commands ride the
telemetry `rc` block (the device acts once per `seq` change).

## Build & flash

```bash
cp include/secrets.h.example include/secrets.h   # WiFi nets, server host, LM Studio endpoint + token
pio run -e nocturne-c6                # build
pio run -e nocturne-c6 -t upload      # flash (USB-Serial/JTAG)
```

**Over the air** — after the first USB flash you never need the cable again:

```bash
pio run -e nocturne-c6 -t upload --upload-port 192.168.1.42
```

The device's IP is on the **Меню → Система → Инфо системы** screen. Set
`OTA_PASSWORD` in `secrets.h` on any shared network (then add
`--upload-flags --auth=...`). The server can also push an update itself via the
`rc` block's `ota` field; the firmware only accepts an image URL on `PC_IP` or a
private-range host, because telemetry is an unauthenticated LAN channel.

**Tests** (host-side, no board needed):

```bash
pio test -e native && python tools/check_schema.py
```

`check_schema.py` fails if the firmware parses a JSON key that `SCHEMA.md` does
not document — the wire contract used to drift silently. Both run in CI along
with the firmware build and an OTA-headroom check.

> The compiled binary bakes in your `secrets.h`, so the GitHub releases ship
> **source only** — build it yourself. After flashing over USB-JTAG the C6 may
> stay in the bootloader; tap **RST** once.

## Hardware (Waveshare ESP32-C6-LCD-1.47)

| Function | GPIO |
|---|---|
| LCD SCLK / MOSI / CS / DC / RST / BL | 7 / 6 / 14 / 15 / 21 / 22 |
| microSD CS / MISO (shared SPI2 bus) | 4 / 5 |
| WS2812B RGB LED | 8 |
| BOOT button (user input, active LOW) | 9 |

Panel: ST7789V3 172×320, `offset_x=34`, **INVON required**, 80 MHz SPI, **4 MB
flash** (the product page's "FH8" is a typo — it is an ESP32-C6FH4), no PSRAM —
the 110 KB RGB565 framebuffer is allocated first at boot.
Backlight is capped below the panel's thermal cliff. Fonts are u8g2 Cyrillic
subsets; all server/LLM text is glyph-filtered so unsupported characters never
render as tofu boxes.

## Architecture notes

- All SPI (LCD + SD) lives on the main loop task; a separate LLM task does
  network only and hands phrases back over a mailbox.
- Telemetry: TCP `<pc>:8888`, newline-delimited JSON (schema `sv:1.0`), with an
  embedded `rc{}` remote-control block; the device reports pet state upstream
  via a `wolf:` line. Forza UDP on port 5300.
- Partitions: `min_spiffs.csv` — two 1.875 MB OTA app slots (the image is
  ~1.5 MB, ~77%). NVS keeps the offset it had under the old `huge_app.csv`, so
  switching tables does not disturb saved state.
- NVS: pet state (`wolfpet`) is unchanged from the Heltec firmware, so the wolf
  survives the hardware migration; settings live in `nocturne`. The boot counter
  and the last `esp_reset_reason()` live there too, and show up on the СИСТЕМА
  screen — the watchdog reboots this device to heal it, and a silent heal is
  indistinguishable from a reboot loop without them.
- SD layout (see [`examples/card/README.md`](examples/card/README.md) for the
  full map): `/wolf/cache/*.jsonl` (phrase cache, de-duplicated),
  `/wolf/memory.jsonl` (dated diary → prompt memory), `/logs/hist.bin` (graph
  snapshot), `/logs/YYYY-MM-DD.csv` (one telemetry row per minute),
  `/logs/boot.jsonl` (one record per boot), `/covers/*.565` (cached album art).
  Line files **rotate** at their cap, keeping the newest half. The card is
  optional — everything degrades without it.
- The card's clock is **negotiated, not assumed**: 25 → 10 → 4 MHz, each rung
  accepted only after a real write succeeds on a quiet bus. The card here mounts
  at 25 MHz and sometimes still cannot write a byte, so neither "always fast" nor
  "always slow" is right. All SD work runs on the render loop, so queued writes
  drain one per flush tick — a 1.3 KB write costs 30-140 ms of *card* time, which
  no bus speed removes.
