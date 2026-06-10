# 🐺 Nocturne C6 — Wolf Companion

> ESP32-C6 + 1.47" color LCD companion for the Nocturne PC monitoring stack.
> Flipper-Zero-style UI, LLM-powered wolf pet, wire-compatible with the
> existing Nocturne server.

Port of [Nocturne OS PC Companion](../Heltec-V3-V4-PC-COMPANION) from
Heltec ESP32-S3 (128×64 mono OLED) to the
**Waveshare ESP32-C6-LCD-1.47** (172×320 ST7789, microSD, WS2812, BOOT button).

## Companion control center 🎛️

A web app served by the Nocturne PC server (`http://<pc>:8899/`) drives the
device with no button presses and mirrors its live state:
- live readout: PC clock, CPU/GPU temps+load, RAM, weather, media, **and the
  wolf's stats/face reported back from the device**
- pet the wolf remotely: feed / play / talk / custom speech
- jump to any of the 16 screens
- **12 colour themes** with real palette swatches + custom chrome & accent
- every setting: brightness, LED, wolf-LLM, 180° flip, carousel, dim timeout

Commands ride the telemetry `rc` block (device acts once per `seq` change);
the device reports pet state via an inbound `wolf:` line.

## Highlights

- **Flipper Den UI** — orange-on-black chrome, dithered fills, chunky-pixel
  fonts (Cyrillic capable), 30 fps animations on a full RGB565 framebuffer.
- **Ноктюрн the wolf** — the classic tamagotchi core (hunger/joy/energy,
  autonomous sleep, NVS persistence) now *speaks*: PetBrain watches stats and
  PC telemetry for trigger events and asks **LM Studio**
  (`google/gemma-4-e2b`, `reasoning_effort:none`) for one-liner reactions.
  Offline → phrase cache on SD → flash fallback table. Never silent.
- **15 scenes**: ЛОГОВО, ОБЗОР, CPU, GPU, ПАМЯТЬ, ДИСКИ, КУЛЕРЫ, ПЛАТА,
  СЕТЬ, МЕДИА (cassette deck), ПОГОДА, CLAUDE, ЛЕС, СЕРВИСЫ, СОБЫТИЯ.
- **One button**: short = next scene · long = contextual action ·
  double = menu · triple = home (the den).
- **Alert takeover**: server `alert:"CRITICAL"` hijacks the screen with a red
  strobe frame + aggressive wolf; long press snoozes 60 s.
- **WS2812 mood light**: breathing orange idle, cyan pulse while the wolf
  thinks, red strobe on alert.
- **Multi-WiFi**: scans, ranks known SSIDs by RSSI, walks the ranking.

## Build & flash

```bash
pio run               # build
pio run -t upload     # flash (COM8, native USB-Serial/JTAG)
pio device monitor
```

Copy `include/secrets.h.example` → `include/secrets.h` (WiFi, server IP,
LM Studio endpoint + API token).

## Hardware (Waveshare ESP32-C6-LCD-1.47)

| Function | GPIO |
|---|---|
| LCD SCLK / MOSI / CS / DC / RST / BL | 7 / 6 / 14 / 15 / 21 / 22 |
| microSD CS / MISO (shared SPI2 bus) | 4 / 5 |
| WS2812B RGB LED | 8 |
| BOOT button (user input, active LOW) | 9 |

Panel: ST7789V3 172×320, `offset_x=34`, **INVON required**, 80 MHz SPI.
No PSRAM — the 110 KB framebuffer is allocated first at boot.

## Architecture notes

- All SPI (LCD + SD) lives on the main loop task; the LLM task does network
  only and hands phrases over a mailbox.
- Telemetry: TCP `PC_IP:8888`, newline-delimited JSON (schema sv 1.0),
  `HELO` / `screen:N` / `cmd:claude` / `cmd:status` upstream.
- Pet state keys in NVS (`wolfpet`: h/j/e/age/alive/slp) are unchanged from
  the Heltec firmware — the wolf survives the hardware migration.
- SD layout: `/wolf/cache/*.jsonl` (phrase cache), `/wolf/memory.jsonl`
  (diary → prompt memory), `/logs/`.

## Roadmap

- RGB565 sprite sheets on SD (64×64 wolf frames) via `tools/png2rgb565.py`
- Forza UDP telemetry scene (port exists in the old repo)
- OTA (needs partition rework on 4 MB flash)
