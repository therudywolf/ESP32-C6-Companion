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

/* Palette is RUNTIME now (extern globals, defined in Theme.cpp): the user
 * can switch presets or set a custom chrome color from the menu or the
 * companion app. Defaults = Cyberpunk 2077 preset. Draw code reads these
 * as plain variables; default args below bind them at each call. */
extern uint16_t BG;
extern uint16_t ORANGE;     /* chrome: frames, headers, selection */
extern uint16_t ORANGE_DIM; /* inactive chrome */
extern uint16_t TEXT;       /* primary text */
extern uint16_t DIM;        /* secondary text */
extern uint16_t PANEL;      /* panel fill */
extern uint16_t GOOD;       /* ok state */
extern uint16_t WARN;       /* warning state */
extern uint16_t CRIT;       /* alarm (blinks) */
extern uint16_t INFO;       /* data / net / LLM */
extern uint16_t ACCENT;     /* secondary accent */

/* Theme control. 12 presets (see kPresets in Theme.cpp). setChrome/setAccent
 * override individual hues on top of the active preset. */
static const int THEME_PRESETS = 12;
void applyPreset(int idx);
void setChrome(uint8_t r, uint8_t g, uint8_t b);
void setAccent(uint8_t r, uint8_t g, uint8_t b);
const char *presetName(int idx);
extern int currentPreset;

/* Frame clock for animations — set once per frame in SceneManager::draw, read
 * by draw helpers so they animate without threading `now` through every call. */
extern unsigned long nowMs;
/* Linear-interpolate two RGB565 colors (t = 0..255). */
uint16_t lerp565(uint16_t a, uint16_t b, int t);

/* Subtle animated cyber backdrop (faint scanlines + a sweeping sheen) drawn
 * behind scene content in the band y0..y1. Cheap; reads through empty areas. */
void backdrop(LGFX_Sprite &g, int y0, int y1);

/* Fonts (lgfx wrappers over U8g2 font data — Cyrillic capable where needed;
 * defined in Theme.cpp). Chunky 2x integer scaling = the Flipper aesthetic. */
extern const lgfx::U8g2font F_SMALL;  /* 5x8 cyrillic: labels, hints */
extern const lgfx::U8g2font F_TEXT;   /* haxrcorp4089 cyrillic: small text */
extern const lgfx::U8g2font F_MED;    /* 10x20 cyrillic: PRIMARY scene text */
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
