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
#define NOCT_TCP_CONNECT_TIMEOUT_MS 5000
#define NOCT_TCP_RECONNECT_INTERVAL_MS 2000
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

/* ── Misc ─────────────────────────────────────────────────────────────── */
#define NOCT_SD_SPI_HZ 25000000
#define NOCT_SCREENSAVER_DEFAULT_SEC 0 /* 0 = off */
/* Backlight cap: at full PWM (255) this panel self-heats and the matrix blooms
 * a black blob. 210 (~82%) is bright but stays well under the thermal cliff. */
#define NOCT_BRIGHT_MAX 210
#define NOCT_VERSION "1.6.2"

#endif
