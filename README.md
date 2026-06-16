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
  (alerts), ИСТОРИЯ (on-device last-hour graphs). Trend carets, reactive
  backgrounds that speed up under load and turn red on alerts.
- **Deep customisation** — 12 themes **plus** an on-device colour editor
  (per-element R/G/B), 3 saved theme slots, animated/light backgrounds,
  **screen composition** (which scenes ride the ring) and **element composition**
  (hide sparklines / trend carets / decorations / etc.) — all on-device or web.
- **One button**: short = next scene · double = menu · triple = home (the den) ·
  long = contextual action. Direction-aware scene transitions.
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

Panel: ST7789V3 172×320, `offset_x=34`, **INVON required**, 80 MHz SPI, 8 MB
flash, no PSRAM — the 110 KB RGB565 framebuffer is allocated first at boot.
Backlight is capped below the panel's thermal cliff. Fonts are u8g2 Cyrillic
subsets; all server/LLM text is glyph-filtered so unsupported characters never
render as tofu boxes.

## Architecture notes

- All SPI (LCD + SD) lives on the main loop task; a separate LLM task does
  network only and hands phrases back over a mailbox.
- Telemetry: TCP `<pc>:8888`, newline-delimited JSON (schema `sv:1.0`), with an
  embedded `rc{}` remote-control block; the device reports pet state upstream
  via a `wolf:` line. Forza UDP on port 5300.
- NVS: pet state (`wolfpet`) is unchanged from the Heltec firmware, so the wolf
  survives the hardware migration; settings live in `nocturne`.
- SD layout: `/wolf/cache/*.jsonl` (phrase cache), `/wolf/memory.jsonl` (diary →
  prompt memory), `/logs/`. The card is optional — everything degrades without it.
