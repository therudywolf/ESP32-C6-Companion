# What goes on the card

The microSD is optional — everything degrades without it — but with one in, the
board stops being limited to what was compiled into it.

Copy `nocturne.ini` to the card root and `themes/1.thm` to `/themes/1.thm`. The
firmware creates the rest itself on first boot.

## What you put there

| path | what it does |
|---|---|
| `/nocturne.ini` | WiFi, server address, LM Studio endpoint, wolf skin — overrides `secrets.h` key by key |
| `/themes/1.thm` … `8.thm` | extra palettes, appended after the built-in presets |
| `/skins/<name>.wolf` | 512 raw bytes: four 128-byte 32×32 XBM frames (idle, blink, aggressive, funny) |

## What the board writes there

| path | what it is |
|---|---|
| `/logs/YYYY-MM-DD.csv` | one telemetry row a minute — ~43 KB a day |
| `/logs/daily.csv` | one row a day, including **idle** CPU/GPU temperature |
| `/logs/zb.csv` | one row per Zigbee sensor report: `uptime_s,name,temp_c,rh,bat` |
| `/logs/zb.bin` | last readings, so a reboot does not blank ДОМ for an hour |
| `/logs/boot.jsonl` | one record per boot: reason, counters, free heap |
| `/logs/coreNN.elf` | a crash dump copied out of flash after a panic (see below) |
| `/logs/hist.bin`, `/logs/today.bin` | graph and rollup state, so a reboot costs nothing |
| `/logs/media.csv` | every track played |
| `/forza/laps.csv`, `/forza/best.txt` | lap times and the personal best |
| `/wolf/cache/*.jsonl` | phrases the wolf learned, de-duplicated |
| `/wolf/memory.jsonl` | the dated diary that feeds its prompt |
| `/wolf/journal/*.txt` | one entry a day, written by the wolf itself |
| `/covers/*.565` | cached album art |
| `/shots/NNN.bmp` | screenshots (Меню → Система → Снимок экрана) |

## Decoding a crash

`/logs/coreNN.elf` is a standard ESP-IDF core dump. With the matching build:

```bash
esp-coredump info_corefile -c /path/to/core01.elf .pio/build/nocturne-c6/firmware.elf
```

The board copies the dump off flash and erases it, so each crash is kept exactly
once and the next one has somewhere to go.

## Why idle temperature is the interesting column

`daily.csv` records the average CPU and GPU temperature over the minutes when
the machine was doing **nothing**. Load temperature tells you what the owner was
doing; idle temperature tells you what the cooling can still do, and it drifts
upwards as dust collects — months before anything throttles. After a couple of
weeks the board has its own baseline and will say so out loud, in the wolf's
voice, when today is more than 5 °C above it.
