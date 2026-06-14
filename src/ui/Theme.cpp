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
int bgStyle = 1;
bool bgLight = false;
unsigned long nowMs = 0;
int reactLevel = 0;
bool reactAlert = false;
uint16_t uiElements = 0xFFFF;

uint16_t lerp565(uint16_t a, uint16_t b, int t) {
  int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  int r = ar + (br - ar) * t / 255;
  int gg = ag + (bg - ag) * t / 255;
  int bl = ab + (bb - ab) * t / 255;
  return (uint16_t)((r << 11) | (gg << 5) | bl);
}

struct Preset {
  const char *name;
  uint16_t bg, chrome, text, dim, panel, good, warn, crit, info, accent;
};

/* Order matches THEME_PRESETS. Colors are RGB565 via rgb(). Backgrounds and
 * panels are TINTED per theme (not near-black) so switching transforms the
 * whole HUD, not just the accents. Fields: bg, chrome, text, dim, panel,
 * good, warn, crit, info, accent. */
static const Preset kPresets[THEME_PRESETS] = {
    /* 0 Cyberpunk — samurai red on deep wine */
    {"Cyberpunk", rgb(18, 6, 14), rgb(255, 61, 94), rgb(255, 255, 255),
     rgb(170, 150, 175), rgb(48, 16, 30), rgb(0, 255, 192), rgb(255, 200, 20),
     rgb(255, 40, 85), rgb(40, 255, 255), rgb(255, 200, 20)},
    /* 1 Matrix — green phosphor on black-green */
    {"Matrix", rgb(2, 18, 6), rgb(0, 255, 90), rgb(190, 255, 190),
     rgb(90, 170, 100), rgb(6, 40, 16), rgb(120, 255, 120), rgb(220, 255, 60),
     rgb(255, 90, 60), rgb(0, 240, 150), rgb(180, 255, 120)},
    /* 2 Amber — retro terminal on dark brown */
    {"Amber", rgb(24, 14, 2), rgb(255, 176, 0), rgb(255, 226, 165),
     rgb(180, 130, 50), rgb(52, 32, 6), rgb(150, 230, 90), rgb(255, 200, 40),
     rgb(255, 80, 40), rgb(255, 160, 50), rgb(255, 210, 90)},
    /* 3 Synthwave — magenta/cyan on deep purple */
    {"Synthwave", rgb(22, 6, 38), rgb(255, 43, 214), rgb(245, 235, 255),
     rgb(165, 135, 215), rgb(48, 18, 70), rgb(0, 255, 200), rgb(255, 210, 60),
     rgb(255, 50, 110), rgb(0, 230, 255), rgb(140, 110, 255)},
    /* 4 Ice — cold blue/white on midnight blue */
    {"Ice", rgb(4, 14, 30), rgb(90, 210, 255), rgb(240, 250, 255),
     rgb(130, 175, 205), rgb(14, 34, 58), rgb(80, 255, 200), rgb(255, 210, 80),
     rgb(255, 90, 110), rgb(130, 235, 255), rgb(170, 220, 255)},
    /* 5 Vaporwave — pink/cyan on dark magenta */
    {"Vaporwave", rgb(28, 8, 40), rgb(255, 113, 206), rgb(245, 235, 255),
     rgb(175, 145, 215), rgb(56, 20, 78), rgb(5, 255, 161), rgb(255, 210, 60),
     rgb(255, 80, 120), rgb(1, 220, 254), rgb(185, 103, 255)},
    /* 6 Gruvbox — warm retro on dark earth */
    {"Gruvbox", rgb(40, 36, 33), rgb(254, 128, 25), rgb(235, 219, 178),
     rgb(180, 165, 142), rgb(60, 56, 50), rgb(184, 187, 38), rgb(250, 189, 47),
     rgb(251, 73, 52), rgb(142, 192, 124), rgb(211, 134, 155)},
    /* 7 Dracula — purple/pink on dracula bg */
    {"Dracula", rgb(40, 42, 54), rgb(189, 147, 249), rgb(248, 248, 242),
     rgb(120, 134, 180), rgb(58, 62, 80), rgb(80, 250, 123), rgb(241, 250, 140),
     rgb(255, 85, 85), rgb(139, 233, 253), rgb(255, 121, 198)},
    /* 8 Nord — cool blue/grey on polar night */
    {"Nord", rgb(38, 44, 56), rgb(136, 192, 208), rgb(236, 239, 244),
     rgb(140, 158, 180), rgb(58, 66, 82), rgb(163, 190, 140), rgb(235, 203, 139),
     rgb(208, 110, 120), rgb(129, 161, 193), rgb(180, 142, 173)},
    /* 9 Blood — aggressive red on black-red */
    {"Blood", rgb(26, 4, 4), rgb(255, 40, 40), rgb(255, 235, 235),
     rgb(190, 110, 110), rgb(60, 12, 12), rgb(0, 230, 130), rgb(255, 180, 0),
     rgb(255, 0, 0), rgb(255, 130, 130), rgb(255, 90, 90)},
    /* 10 Forest — green/earth on deep forest */
    {"Forest", rgb(8, 26, 12), rgb(130, 210, 90), rgb(232, 248, 224),
     rgb(140, 175, 140), rgb(20, 46, 26), rgb(160, 240, 100), rgb(235, 205, 70),
     rgb(245, 100, 70), rgb(120, 220, 170), rgb(190, 235, 130)},
    /* 11 Mono — minimal white/grey on slate */
    {"Mono", rgb(24, 26, 30), rgb(235, 238, 245), rgb(255, 255, 255),
     rgb(150, 162, 175), rgb(44, 48, 56), rgb(120, 235, 160), rgb(245, 215, 95),
     rgb(255, 95, 95), rgb(150, 205, 255), rgb(205, 210, 220)},
};

/* slightly darker chrome for inactive frames */
static uint16_t dimmer(uint16_t c) {
  uint8_t r = ((c >> 11) & 0x1F), g = ((c >> 5) & 0x3F), b = (c & 0x1F);
  return (uint16_t)(((r * 5 / 8) << 11) | ((g * 5 / 8) << 5) | (b * 5 / 8));
}

/* scale a colour's luminance by num/den (clamped) */
static uint16_t scale565(uint16_t c, int num, int den) {
  int r = ((c >> 11) & 0x1F) * num / den;
  int g = ((c >> 5) & 0x3F) * num / den;
  int b = (c & 0x1F) * num / den;
  if (r > 31) r = 31;
  if (g > 63) g = 63;
  if (b > 31) b = 31;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

/* Push a colour away from its own grey (saturation boost). pct>100 = more
 * vivid. Counteracts RGB565 mid-tone dullness without raising the backlight. */
static uint16_t saturate(uint16_t c, int pct) {
  int r = ((c >> 11) & 0x1F) * 255 / 31;
  int g = ((c >> 5) & 0x3F) * 255 / 63;
  int b = (c & 0x1F) * 255 / 31;
  int gray = (r * 30 + g * 59 + b * 11) / 100;
  r = gray + (r - gray) * pct / 100;
  g = gray + (g - gray) * pct / 100;
  b = gray + (b - gray) * pct / 100;
  r = r < 0 ? 0 : (r > 255 ? 255 : r);
  g = g < 0 ? 0 : (g > 255 ? 255 : g);
  b = b < 0 ? 0 : (b > 255 ? 255 : b);
  return rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

/* Recompute BG/PANEL/TEXT family + darken accents for light mode. */
static void applyBgLight() {
  if (!bgLight) return;
  BG = rgb(234, 238, 244);
  PANEL = rgb(206, 213, 224);
  TEXT = rgb(16, 18, 26);
  DIM = rgb(96, 106, 120);
  /* chrome + state colours need darkening to stay legible on white */
  ORANGE = scale565(ORANGE, 70, 100);
  ORANGE_DIM = lerp565(ORANGE, PANEL, 120);
  GOOD = scale565(GOOD, 62, 100);
  WARN = scale565(WARN, 72, 100);
  CRIT = scale565(CRIT, 78, 100);
  INFO = scale565(INFO, 60, 100);
  ACCENT = scale565(ACCENT, 70, 100);
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
  /* dark mode: boost chroma so colours read vivid even at the safe (sub-max)
   * backlight. Light mode darkens instead (applyBgLight). Structural roles
   * (BG/TEXT/DIM/PANEL) are left alone. */
  if (!bgLight) {
    ORANGE = saturate(ORANGE, 122);
    ORANGE_DIM = dimmer(ORANGE);
    GOOD = saturate(GOOD, 122);
    WARN = saturate(WARN, 122);
    CRIT = saturate(CRIT, 122);
    INFO = saturate(INFO, 122);
    ACCENT = saturate(ACCENT, 122);
  }
  applyBgLight();
}

void setBgStyle(int s) { bgStyle = (s < 0 || s >= BG_STYLES) ? 0 : s; }

void setBgLight(bool light) {
  bgLight = light;
  applyPreset(currentPreset); /* re-derive palette in the new mode */
}

const char *bgStyleName(int s) {
  static const char *n[BG_STYLES] = {"выкл", "анимация", "сетка"};
  return n[(s < 0 || s >= BG_STYLES) ? 0 : s];
}

void setChrome(uint8_t r, uint8_t g, uint8_t b) {
  ORANGE = rgb(r, g, b);
  ORANGE_DIM = dimmer(ORANGE);
}

void setAccent(uint8_t r, uint8_t g, uint8_t b) { ACCENT = rgb(r, g, b); }

/* Role table — order MUST match COLOR_ROLES / the web editor. */
static uint16_t *const kRoleVar[COLOR_ROLES] = {
    &BG, &ORANGE, &TEXT, &DIM, &PANEL, &GOOD, &WARN, &CRIT, &INFO, &ACCENT};
static const char *const kRoleName[COLOR_ROLES] = {
    "Фон", "Рамки", "Текст", "Втор.текст", "Панель",
    "OK",  "Предупр", "Тревога", "Данные", "Акцент"};

const char *roleName(int role) {
  return (role < 0 || role >= COLOR_ROLES) ? "" : kRoleName[role];
}

void setColorRole(int role, uint8_t r, uint8_t g, uint8_t b) {
  if (role < 0 || role >= COLOR_ROLES) return;
  *kRoleVar[role] = rgb(r, g, b);
  if (role == 1) ORANGE_DIM = dimmer(ORANGE); /* chrome → derive inactive */
}

void getPalette(uint16_t out[COLOR_ROLES]) {
  for (int i = 0; i < COLOR_ROLES; i++) out[i] = *kRoleVar[i];
}

void applyPalette(const uint16_t pal[COLOR_ROLES]) {
  for (int i = 0; i < COLOR_ROLES; i++) *kRoleVar[i] = pal[i];
  ORANGE_DIM = dimmer(ORANGE);
}

const char *presetName(int idx) {
  if (idx < 0 || idx >= THEME_PRESETS) idx = 0;
  return kPresets[idx].name;
}

void backdrop(LGFX_Sprite &g, int y0, int y1) {
  int energy = reactLevel < 0 ? 0 : (reactLevel > 100 ? 100 : reactLevel);
  if (bgStyle == 0) {
    /* even on a solid background, a hot alert breathes a faint red edge */
    if (reactAlert) {
      int p = ((nowMs / 350) & 1) ? 18 : 7;
      g.drawRect(0, y0, 320, y1 - y0, lerp565(BG, CRIT, p));
      g.drawRect(1, y0 + 1, 318, y1 - y0 - 2, lerp565(BG, CRIT, p / 2));
    }
    return;
  }
  /* the backdrop LIVES: busier PC sweeps faster & brighter, an alert turns it
   * red with a second counter-sweep. */
  uint16_t tint = reactAlert ? CRIT : (bgLight ? DIM : INFO);
  uint16_t sheenC = reactAlert ? CRIT : (bgLight ? rgb(0, 0, 0) : TEXT);
  int sweepDiv = 13 - energy / 12; /* 13..~5: faster under load */
  if (sweepDiv < 4) sweepDiv = 4;
  if (bgStyle == 1) {
    uint16_t sl = lerp565(BG, tint, (bgLight ? 14 : 5) + energy / 18);
    for (int y = y0; y < y1; y += 4) g.drawFastHLine(0, y, 320, sl);
    int sheen = 14 + energy / 8; /* brighter sheen under load */
    int sx = (int)((nowMs / sweepDiv) % (320 + 90)) - 45;
    for (int dx = -7; dx <= 7; dx++) {
      int xx = sx + dx;
      if (xx < 0 || xx >= 320) continue;
      int a = sheen - (dx < 0 ? -dx : dx) * 2;
      if (a > 0) g.drawFastVLine(xx, y0, y1 - y0, lerp565(BG, sheenC, a));
    }
    if (reactAlert) { /* red counter-sweep that screams "look here" */
      int sx2 = 365 - ((int)((nowMs / 6) % (320 + 90)));
      for (int dx = -5; dx <= 5; dx++) {
        int xx = sx2 + dx;
        if (xx < 0 || xx >= 320) continue;
        int a = 13 - (dx < 0 ? -dx : dx) * 2;
        if (a > 0) g.drawFastVLine(xx, y0, y1 - y0, lerp565(BG, CRIT, a));
      }
    }
  } else {
    /* drifting dot grid — drifts faster and brightens with load */
    uint16_t dot = lerp565(BG, tint, (bgLight ? 28 : 12) + energy / 10);
    int ds = 90 - energy / 2;
    if (ds < 28) ds = 28;
    int ox = (int)((nowMs / ds) % 18), oy = (int)((nowMs / (ds + 40)) % 18);
    for (int y = y0 + oy; y < y1; y += 18)
      for (int x = ox; x < 320; x += 18) g.drawPixel(x, y, dot);
  }
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
  /* clean full rectangle frame + brighter L-accents at two corners (static,
   * HUD feel, no moving glint that reads as a glitch) */
  g.drawRect(x, y, w, h, color);
  g.drawFastHLine(x, y, 10, titleColor);
  g.drawFastVLine(x, y, 8, titleColor);
  g.drawFastHLine(x + w - 10, y + h - 1, 10, titleColor);
  g.drawFastVLine(x + w - 1, y + h - 8, 8, titleColor);
  if (title && title[0]) {
    g.setFont(&F_TEXT);
    g.setTextSize(1);
    int tw = g.textWidth(title);
    g.fillRect(x + 6, y - 5, tw + 8, 11, BG); /* tab punches the frame */
    g.setTextColor(titleColor);
    g.setCursor(x + 10, y - 4);
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
    /* bright shimmer sweeping left→right across the filled region */
    int sx = x + 2 + (int)((nowMs / 9 + x * 5) % (fill + 1));
    uint16_t hi = lerp565(color, TEXT, 150);
    g.drawFastVLine(sx, y + 2, h - 4, hi);
  }
}

void vBar(LGFX_Sprite &g, int x, int y, int w, int h, int pct, uint16_t color) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  g.drawRect(x, y, w, h, ORANGE_DIM);
  int fill = (h - 4) * pct / 100;
  if (fill > 0) {
    int top = y + h - 2 - fill;
    ditherRect(g, x + 2, top, w - 4, fill, color);
    g.drawFastHLine(x + 2, top, w - 4, color);
    /* shimmer sweeping up the column */
    int sy = y + h - 2 - (int)((nowMs / 9 + y * 5) % (fill + 1));
    uint16_t hi = lerp565(color, TEXT, 150);
    g.drawFastHLine(x + 2, sy, w - 4, hi);
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

const char *clipW(LGFX_Sprite &g, const char *s, char *out, size_t cap,
                  int maxW) {
  if (!cap) return out;
  size_t n = 0;            /* bytes committed (excl. NUL) */
  out[0] = '\0';
  while (*s && n + 1 < cap) {
    /* codepoint length from the UTF-8 lead byte */
    unsigned char c = (unsigned char)*s;
    int len = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
    if (n + (size_t)len + 1 > cap) break;       /* would overflow buffer */
    for (int i = 0; i < len; i++) out[n + i] = s[i];
    out[n + len] = '\0';
    if (g.textWidth(out) > maxW) {              /* this glyph overran — drop it */
      out[n] = '\0';
      break;
    }
    n += len;
    s += len;
  }
  return out;
}

} // namespace theme
