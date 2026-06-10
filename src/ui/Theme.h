/*
 * Nocturne C6 — "Flipper Den" design system: palette, fonts, draw helpers.
 * Rule: orange is chrome (frames, headers, selection, wolf); accent colors
 * carry state only (GOOD/warn/crit/info). Dithered fills give the Flipper
 * texture without extra colors.
 */
#ifndef NOCT_THEME_H
#define NOCT_THEME_H

#include "ui/Display.h"

namespace theme {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t BG = rgb(0x00, 0x00, 0x00);
constexpr uint16_t ORANGE = rgb(0xFF, 0x82, 0x00);      /* Flipper chrome */
constexpr uint16_t ORANGE_DIM = rgb(0x7A, 0x3E, 0x00);  /* inactive chrome */
constexpr uint16_t TEXT = rgb(0xF2, 0xEF, 0xE8);        /* warm white */
constexpr uint16_t DIM = rgb(0x8A, 0x8A, 0x80);         /* secondary text */
constexpr uint16_t PANEL = rgb(0x1C, 0x1C, 0x18);       /* panel fill */
constexpr uint16_t GOOD = rgb(0x3D, 0xDC, 0x84);
constexpr uint16_t WARN = rgb(0xFF, 0xC5, 0x3D);
constexpr uint16_t CRIT = rgb(0xFF, 0x3B, 0x30);
constexpr uint16_t INFO = rgb(0x33, 0xC5, 0xD6);        /* net / LLM spark */

/* Fonts (lgfx wrappers over U8g2 font data — Cyrillic capable where needed;
 * defined in Theme.cpp). Chunky 2x integer scaling = the Flipper aesthetic. */
extern const lgfx::U8g2font F_SMALL;  /* 5x8 cyrillic: labels, hints */
extern const lgfx::U8g2font F_TEXT;   /* haxrcorp4089 cyrillic: Flipper text */
extern const lgfx::U8g2font F_VALUE;  /* helvB10: bold values (latin) */
extern const lgfx::U8g2font F_BIG;    /* logisoso24: big numbers */
extern const lgfx::U8g2font F_HUGE;   /* logisoso32: hero numbers */

/* Pick accent by percent-of-limit (load/usage bars). */
inline uint16_t pctColor(int pct) {
  if (pct >= 90) return CRIT;
  if (pct >= 75) return WARN;
  return GOOD;
}
/* Pick accent for a temperature given warn/crit thresholds. */
inline uint16_t tempColor(int t, int warn = 70, int crit = 85) {
  if (t >= crit) return CRIT;
  if (t >= warn) return WARN;
  return GOOD;
}

/* 1px checkerboard fill — reads as a 50% tint, the Flipper texture trick. */
void ditherRect(LGFX_Sprite &g, int x, int y, int w, int h, uint16_t color);

/* Flipper-style panel: thin frame, cut corner, optional title tab. */
void panel(LGFX_Sprite &g, int x, int y, int w, int h,
           const char *title = nullptr, uint16_t color = ORANGE_DIM,
           uint16_t titleColor = ORANGE);

/* Horizontal stat bar: frame + dithered fill + solid tip. */
void hBar(LGFX_Sprite &g, int x, int y, int w, int h, int pct, uint16_t color);

/* Vertical bar (fans etc.). */
void vBar(LGFX_Sprite &g, int x, int y, int w, int h, int pct, uint16_t color);

/* Text helpers (current font/size respected). */
void textAt(LGFX_Sprite &g, int x, int y, const char *s, uint16_t color);
void textRight(LGFX_Sprite &g, int xRight, int y, const char *s,
               uint16_t color);
void textCenter(LGFX_Sprite &g, int xCenter, int y, const char *s,
                uint16_t color);

} // namespace theme

#endif
