# Settings map (v1.44.1)

**English** · [Русский](SETTINGS-MAP.ru.md)

One setting, one row: where you turn it, what it is called on the wire and in
NVS, what it defaults to. Settings live in five places:

| place | how to get there | what it can do |
|---|---|---|
| **board menu** | double-press → category → long press | everything except the climate thresholds and the panel tone |
| **the board's own web panel** | `http://<board IP>/` (the address is on the C6 BOARD screen and in the boot log) | works **with the PC off**; everything day-to-day plus the pet actions |
| **the hub's panel** | `http://<hub>:8899/` | everything, plus per-role colours, glass tone, per-screen carousel, climate, the archive |
| **the card, `/nocturne.ini`** | a text file on the SD card | the things that are identity rather than settings: networks, hub addresses, pet name, skin, alarm |
| **USB console** | `help` | pinpoint debugging: `look 3`, `kind 2`, `style 4`, `pet 0`, `theme 8`, `dots 1`; `server <host> [port] [token]` is the primary hub, `server2 <host> [port] [token]` the fallback (`server2 -` drops it). Both are written into `[server]` on the card and applied at once; a token left out is kept, `-` erases it |

The board menu has five categories: **Look · Screen · Pet · Signals · System**.
The `rc` key is what the panel sends; `cfg[N]` is the position in the `cfg:`
line the board mirrors back (numbered from 0).

## Look

| setting | menu | web | rc | NVS | cfg | values / default |
|---|---|---|---|---|---|---|
| Look (preset) | Look → Look | both: buttons | `look` (one-shot) | — | — | 0 Cyberpunk · 1 Material · 2 Windows · 3 LEDs · 4 Nixie. Writes style+palette+background+matrix in one go |
| Style | Look → Style | both | `style` | `style` | 33 | 0 outlines · **1 surfaces** · 2 flat tiles · 3 scoreboard · 4 nixie (glowing digits) |
| Palette | Look → Palette | both | `theme` | `theme` | 10 | 0..28 built in, up to 8 more from the card (`/themes/N.thm`); −1 = your own |
| Background | Look → Background | both | `bgstyle` | `bgStyle` | 9 | 0 plain · **1 animated** · 2 grid |
| Matrix | Look → Matrix | both | `dots` | `dots` | 29 | **0 off** · 1 dots (step 3) · 2 sparse (step 4). The LEDs style turns it on by itself |
| Light background | Look → Light background | both | `bglight` | `bgLight` | 5 | **0** / 1 |
| Palette slot | Look → Palette slot | hub | `slot` | `aslot`, `slot0..2` | 16 | 0..2 |
| Colours by hand | Look → Colours by hand (editor) | hub: by role | `color`, `palette`, `chrome`, `accent`, `resetcustom` | `custom`, `customOn` | — | 10 RGB roles |
| Glass tone | — | hub | `toner/toneg/toneb/tonek` | not stored | — | gain 30..300 %, black 0..96; the panel replays it after a reboot |
| Mono review | — | hub | `mono`, `review` | not stored | — | for comparing screenshots |

## Screen

| setting | menu | web | rc | NVS | cfg | values / default |
|---|---|---|---|---|---|---|
| Brightness | Screen → Brightness | both | `bright` | `bright` | 6 | 30..210 (210 = 100 %) |
| Blanking | Screen → Blanking | both | `timeout` | `dispTimeout` | 8 | **0** · 30 · 60 s → screensaver |
| Night mode | Screen → Night mode | both | `night`, `nightfrom`, `nightto` | `night`, `nightF`, `nightT` | 17,18,19 | **off**, 23→8; backlight 35, LED dark, 10 fps, the pet falls asleep earlier |
| Carousel | Screen → Carousel | both | `carousel` | `carousel`, `carouselSec` | 7 | −1 off · 5/10/15/… s |
| Carousel mode | — | hub | `carmode` | `carPre` | 27 | 0 custom · **1 at the desk** · 2 home · 3 hardware · 4 quiet · 5 even |
| Screen weights | — | hub | `carfreq` | `carFreq` | 28 | 20 digits, one per scene |
| Screens in the ring | Screen → Screens | hub | `scenemask` | `scnMask` | 12 | bit mask, bit 0 always set |
| Elements | Screen → Elements | hub | `uielem` | `uiElem` | 11 | graphs · arrows · extra lines · paws · quips |
| Home screen | long press on a screen | hub | `pin` | `pinScene` | 15 | −1 = DEN/MAIN |
| Flip | Screen → Flip | both | `flip` | `flip` | 4 | **0** / 1 |
| Forza HUD | Screen → Forza HUD | — | — | — | — | entered by hand; opens itself on UDP |
| Current screen | — | both | `screen` | — | — | 0..19 |

## Pet

| setting | menu | web | rc | NVS | cfg | values / default |
|---|---|---|---|---|---|---|
| Pet | Pet → Pet | both | `pet` | `pet` | 30 | **on**. Off: DEN → MAIN (clock, room, PC, outside), stats frozen, no voice, LED without mood |
| Kind | Pet → Kind | both | `kind` | `kind` | 31 | **0 wolf** Nocturne · 1 dog Hati · 2 cat Shadow · 3 fox Ginger. Same stats; sprite, name, stages, toasts and prompt change |
| Name | — | — | — | — | — | `[pet] name = …` on the card; otherwise the kind's own name |
| Furriness | Pet → Furriness | both | `furry` | `furry` | 32 | **on**. Off: no paws, no sprite in boot/screensaver/quip, neutral toasts |
| LLM voice | Pet → LLM voice | both | `petllm` | `petLlm` | 0 | **on**; off leaves only the phrase cache |
| Chattiness | Pet → Chattiness | both | `wchat` | `wChat` | 1 | 0 off · 1 rare · **2 normal** · 3 often |
| Temper | Pet → Temper | both | `wtone` | `wTone` | 2 | **0 plain** · 1 kind · 2 grump · 3 cheeky |
| Actions | DEN, long press | both | `action` | — | — | feed · play · pet · talk |
| Quip | — | both | `say` | — | — | text ≤160 |
| Achievements | Pet → Achievements | — | — | `wolfstat/*` | — | counters plus experience (`xp`), level = √(xp/25) |
| Game: run | Pet → Game: run | both | `game`=1 | `wolfstat/game` | — | best score |
| Game: reflex | Pet → Game: reflex | both | `game`=2 | `wolfstat/react` | — | 5 rounds, max 500; finishing counts as playing with the pet |
| Skin | — | — | — | — | — | `[pet] skin = name` → `/skins/name.wolf` (512 bytes) |

## Signals

| setting | menu | web | rc | NVS | cfg | values / default |
|---|---|---|---|---|---|---|
| LED | Signals → LED | both | `led` | `led` | 3 | **on** |
| LED mode | Signals → LED mode | both | `ledmode` | `ledMode` | 14 | **0 mood** · 1 off · 2 rainbow · 3 candle |
| LED brightness | Signals → LED brightness | both | `ledbright` | `ledBr` | 34 | 10..**100** %, a ceiling for every mode, alarms included |
| Notifications from the PC | Signals → Notifications | both | `notif` | `notifShow` | 13 | **on** |
| What exactly to show | — | hub | (`/api/notif`) | hub side | — | apps, body text, emoji, duration |

## System

| setting | menu | web | rc | NVS | cfg | values / default |
|---|---|---|---|---|---|---|
| Wi-Fi | System → Wi-Fi | — | — | `netSel` | — | auto / a fixed network from the list |
| The board's web panel | System → Web panel | both | `web` | `web` | 35 | **on**; port 80 on the board's IP (no mDNS: there is no RAM for it next to Zigbee); steps aside when memory runs short |
| System info | System → Info | — | — | — | — | overlay |
| Screenshot | System → Screenshot | — | — | — | — | `/shots/NNN.bmp`, a ring of 24 |
| Pair a sensor | System → Pair a sensor | hub | `zbjoin` | — | — | a 180 s window |
| Factory reset | System → Reset (twice) | — | — | clears `nocturne` | — | the pet and its achievements are left alone |
| Climate thresholds | — | hub | `zbalert`, `zbtmin/max`, `zbhmin/max`, `zbbat` | `zbAl`, `zbT*`, `zbH*`, `zbBat` | 20..25 | |
| Climate archive | — | hub | `zbdump`, `zbint`, `zbpoll` | — | — | one-shot |
| Backlight ceiling | — | hub | `blmax`, `blmins` | not stored | — | an experiment that rolls itself back |

## What works with the PC off

With a fallback hub configured (`[server] host2` on the card, or `server2` in
the console) the board moves over after three failed connects and probes once a
minute to come home. Weather, nodes and events keep arriving — everything the
hub fetches by itself. The private half of the payload simply does not exist
while the PC sleeps: no notifications, no track, no hardware.

With no hub at all the board still holds: DEN/MAIN, HOME, PRESSURE, C6 BOARD,
HISTORY (from the card), WEATHER/FOREST/SERVICES through the lite endpoint (any
host `[lite]` points at), its own clock (NTP), the alarm, night mode, the
Zigbee coordinator and the archive on the card, the pet's voice from the phrase
cache (the LLM too, if an endpoint is set in `[llm]`), and **its own web
panel**. The ring of screens skips whatever needs the PC.

## How this is wired (so you do not break it)

- **One chain of application.** Every source — the hub's panel through `rc` in
  the payload, the board's own panel through `/api/set`, the console — puts the
  value into `AppState::rc*` and raises `rcNew`; only `main.cpp` applies it.
  The board's own panel must call `rcClear()` before filling it in: with the PC
  off, nothing else will clear the previous commands.
- **`cfg:` is only ever appended to.** The hub's panel slices the line by
  index; a new field goes at the end, and into `_CFG_KEYS` in the same commit.
- **A style is four functions.** `panel()`, `panelM()`, `hBar()/vBar()`,
  `textAt()` in `Theme.cpp`. Screens know nothing about style.
- **A pet kind is one module.** `PetKind`: name, stages, toasts, sprites,
  prompt, phrase filter. The stats (`WolfPet`) are untouched when the kind
  changes.
