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
- **17 telemetry scenes** + Forza: ЛОГОВО, ОБЗОР, CPU, GPU, ПАМЯТЬ, ДИСКИ,
  КУЛЕРЫ, ПЛАТА, СЕТЬ, МЕДИА, ПОГОДА, CLAUDE, ЛЕС (nodes), СЕРВИСЫ, СОБЫТИЯ
  (alerts), ИСТОРИЯ (on-device graphs; long-press cycles **last hour → last
  24 h → the card's archive by day**, and the first two survive a reboot),
  **ДОМ** (the climate dashboard — see below).
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
- **WS2812 mood light** + **multi-WiFi** (ranks known SSIDs by RSSI). While
  music plays the LED wears the **dominant colour of the album art** (chroma-
  weighted, because the plain average of a cover is always mud).
- **It knows its own temperature.** `temperatureRead()` on the C6 die, measured
  at 49 °C steady on this board — so the backlight cap stopped being a guess made
  once at room temperature and became a guard that reacts. Shown on СИСТЕМА.
- **The wolf grows up.** A pup is brighter and more restless, an adult is the
  wolf you know, an elder greys toward the text colour and blinks slowly. Its
  age and stage go into every prompt, so it talks like what it is.
- **It knows what time it is.** The NTP clock had been there since 1.7.7 and was
  never used to say anything — which is odd for something that watches you work
  at three in the morning. Now it comments, once a night, and greets you in the
  morning.
- **Weather on the glass.** Rain streaks or drifting snow behind the content,
  driven by the real WMO code already in the payload.
- **Достижения** (Меню → Волк) — meals, laps, tracks, days survived, faints
  lived through, each with milestones. Kept in their own NVS namespace, so a
  factory reset cannot erase a month of living with the thing.
- **A game.** One button means press-to-jump, so: the wolf runs, fences come at
  it, the high score is on the board for good. Меню → Волк → Игра.
- **An alarm clock**, because the board has a clock, a light and something that
  talks — `[alarm] at = 07:30` in `nocturne.ini`.
- **The board is a Zigbee hub.** A real 802.15.4 coordinator on channel 25,
  living alongside WiFi on one shared radio — which took four genuine bug fixes
  to make true: an lwIP DNS thread-safety bug in arduino-esp32's `hostByName`
  (patched via linker wrap), uplink writes that could hang the render loop when
  RX stalled (non-blocking now), `esp_coex_wifi_i154_enable()` that **nobody in
  the whole stack ever calls** (without it 15.4 squats on the antenna and WiFi
  receives nothing), and an RF-calibration quirk where the boot after a Zigbee
  session cannot associate (cured by an automatic sub-second erase-and-restart).
  Pair a sensor via **Меню → Система → Подключить датчик** or `zb join`;
  temperature, humidity and Aqara's proprietary battery TLV are parsed, and the
  readings go upstream as `zbs:` lines — the server, and through it a Yandex
  Smart Home skill, hears what the coordinator hears. The network calls itself
  **ForestHome**, which is also what the first sensor is named until
  `nocturne.ini` says otherwise.
- **The sensor is managed from the panel** — pair it, poll it, ask for a
  reporting cadence, and check the link. The check reports the *coordinator's
  own* answer (up / channel / devices / seconds since the last report), because
  a hub that is up with no sensors and a hub that is down look identical from
  outside and need different fixes. Poll and interval are labelled as
  **requests**, not settings: an Aqara sleeps between check-ins and Xiaomi
  firmware is known to ignore configure-reporting, and a button that quietly
  does nothing is worse than one that says what it can promise.
- **Useful with the PC switched off.** The board is a companion, not a
  terminal: with the server dark it still has a wolf, a paired climate sensor,
  its own vitals and an archive on the card. The nav ring now **skips** the
  screens that need the PC instead of walking you through fourteen identical
  blanks, and the ones you can still reach say when the numbers behind them
  froze — "последние данные 3 ч назад" — plus where there is something live to
  look at. A fallback endpoint feeding real data counts as *not* offline, so
  nothing is hidden while it covers.
- **ПЛАТА C6 — the board's own screen.** Die temperature with its 32-second
  trend and the peak since boot, heap free against the lowest ever seen and the
  largest contiguous block, the render loop's duty cycle with the frame rate
  actually achieved, uptime and the restart reason with its counters. All of it
  existed only in the serial log or a menu overlay before, which meant nobody
  ever saw it. The same numbers go upstream as `brd:` and get a card in the web
  panel.
- **The room keeps a record.** Every reading lands in
  `/climate/YYYY-MM-DD.csv` — `time,temp_c,rh,bat,press_hpa`, wall clock, one
  row per report. (The first version stamped rows with *uptime*, which cannot
  order two readings from either side of a reboot and so was not history at
  all.) A long press on ДОМ cycles the trends between the last reports held in
  RAM, **today** and **the week**, both read back from those files. A bucket no
  reading fell into leaves a gap in the line rather than a straight segment
  across six silent hours, because that segment would be a claim the sensor
  never made.
- **A barometer that forecasts.** The WSDCGQ11LM's pressure is the only reading
  on this device that is about the world outside — a building leaks, so the
  needle tracks the atmosphere. The board reads its own card archive for the
  **3-hour tendency** (the interval METAR reports and every published threshold
  is quoted against), classifies it on the WMO bands, and says what to expect:
  falling means a low approaching — cloud, wind, rain; rising means a high
  building — clearing, and colder nights because clear skies radiate heat away.
  A fast fall also carries the note that this is the change weather-sensitive
  people report noticing; it states what the pressure did and does not diagnose
  anyone. Announced on a change of state with six hours of quiet after, because
  a front is news once, not every five minutes for three hours.
- **Climate alerts.** Thresholds set from the web panel — too warm, too cold,
  too damp, too dry, battery running out. They fire on the **edge**, not on the
  fact of being out of range, with a degree of hysteresis so a room sitting on
  the line does not announce itself every report. A stale sensor never clears
  an alert: "no reading" and "the room is fine" are different facts. The
  battery warning nags once a day instead, because a coin cell moves a few
  percent a week and a dismissed one-shot means the sensor dies quietly three
  weeks later.
- **ДОМ, the climate screen.** The ПОГОДА tile answers "and it's 23 inside";
  this answers what the house has been *doing*. One sensor gets the hero
  layout — big temperature, humidity with a comfort verdict, a battery bar, and
  two sparklines reaching back most of a day (32 samples of a device that speaks
  every 20–60 minutes). Built for **one** sensor — the Aqara WSDCGQ11LM this
  runs against, whose barometer also gets a line in mmHg. **Zero** sensors gets
  the pairing instructions and a live countdown of the join window, because a
  screen that is simply blank until you guess the right menu item teaches
  nothing. Locally paired sensors and ones relayed by a server are merged into
  one list, deduplicated by name — neither source silently wins. The measured cost: +380 KB flash and
  the OTA slots (**updates are by cable now**) plus ~55 KB RAM, guarded so a
  TLS fetch skips rather than OOMs. A reading older than an hour dims the whole
  tile, because battery sensors go quiet and a stale number presented as current
  is the same lie "no signal" exists to prevent. (`nocturne-c6-nozb` builds
  without the stack and takes its `zb` block from a server instead.)
- **A USB console.** `help`, `info`, `ls`, `cat`, `ach`, `say`, `eat`, `shot`,
  `theme`, `bright`, `feed <json>`, `zb [join|reset]`, `mem`, `probe`, `phy`,
  `dump <path>`, `reboot` on the same serial port as the logs. `dump` base64s a
  card file out — which is how the screenshots in this README were taken, with
  no card reader and no camera: `shot` then `dump /shots/001.bmp`.
  `feed` pushes a raw payload through the real parser, so a producer for this
  schema can be developed against the board with no server in the middle. Most of a
  debugging session is asking the board questions it could simply answer.

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

**No OTA in the Zigbee build.** The coordinator stack needed a single 3.62 MB
app partition, so the two OTA slots are gone and updates go over
USB-Serial/JTAG. A measured, deliberate trade — a bare WiFi + coordinator
sketch already overflows Espressif's own Zigbee OTA layout.

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
