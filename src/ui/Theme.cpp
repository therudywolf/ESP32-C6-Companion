#include "ui/Theme.h"

#include <U8g2lib.h> /* font data only */

namespace theme {

const lgfx::U8g2font F_SMALL(u8g2_font_5x8_t_cyrillic);
const lgfx::U8g2font F_TEXT(u8g2_font_haxrcorp4089_t_cyrillic);
const lgfx::U8g2font F_MED(u8g2_font_10x20_t_cyrillic);
const lgfx::U8g2font F_VALUE(u8g2_font_helvB10_tr);
const lgfx::U8g2font F_BIG(u8g2_font_logisoso24_tr);
const lgfx::U8g2font F_HUGE(u8g2_font_logisoso32_tr);

/* ── Runtime palette ──────────────────────────────────────────────────── */
uint16_t BG, ORANGE, ORANGE_DIM, TEXT, DIM, PANEL, GOOD, WARN, CRIT, INFO,
    ACCENT;
int currentPreset = 0;

struct Preset {
  const char *name;
  uint16_t bg, chrome, text, dim, panel, good, warn, crit, info, accent;
};

/* Order matches THEME_PRESETS. Colors are RGB565 via rgb(). */
static const Preset kPresets[THEME_PRESETS] = {
    /* 0 Cyberpunk 2077 — samurai red / cyan / amber */
    {"Cyberpunk", rgb(6, 4, 8), rgb(255, 61, 94), rgb(250, 254, 255),
     rgb(158, 196, 210), rgb(44, 18, 28), rgb(0, 255, 192), rgb(255, 200, 20),
     rgb(255, 40, 85), rgb(32, 255, 255), rgb(255, 200, 20)},
    /* 1 Matrix — green phosphor */
    {"Matrix", rgb(2, 8, 3), rgb(0, 255, 90), rgb(200, 255, 200),
     rgb(80, 150, 90), rgb(8, 26, 12), rgb(120, 255, 120), rgb(220, 255, 60),
     rgb(255, 80, 60), rgb(0, 230, 140), rgb(180, 255, 120)},
    /* 2 Amber — retro terminal */
    {"Amber", rgb(10, 6, 0), rgb(255, 176, 0), rgb(255, 224, 160),
     rgb(160, 110, 30), rgb(30, 18, 4), rgb(120, 230, 90), rgb(255, 200, 40),
     rgb(255, 70, 40), rgb(255, 150, 40), rgb(255, 210, 90)},
    /* 3 Synthwave — magenta / cyan */
    {"Synthwave", rgb(8, 4, 14), rgb(255, 43, 214), rgb(240, 230, 255),
     rgb(150, 120, 200), rgb(26, 12, 38), rgb(0, 255, 200), rgb(255, 200, 40),
     rgb(255, 50, 110), rgb(0, 220, 255), rgb(120, 220, 255)},
    /* 4 Ice — cold blue / white */
    {"Ice", rgb(2, 6, 12), rgb(80, 200, 255), rgb(240, 250, 255),
     rgb(120, 160, 190), rgb(10, 22, 34), rgb(80, 255, 200), rgb(255, 210, 80),
     rgb(255, 80, 100), rgb(120, 230, 255), rgb(160, 220, 255)},
};

/* slightly darker chrome for inactive frames */
static uint16_t dimmer(uint16_t c) {
  uint8_t r = ((c >> 11) & 0x1F), g = ((c >> 5) & 0x3F), b = (c & 0x1F);
  return (uint16_t)(((r * 5 / 8) << 11) | ((g * 5 / 8) << 5) | (b * 5 / 8));
}

void applyPreset(int idx) {
  if (idx < 0 || idx >= THEME_PRESETS) idx = 0;
  currentPreset = idx;
  const Preset &p = kPresets[idx];
  BG = p.bg;
  ORANGE = p.chrome;
  ORANGE_DIM = dimmer(p.chrome);
  TEXT = p.text;
  DIM = p.dim;
  PANEL = p.panel;
  GOOD = p.good;
  WARN = p.warn;
  CRIT = p.crit;
  INFO = p.info;
  ACCENT = p.accent;
}

void setChrome(uint8_t r, uint8_t g, uint8_t b) {
  ORANGE = rgb(r, g, b);
  ORANGE_DIM = dimmer(ORANGE);
}

const char *presetName(int idx) {
  if (idx < 0 || idx >= THEME_PRESETS) idx = 0;
  return kPresets[idx].name;
}

void ditherRect(LGFX_Sprite &g, int x, int y, int w, int h, uint16_t color) {
  for (int yy = y; yy < y + h; yy++) {
    for (int xx = x + ((yy ^ x) & 1); xx < x + w; xx += 2) {
      g.drawPixel(xx, yy, color);
    }
  }
}

void panel(LGFX_Sprite &g, int x, int y, int w, int h, const char *title,
           uint16_t color, uint16_t titleColor) {
  const int cut = 4; /* cut corner, bottom-right */
  g.drawFastHLine(x, y, w - 1, color);
  g.drawFastVLine(x, y, h - 1, color);
  g.drawFastVLine(x + w - 1, y, h - cut, color);
  g.drawFastHLine(x, y + h - 1, w - cut, color);
  g.drawLine(x + w - 1, y + h - 1 - cut, x + w - 1 - cut, y + h - 1, color);
  if (title && title[0]) {
    g.setFont(&F_TEXT);
    g.setTextSize(1);
    int tw = g.textWidth(title);
    g.fillRect(x + 5, y - 5, tw + 8, 11, BG);
    g.setTextColor(titleColor);
    g.setCursor(x + 9, y - 4);
    g.print(title);
  }
}

void hBar(LGFX_Sprite &g, int x, int y, int w, int h, int pct, uint16_t color) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  g.drawRect(x, y, w, h, ORANGE_DIM);
  int fill = (w - 4) * pct / 100;
  if (fill > 0) {
    ditherRect(g, x + 2, y + 2, fill, h - 4, color);
    g.drawFastVLine(x + 2 + fill - 1, y + 2, h - 4, color); /* solid tip */
  }
}

void vBar(LGFX_Sprite &g, int x, int y, int w, int h, int pct, uint16_t color) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  g.drawRect(x, y, w, h, ORANGE_DIM);
  int fill = (h - 4) * pct / 100;
  if (fill > 0) {
    ditherRect(g, x + 2, y + h - 2 - fill, w - 4, fill, color);
    g.drawFastHLine(x + 2, y + h - 2 - fill, w - 4, color);
  }
}

void textAt(LGFX_Sprite &g, int x, int y, const char *s, uint16_t color) {
  g.setTextColor(color);
  g.setCursor(x, y);
  g.print(s);
}

void textRight(LGFX_Sprite &g, int xRight, int y, const char *s,
               uint16_t color) {
  int w = g.textWidth(s);
  textAt(g, xRight - w, y, s, color);
}

void textCenter(LGFX_Sprite &g, int xCenter, int y, const char *s,
                uint16_t color) {
  int w = g.textWidth(s);
  textAt(g, xCenter - w / 2, y, s, color);
}

} // namespace theme
