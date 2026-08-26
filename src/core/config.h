/*
 * Nocturne C6 — board pin map (Waveshare ESP32-C6-LCD-1.47) + timing constants.
 * Timing values ported from Nocturne OS (lib/nocturne-core config.h) — they
 * match the PC server's heartbeat and must not drift from it.
 */
#ifndef NOCT_CONFIG_H
#define NOCT_CONFIG_H

/* ── Pins ─────────────────────────────────────────────────────────────── */
/* LCD: ST7789V3 172x320 on SPI2, shared bus with the TF slot. */
#define NOCT_PIN_LCD_SCLK 7
#define NOCT_PIN_LCD_MOSI 6
#define NOCT_PIN_LCD_CS 14
#define NOCT_PIN_LCD_DC 15
#define NOCT_PIN_LCD_RST 21
#define NOCT_PIN_LCD_BL 22 /* active HIGH, LEDC PWM-able */

/* TF/microSD, SPI mode only (D1/D2 not routed; C6 has no SDMMC host). */
#define NOCT_PIN_SD_CS 4
#define NOCT_PIN_SD_MISO 5

/* WS2812B-0807 single RGB LED (GRB, 800 kHz). Strapping pin: output-only. */
#define NOCT_PIN_RGB 8

/* BOOT button: external 10K pull-up, active LOW. Strapping pin: never drive,
 * input only — safe as a user button after boot. */
#define NOCT_PIN_BUTTON 9

/* ── Display geometry (landscape, rotation 1) ─────────────────────────── */
#define NOCT_W 320
#define NOCT_H 172
#define NOCT_STATUS_H 20  /* status bar: y 0..19 */
#define NOCT_CONTENT_TOP 20
#define NOCT_CONTENT_H 132 /* content band: y 20..151 */
#define NOCT_FOOTER_TOP 152 /* footer/hints: y 152..171 */

/* ── Render loop ──────────────────────────────────────────────────────── */
#define NOCT_FRAME_MS 40        /* 25 fps — cooler board, still fluid */
#define NOCT_TRANSITION_MS 180  /* scene slide duration (ported) */
#define NOCT_TOAST_MS 1800

/* ── Telemetry protocol (MUST match the PC server) ────────────────────── */
#define NOCT_TCP_LINE_MAX 4096
#define NOCT_TCP_CONNECT_TIMEOUT_MS 1500 /* bounded connect: an unreachable PC (off) stalls the render loop at most this long (was 5000) */
#define NOCT_TCP_RECONNECT_INTERVAL_MS 2000 /* base retry gap; grows via backoff below */
#define NOCT_TCP_RECONNECT_MAX_MS 30000 /* backoff ceiling while the PC stays offline, so the UI stops stuttering */
#define NOCT_SIGNAL_GRACE_MS 8000  /* after connect, before "no signal" */
#define NOCT_SIGNAL_TIMEOUT_MS 5000 /* silence after first data = stale */

/* ── WiFi ─────────────────────────────────────────────────────────────── */
#define NOCT_WIFI_RETRY_INTERVAL_MS 8000
#define NOCT_WIFI_CONNECT_TIMEOUT_MS 15000
#define NOCT_WIFI_MAX_NETS 5

/* ── LLM (LM Studio) ──────────────────────────────────────────────────── */
#define NOCT_LLM_TIMEOUT_MS 25000      /* JIT model load can take ~13 s cold */
#define NOCT_LLM_COOLDOWN_MS 60000     /* min gap between speech requests */
#define NOCT_LLM_MAX_TOKENS 800        /* reasoning (~300-450) + the reply */
#define NOCT_LLM_REPLY_MAX 200         /* hard truncate, bytes (UTF-8) */
#define NOCT_LLM_IDLE_CHATTER_MIN_MS (20UL * 60UL * 1000UL)
#define NOCT_LLM_IDLE_CHATTER_RND_MS (20UL * 60UL * 1000UL)

/* ── Pet (ported decay model — keep identical to old firmware) ────────── */
#define NOCT_WOLF_DECAY_INTERVAL_MS 90000UL
#define NOCT_WOLF_SAVE_INTERVAL_MS 300000UL
#define NOCT_WOLF_DAY_MS 3600000UL /* 1 pet-day = 1 real hour */

/* ── Forza "Data Out" telemetry ───────────────────────────────────────── */
#define NOCT_FORZA_UDP_PORT 5300
#define NOCT_FORZA_TIMEOUT_MS 3000
#define NOCT_FORZA_MIN_PACKET 311
#define NOCT_FORZA_SHIFT_PCT 0.95f /* strobe + SHIFT! above this rev pct */
#define NOCT_FORZA_MAX_DRAIN 8 /* cap UDP packets parsed per tick (bound frame time) */

/* ── Clock ────────────────────────────────────────────────────────────── */
/* Own NTP clock so the time survives the PC being off. MSK is UTC+3 with no
 * DST, hence a fixed offset and an empty DST rule. */
#define NOCT_TZ_OFFSET_SEC (3 * 3600)
#define NOCT_TZ_DST_SEC 0
#define NOCT_NTP_PRIMARY "pool.ntp.org"
#define NOCT_NTP_SECONDARY "time.google.com"

/* ── Night mode (quiet hours) ─────────────────────────────────────────── */
/* Between nightFrom:00 and nightTo:00 the panel drops to this backlight and the
 * mood LED goes dark — a desk device must not light the room at 3 a.m. A button
 * press suspends it for the grace window below. Needs the NTP clock. */
#define NOCT_NIGHT_BRIGHT 35
#define NOCT_NIGHT_WAKE_MS 30000UL

/* ── Button ───────────────────────────────────────────────────────────── */
/* Hold-to-repeat is OPT-IN per UI context (SceneManager enables it for lists
 * and the colour editor only), so the LONG grammar is untouched everywhere
 * else — see InputSystem::setRepeatEnabled. */
#define NOCT_BTN_LONG_MS 500
#define NOCT_BTN_REPEAT_DELAY_MS 900 /* hold this long before repeats start */
#define NOCT_BTN_REPEAT_MS 160       /* gap between repeats */

/* ── Zigbee ───────────────────────────────────────────────────────────── */
/* The board is the coordinator. Measured cost: +380 KB flash, +20 KB static
 * RAM, and no OTA — a single 3.62 MB app partition is the only 4 MB layout that
 * fits the stack (Espressif's own zigbee_zczr gives 1.25 MB slots, which a
 * hello-world coordinator already overflows). Updates are by cable now.
 * Endpoint 1 is the conventional first application endpoint. */
#define NOCT_ZB_ENDPOINT 1
#define NOCT_ZB_JOIN_SEC 180
/* What this hub calls itself. Goes three places: the Basic cluster's
 * ModelIdentifier (so the board is recognisable from any other Zigbee tool),
 * the default name of the first paired sensor, and the ДОМ screen's header.
 * nocturne.ini [zigbee] name1..name4 override the per-sensor names. */
#define NOCT_ZB_NET_NAME "ForestHome"
#define NOCT_ZB_VENDOR "Nocturne"
/* 802.15.4 channel for a NEW network formation. 25 = 2.475 GHz, above WiFi
 * channel 11 - measured on this board, parking Zigbee on top of the WiFi
 * frequency starved WiFi RX to zero payloads while TCP stayed "connected". */
#define NOCT_ZB_CHANNEL 25

/* ── Storage caps (SD is optional; rotate, never freeze) ──────────────── */
/* How much of a line file's tail is ever read back. A cap ABOVE this makes the
 * older half of a rotated file unreachable — which is exactly what happened to
 * the phrase cache: it rotated at 8 KB while only the newest 4 KB was ever
 * sampled. Keep every cap <= this. */
#define NOCT_SD_READ_MAX 4096
#define NOCT_SD_PHRASE_MAX 4096   /* per phrase-cache bucket, bytes */
#define NOCT_SD_DIARY_MAX 32768   /* /wolf/memory.jsonl (only its tail is read) */
/* Entries drained per flush(). ONE, because measurement says so: a 1332-byte
 * write to this card takes 30-140 ms, and that is the card's internal program
 * time, not the SPI clock — no bus speed makes it go away. Since all SPI lives
 * on the render loop, each queued entry is worth up to three dropped frames, so
 * they get spread one per flush tick (twice a second) instead of batched. */
#define NOCT_SD_FLUSH_MAX 1
/* Log any card operation slower than this. Measured baseline on this board at
 * 10 MHz: reads ~80 ms, writes 30-140 ms — dominated by the card's internal
 * program time, not the SPI clock. So the threshold sits ABOVE the normal
 * range: set at the frame budget it fired on every single write and the
 * diagnostic became noise, which is the exact failure it exists to prevent. */
#define NOCT_SD_SLOW_MS 250
/* Consecutive failures before the card is declared gone. A pulled card used to
 * fail forever, once per call, because ok_ was decided at boot and never again. */
#define NOCT_SD_FAIL_LIMIT 5
/* Album covers kept on the card (18 KB each, RGB565 96x96). */
#define NOCT_COVER_CACHE_MAX 24
/* Flush the history snapshot once a minute, right after a sample is committed.
 * It must stay well under HourHistory's 5-minute freshness window: at the old
 * 5-minute cadence a reboot landed anywhere between 0 and 300 s after the last
 * save, so restoring the minute series was a coin flip. 1332 B/min is nothing
 * for an SD card. */
#define NOCT_HIST_SAVE_MS 60000UL

/* ── Board thermals (temperatureRead(), the C6's own die sensor) ──────── */
/* RE-MEASURED 2026-08-26, with the Zigbee coordinator running: 67.3 C steady,
 * 68.3 C peak at full brightness. The old numbers here — 49.3 steady, 51.3
 * peak — were taken BEFORE the 802.15.4 radio existed, and the comment then
 * claimed "warm starts 20 C above anything normal". Coexistence with WiFi
 * costs about 17 C of die temperature, so that margin had quietly become
 * 2.7 C and the guard was tripping during ordinary use: the backlight kept
 * being pulled from 210 to 150 for no fault at all, which is precisely what
 * "the screen is too dim" turned out to be.
 *
 * A limit calibrated against a machine that no longer exists is worse than no
 * limit, because it fires and looks like a hardware problem. 78/88 restores
 * the intent — catch a blocked vent, a sunny windowsill, a panel cooking
 * itself — without firing on the normal state of the board as it is now.
 * The C6 is specified well past this; the number that protects the PANEL is
 * NOCT_BRIGHT_MAX, and it is separate. */
#define NOCT_BOARD_WARM_C 78.0f  /* start pulling the backlight down */
#define NOCT_BOARD_HOT_C 88.0f   /* hold it at the dim floor */
#define NOCT_BOARD_WARM_BRIGHT 150
#define NOCT_BOARD_HOT_BRIGHT 90

/* ── Misc ─────────────────────────────────────────────────────────────── */
#define NOCT_SD_SPI_HZ 25000000
#define NOCT_SCREENSAVER_DEFAULT_SEC 0 /* 0 = off */
/* Backlight cap: at full PWM (255) this panel self-heats and the matrix blooms
 * a black blob. 210 (~82%) is bright but stays well under the thermal cliff.
 * NOCT_BRIGHT_MAX is 100% as far as the UI is concerned — never divide the
 * displayed percentage by 255, or the menu tops out at "82%". */
/* How long presence waits before it may act again. Ten minutes, and measured
 * from when the action FIRED rather than from the last motion — timing it
 * from motion would let continuous presence push the window out forever and
 * it would never settle.
 *
 * Far longer than the sensor's own 60 s lockout on purpose: waking a PC is
 * intrusive and cannot be undone by waiting, so somebody crossing the room
 * three times with laundry must not send three magic packets. */
#define NOCT_PRESENCE_COOLDOWN_MS 600000UL

#define NOCT_BRIGHT_MAX 210
#define NOCT_VERSION "1.29.1"

#endif
