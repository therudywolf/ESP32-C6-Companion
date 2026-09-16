# 🐺 Nocturne C6 — the wolf companion

**English** · [Русский](README.ru.md)

[![CI](https://github.com/therudywolf/ESP32-C6-Companion/actions/workflows/ci.yml/badge.svg)](https://github.com/therudywolf/ESP32-C6-Companion/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/therudywolf/ESP32-C6-Companion?display_name=tag)](https://github.com/therudywolf/ESP32-C6-Companion/releases)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

> A desk companion in the spirit of the Flipper Zero, on an **ESP32-C6 with a
> 1.47" colour screen**: a live mirror of your PC's telemetry, an adaptive
> racing HUD for Forza, and **Nocturne** — a wolf pet that speaks with the
> voice of a language model. Wire-compatible with the Nocturne server and
> configurable end to end, from the board itself or from a web panel.

Hardware: **Waveshare ESP32-C6-LCD-1.47** (172×320 ST7789V3, microSD, WS2812,
one BOOT button). Ported from the original on a Heltec ESP32-S3 with a mono
OLED.

## What it can do

**Nocturne, the wolf.** A tamagotchi core (hunger, joy, energy, sleeping on its
own, saved in NVS) that *talks*. PetBrain looks at both the pet's state and the
PC's telemetry — apps launched, alarms, music, load, weather, server events —
and asks **LM Studio** for a short line in character, naming the actual app,
track or alarm. With no network, or in the middle of a game, it falls back to a
cache of about 95 phrases, so it is never silent. You can feed it, play with
it, pet it and talk to it — from the button or from the panel.

The pet can also be switched off: then DEN becomes the MAIN screen — clock,
room, machine, outside. The kind is yours to pick: wolf, dog, cat, fox — the
same stats, a different skin, name, stages and lines.

**Twenty screens** in a carousel: the machine (CPU, GPU, memory, disks, fans,
board, network), media with cover art, weather with a five-day forecast and a
barometer, Claude usage, node and service health, Alertmanager events, history
from the card, the room, the board itself, a pressure analysis and the racing
HUD for Forza.

**The room is its own.** The board is a Zigbee coordinator: a sensor pairs
straight to it, with no third-party bridge and no cloud. Readings are written
to an archive on the microSD, analysed (pressure trend, dew point, condensation
risk) and mirrored upstream to the server.

**Five looks** at one press: Cyberpunk, Material, Windows, LEDs, Nixie. A look
is a style plus a palette, a background and a dot pass; each of the four can be
adjusted separately afterwards.

## What changed in v1.44

- **Two hubs, one link.** `[server] host2/port2/token2` on the card: the board
  holds a primary hub and a fallback, moves to the fallback after three failed
  connects and probes once a minute to see whether the primary is back. The
  point is not reliability: the link is plain TCP and cannot be encrypted (a
  TLS handshake wants roughly 46 KB of contiguous heap, and the board has
  32–36 KB free with Zigbee up). So the primary hub belongs on the local
  network, where notification text never leaves the house. While the PC sleeps
  there is no private half of the payload at all.
- **The last screen was unreachable.** `SCENE_FORZA` was used as an exclusive
  bound in six places, and the last screen of the ring could not be chosen in
  the menu, turned on from the panel, pinned as home, or enabled by the "the
  ring grew" migration. None of the six crashed — the screen simply was not
  there. `check_panel.py` now looks for that pattern in the firmware.
- **Settings report back at once.** `cfg:` used to ride along with the pet
  report every two seconds, so a control in the panel showed a stale value for
  up to two seconds and could jump back. Measured round trip panel → board →
  report: was 1.79 s, now 0.14 s.

## Build and flash

```bash
cp include/secrets.h.example include/secrets.h   # Wi-Fi networks, hub address, LM Studio endpoint
pio run -e nocturne-c6                           # build
pio run -e nocturne-c6 -t upload                 # flash (USB-Serial/JTAG)
pio device monitor                               # console, 115200
```

`include/secrets.h` never goes into git. Everything in it can be overridden
from the card by `/nocturne.ini` — networks, hub addresses, tokens, sensor
names, the alarm — and then moving to another network needs neither a build nor
a cable.

Environments: `nocturne-c6` is the main one, `nocturne-c6-nozb` is the same
without Zigbee (to find out who is to blame for a Wi-Fi problem),
`nocturne-c6-lint` builds with the screen-layout check, `native` runs the tests
on the host.

## Releases

A tag builds the firmware and attaches it to a release, together with
`firmware.elf` and `SHA256SUMS`. Mind what the attached image is: the real
`secrets.h` is not in git, so CI builds against the example. The binary is
**proof that the tree compiles**, not an image ready to flash — your network,
hub addresses and tokens are not in it. Either build it yourself, or put
everything on the card in `nocturne.ini`, which overrides the compiled values.

## Web panel 🎛️

The panel is served by the Nocturne server (`http://<host>:8899/`) — usually
the PC, on the local network, for the same privacy reason. It drives the board
without the button and mirrors its state: the PC's clock, temperatures and
loads, memory, weather, media, and the wolf's face with the stats the board
reports back. Commands travel in the `rc` block of the telemetry, and the board
runs them once per change of sequence number.

The board also has **its own** panel on port 80 (`http://<board address>/`),
which works with the PC off: everyday settings and the pet's actions.

## Documentation

- [SCHEMA.md](SCHEMA.md) — the payload format and every protocol field.
- [docs/SETTINGS-MAP.md](docs/SETTINGS-MAP.md) — every setting: its path in the
  menu, its `rc` key, its NVS key, its index in `cfg:`, its default.
- The server lives in a separate repository,
  [NocturneServer](https://github.com/therudywolf/NocturneServer).

## What does not go into git

`include/secrets.h`, the card's `/nocturne.ini`, archives and logs from the
microSD. The repository holds only what is the same for everyone; everything
belonging to one particular board and one particular home stays with its owner.
