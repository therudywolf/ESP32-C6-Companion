#include "ui/Theme.h"

#include <math.h>

#include "storage/SdStore.h"

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
uint16_t SURFACE = 0, SURFACE_DIM = 0;

/* Defined below, called from applyPreset above it. */
static void deriveSurface();
unsigned long nowMs = 0;
int reactLevel = 0;
int weatherCode = 0;
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
     rgb(190, 142, 64), rgb(52, 32, 6), rgb(150, 230, 90), rgb(255, 200, 40),
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
    {"Dracula", rgb(40, 42, 54), rgb(196, 158, 250), rgb(248, 248, 242),
     rgb(160, 176, 200), rgb(58, 62, 80), rgb(80, 250, 123), rgb(241, 250, 140),
     rgb(255, 85, 85), rgb(139, 233, 253), rgb(255, 121, 198)},
    /* 8 Nord — cool blue/grey on polar night */
    {"Nord", rgb(38, 44, 56), rgb(136, 192, 208), rgb(236, 239, 244),
     rgb(160, 180, 192), rgb(58, 66, 82), rgb(163, 190, 140), rgb(235, 203, 139),
     rgb(208, 110, 120), rgb(129, 161, 193), rgb(180, 142, 173)},
    /* 9 Blood — aggressive red on black-red */
    {"Blood", rgb(26, 4, 4), rgb(255, 72, 72), rgb(255, 235, 235),
     rgb(192, 116, 112), rgb(60, 12, 12), rgb(0, 230, 130), rgb(255, 180, 0),
     rgb(255, 0, 0), rgb(255, 130, 130), rgb(255, 90, 90)},
    /* 10 Forest — green/earth on deep forest */
    {"Forest", rgb(8, 26, 12), rgb(130, 210, 90), rgb(232, 248, 224),
     rgb(140, 175, 140), rgb(20, 46, 26), rgb(160, 240, 100), rgb(235, 205, 70),
     rgb(245, 100, 70), rgb(120, 220, 170), rgb(190, 235, 130)},
    /* 11 Mono — minimal white/grey on slate */
    {"Mono", rgb(24, 26, 30), rgb(235, 238, 245), rgb(255, 255, 255),
     rgb(150, 162, 175), rgb(44, 48, 56), rgb(120, 235, 160), rgb(245, 215, 95),
     rgb(255, 95, 95), rgb(150, 205, 255), rgb(205, 210, 220)},

    /* ── The four below were added for what this device BECAME: something
     * that sits in a room and reports on that room, day and night. The first
     * twelve are HUD palettes — excellent on a desk beside a gaming PC, and
     * all wrong at 3 a.m. or in direct sun. ────────────────────────────── */

    /* 12 Свеча — warm, almost no blue. Evening light: blue is the part of the
     * spectrum that suppresses melatonin, so a bedside screen that has to be
     * on all night should have as little of it as possible. Even the "info"
     * role is pulled to warm gold rather than cyan. */
    {"Свеча", rgb(20, 10, 2), rgb(255, 158, 46), rgb(255, 232, 190),
     rgb(176, 132, 80), rgb(44, 24, 8), rgb(196, 214, 92), rgb(255, 190, 60),
     rgb(255, 96, 40), rgb(240, 178, 88), rgb(255, 206, 130)},

    /* 13 Бумага — dark ink on light paper, for a sunlit room. Every other
     * preset is light-on-dark, which is right in a dim room and unreadable
     * with a window behind you: an LCD cannot outshine daylight, so the fix is
     * to stop trying and use the backlight as paper instead. Roles keep their
     * meaning but are darkened until they read against white. */
    {"Бумага", rgb(238, 236, 230), rgb(28, 42, 72), rgb(20, 22, 26),
     rgb(96, 100, 112), rgb(250, 249, 245), rgb(22, 122, 60),
     rgb(176, 110, 0), rgb(190, 30, 40), rgb(28, 88, 160), rgb(70, 90, 130)},

    /* 14 Ночь — deep red only. Red light preserves dark adaptation, which is
     * why instrument panels and observatories use it; this is the palette for
     * a screen you glance at without waking up.
     *
     * It used to be dim as well as red, and those are different things. Rod
     * cells are what dark adaptation lives in and they are nearly blind to
     * long wavelengths, so a BRIGHT red costs night vision very little — it
     * is the green and blue that ruin it. Lowering the red instead bought
     * nothing and made the screen unreadable: 2.0 against the ground, where
     * 4.5 is the floor for small print.
     *
     * So the red is now strong and the grounds are nearly black: contrast
     * comes from the ink, not from lifting everything. Green and blue stay
     * minimal, which is the part that actually matters at 3 a.m. */
    {"Ночь", rgb(8, 0, 0), rgb(255, 56, 48), rgb(255, 130, 116),
     rgb(216, 82, 72), rgb(20, 2, 2), rgb(230, 96, 64), rgb(255, 128, 56),
     rgb(255, 80, 68), rgb(226, 84, 76), rgb(255, 110, 90)},

    /* 15 Мох — muted green and clay, nothing saturated. A climate device is
     * furniture: it is in the corner of your eye for hours, and a HUD palette
     * that is exciting for ten minutes is tiring for ten hours. Alert roles
     * stay vivid so they still cut through the calm. */
    {"Мох", rgb(14, 20, 16), rgb(142, 176, 120), rgb(228, 236, 220),
     rgb(128, 152, 128), rgb(30, 40, 32), rgb(150, 200, 130),
     rgb(224, 178, 92), rgb(214, 96, 74), rgb(140, 180, 175),
     rgb(178, 196, 150)},

    /* 16 Закат — coral and gold over a violet dusk. Warm like Amber but not
     * a terminal: Amber is one hue at three brightnesses, this has an actual
     * colour scheme. */
    {"Закат", rgb(26, 12, 34), rgb(255, 122, 88), rgb(255, 234, 214),
     rgb(178, 132, 150), rgb(52, 24, 62), rgb(255, 196, 92),
     rgb(255, 168, 60), rgb(255, 80, 96), rgb(255, 154, 120),
     rgb(255, 190, 140)},

    /* 17 Сакура — soft pink on deep plum. Vaporwave without the neon: the
     * same family of hues taken down to something you can live beside. */
    {"Сакура", rgb(28, 16, 28), rgb(246, 168, 196), rgb(255, 238, 245),
     rgb(176, 138, 158), rgb(52, 32, 50), rgb(168, 226, 178),
     rgb(246, 206, 130), rgb(240, 108, 128), rgb(198, 176, 240),
     rgb(230, 186, 210)},

    /* 18 Море — teal and aqua on deep water. Ice is a cold blue; this is
     * green-blue and warmer for it, which suits a room rather than a HUD. */
    {"Море", rgb(4, 22, 26), rgb(64, 200, 190), rgb(224, 248, 246),
     rgb(112, 158, 158), rgb(12, 44, 50), rgb(110, 226, 170),
     rgb(238, 200, 110), rgb(250, 110, 110), rgb(96, 206, 232),
     rgb(140, 216, 206)},

    /* 19 Кофе — cream on espresso. Reads like paper in a dim room, where the
     * light Бумага theme would be a lamp in your face. */
    {"Кофе", rgb(26, 18, 14), rgb(214, 172, 124), rgb(245, 232, 214),
     rgb(160, 140, 120), rgb(48, 34, 26), rgb(166, 194, 128),
     rgb(230, 176, 96), rgb(214, 104, 82), rgb(182, 168, 208),
     rgb(224, 196, 158)},

    /* 20 Хвоя — pine and brass. Forest is bright green; this is the dark
     * needle-green of a winter wood, with warm metal for the accents. */
    {"Хвоя", rgb(8, 20, 16), rgb(206, 174, 106), rgb(226, 238, 226),
     rgb(128, 148, 136), rgb(18, 40, 32), rgb(122, 196, 132),
     rgb(226, 186, 96), rgb(224, 104, 84), rgb(128, 186, 170),
     rgb(186, 176, 128)},

    /* 21 Сталь — cool grey-blue, industrial. Mono is neutral grey; this is
     * grey with a temperature, which keeps the alert roles from looking like
     * the only colours on the screen. */
    {"Сталь", rgb(18, 22, 28), rgb(150, 178, 204), rgb(232, 240, 248),
     rgb(136, 148, 168), rgb(34, 42, 52), rgb(126, 206, 168),
     rgb(230, 196, 110), rgb(238, 108, 108), rgb(140, 186, 232),
     rgb(178, 198, 220)},
};

/* Slightly darker chrome for inactive frames — and this is the colour EVERY
 * tile border on every screen is drawn in, so "slightly" has to be true.
 *
 * It was 5/8, applied to the RGB565 channel values. Those are gamma-encoded,
 * so cutting them by 37 % cuts PERCEIVED luminance by a great deal more, and
 * the frames that define every tile boundary fell under the 3.0 floor for a
 * graphical element in eight of the twenty-two themes - Blood worst at 2.3.
 * Tile edges were nearly invisible and the layout read as floating text.
 *
 * 13/16 keeps the frame a clear step quieter than the title, which is the
 * whole reason this function exists, and clears 3.0 in every theme with
 * margin. tools/check_contrast.py reads this factor out of the source rather
 * than repeating it: a check that hardcodes the value it is checking passes
 * happily after the code changes underneath it. */
static uint16_t dimmer(uint16_t c) {
  uint8_t r = ((c >> 11) & 0x1F), g = ((c >> 5) & 0x3F), b = (c & 0x1F);
  return (uint16_t)(((r * 13 / 16) << 11) | ((g * 13 / 16) << 5) |
                    (b * 13 / 16));
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

/* Card themes: a name plus the same ten roles the on-device colour editor
 * edits, so the format is exactly what the board already understands. */
struct CardTheme {
  char name[20];
  uint16_t role[COLOR_ROLES];
};
static CardTheme gCardThemes[CARD_THEMES_MAX];
static int gCardThemeCount = 0;

int cardThemeCount() { return gCardThemeCount; }
int presetTotal() { return THEME_PRESETS + gCardThemeCount; }

void applyPreset(int idx) {
  if (idx < 0 || idx >= presetTotal()) idx = 0;
  if (idx >= THEME_PRESETS) {
    /* A file theme is a full palette, so it goes in wholesale — no chroma
     * boost, because whoever wrote the file already chose the colours. */
    currentPreset = idx;
    applyPalette(gCardThemes[idx - THEME_PRESETS].role);
    applyBgLight();
    return;
  }
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
  /* Last, and after applyBgLight: both derived values read BG, TEXT and DIM
   * in their FINAL form, and light mode rewrites all three. */
  deriveSurface();
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
  if (idx < 0 || idx >= presetTotal()) idx = 0;
  if (idx >= THEME_PRESETS) return gCardThemes[idx - THEME_PRESETS].name;
  return kPresets[idx].name;
}

/* Parse one "key = RRGGBB" line into the role table. */
static bool themeRole(const String &key, const String &val, CardTheme &t) {
  static const char *kKeys[COLOR_ROLES] = {"bg",   "chrome", "text", "dim",
                                           "panel", "good",  "warn", "crit",
                                           "info", "accent"};
  for (int i = 0; i < COLOR_ROLES; i++) {
    if (key != kKeys[i]) continue;
    long v = strtol(val.c_str(), nullptr, 16);
    t.role[i] = rgb((uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v);
    return true;
  }
  return false;
}

int loadCardThemes(::SdStore *sd) {
  gCardThemeCount = 0;
  if (!sd || !sd->ok()) return 0;
  for (int slot = 0; slot < CARD_THEMES_MAX; slot++) {
    char path[40];
    snprintf(path, sizeof(path), "/themes/%d.thm", slot + 1);
    String text;
    if (!sd->readAll(path, text, 1024) || !text.length()) continue;
    CardTheme t{};
    snprintf(t.name, sizeof(t.name), "файл %d", slot + 1);
    int roles = 0, start = 0;
    while (start < text.length()) {
      int nl = text.indexOf('\n', start);
      String line = (nl < 0) ? text.substring(start) : text.substring(start, nl);
      start = (nl < 0) ? text.length() : nl + 1;
      line.replace("\r", "");
      line.trim();
      if (!line.length() || line.startsWith("#")) continue;
      int eq = line.indexOf('=');
      if (eq <= 0) continue;
      String key = line.substring(0, eq), val = line.substring(eq + 1);
      key.trim();
      key.toLowerCase();
      val.trim();
      if (key == "name") {
        snprintf(t.name, sizeof(t.name), "%s", val.c_str());
        continue;
      }
      if (themeRole(key, val, t)) roles++;
    }
    /* All ten or none: a half-specified palette would leave roles black and
     * look like a broken screen rather than a theme. */
    if (roles != COLOR_ROLES) {
      Serial.printf("[THEME] %s: %d/%d roles - skipped\n", path, roles,
                    COLOR_ROLES);
      continue;
    }
    gCardThemes[gCardThemeCount++] = t;
    Serial.printf("[THEME] loaded '%s' from %s\n", t.name, path);
  }
  return gCardThemeCount;
}

/* Rain or snow drifting behind the content, driven by the real WMO code from
 * the weather payload. Deterministic hash per particle, so there is no state to
 * keep and nothing to allocate — the same trick the screensaver starfield uses.
 * WMO: 51-67 drizzle/rain, 80-82 showers, 95-99 thunder; 71-77 + 85-86 snow. */
static void weatherParticles(LGFX_Sprite &g, int y0, int y1) {
  int w = weatherCode;
  bool rain = (w >= 51 && w <= 67) || (w >= 80 && w <= 82) || (w >= 95);
  bool snow = (w >= 71 && w <= 77) || w == 85 || w == 86;
  if (!rain && !snow) return;
  int span = y1 - y0;
  if (span < 8) return;
  const int kN = 26;
  for (int i = 0; i < kN; i++) {
    uint32_t h = (uint32_t)(i * 2246822519u) ^ 0x9E3779B9u;
    int x = (int)((h >> 9) % 320);
    int speed = snow ? (5 + (int)(h % 4)) : (22 + (int)(h % 12));
    int y = y0 + (int)(((h >> 3) + nowMs / (unsigned long)(40 / speed + 1)) %
                       (unsigned long)span);
    if (snow) {
      /* drift sideways so it falls like snow rather than like slow rain */
      x = (x + (int)(8 * sinf((nowMs / 900.0f) + i))) % 320;
      if (x < 0) x += 320;
      g.drawPixel(x, y, lerp565(BG, TEXT, 120));
      if ((i & 3) == 0) g.drawPixel(x + 1, y, lerp565(BG, TEXT, 70));
    } else {
      g.drawFastVLine(x, y, 3 + (int)(h % 3), lerp565(BG, INFO, 95));
    }
  }
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
    weatherParticles(g, y0, y1); /* weather shows even on the plain background */
    return;
  }
  /* the backdrop LIVES: busier PC sweeps faster & brighter, an alert turns it
   * red with a second counter-sweep. */
  uint16_t tint = reactAlert ? CRIT : (bgLight ? DIM : INFO);
  uint16_t sheenC = reactAlert ? CRIT : (bgLight ? rgb(0, 0, 0) : TEXT);
  int sweepDiv = 13 - energy / 12; /* 13..~5: faster under load */
  if (sweepDiv < 4) sweepDiv = 4;
  if (bgStyle == 1) {
    /* 5/255 is two percent: scanlines were being drawn and could not be seen.
     * The sweep carried the whole effect and the texture under it was a
     * theoretical exercise. */
    uint16_t sl = lerp565(BG, tint, (bgLight ? 26 : 22) + energy / 18);
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
    /* Drifting graticule — drifts faster and brightens with load.
     *
     * This took two goes and the first one fixed the wrong thing. The blend
     * started at 12/255, under five percent, so the obvious diagnosis was
     * contrast; raising it to 70 bought a measured 2:1 and the grid was still
     * invisible on glass.
     *
     * Because contrast was only half of it. Each node was ONE PIXEL, and a
     * lone pixel has almost no perceived brightness whatever its ratio says:
     * the eye integrates over an area, and a contrast figure assumes an
     * element large enough to have one. WCAG's numbers are written for
     * legible-sized marks, not for single dots, and applying them to a dot is
     * measuring the right quantity on the wrong object.
     *
     * So the node is now a small cross — five pixels instead of one, and a
     * shape that reads as deliberate structure rather than as noise in the
     * matrix. With the blend at 120 (about 4:1) it is unmistakably a grid and
     * still sits behind the data rather than competing with it. Spacing goes
     * to 26 because five-pixel nodes at 18 px would be busy. */
    uint16_t node = lerp565(BG, tint, (bgLight ? 140 : 120) + energy / 8);
    int ds = 90 - energy / 2;
    if (ds < 28) ds = 28;
    const int step = 26;
    int ox = (int)((nowMs / ds) % step), oy = (int)((nowMs / (ds + 40)) % step);
    for (int y = y0 + oy; y < y1; y += step) {
      for (int x = ox; x < 320; x += step) {
        /* clipped by hand: the sprite would clip anyway, but the top rows of
         * the content band belong to the status bar and must stay clean */
        if (y - 1 >= y0 && y + 1 < y1) g.drawFastVLine(x, y - 1, 3, node);
        g.drawFastHLine(x - 1, y, 3, node);
      }
    }
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

/* WCAG relative luminance of a 565 colour. Called only when the palette
 * changes, so the powf() costs nothing at any frame rate. */
static float relLum(uint16_t v) {
  int c[3] = {((v >> 11) & 0x1F) << 3, ((v >> 5) & 0x3F) << 2,
              (v & 0x1F) << 3};
  static const float w[3] = {0.2126f, 0.7152f, 0.0722f};
  float L = 0.0f;
  for (int i = 0; i < 3; i++) {
    float x = c[i] / 255.0f;
    x = (x <= 0.03928f) ? x / 12.92f : powf((x + 0.055f) / 1.055f, 2.4f);
    L += w[i] * x;
  }
  return L;
}

static float contrastOf(uint16_t a, uint16_t b) {
  float la = relLum(a), lb = relLum(b);
  if (la < lb) {
    float t = la;
    la = lb;
    lb = t;
  }
  return (la + 0.05f) / (lb + 0.05f);
}

static void deriveSurface() {
  /* Step one: lift BG until the tile is actually a tile.
   *
   * Toward white on a dark theme, toward black on a light one - the direction
   * has to follow the background or the "raised" surface sinks, and lifting a
   * white background toward white produces white. The STEP has to follow it
   * too: contrast is a ratio, so the same absolute step is worth about a
   * third as much when both sides sit near white. Measured off the board, one
   * fixed lift gave 2.41:1 on the dark theme and 1.12:1 on the light one -
   * invisible, which is the whole feature gone on half the palettes.
   *
   * Solving for the ratio instead of picking a step removes the question. */
  uint16_t target = bgLight ? 0x0000 : 0xFFFF;
  SURFACE = lerp565(BG, target, 96);
  for (int lift = 12; lift <= 96; lift += 4) {
    uint16_t cand = lerp565(BG, target, lift);
    if (contrastOf(cand, BG) >= 1.25f) {
      SURFACE = cand;
      break;
    }
  }
  /* Step two: the label. Plain DIM was solved against BG and against PANEL,
   * and a tile is neither - on 15 of 22 presets DIM lands between 3.9 and 4.4
   * against the surface, under the 4.5 a small caption needs. Blended toward
   * TEXT until it clears, which is the least loud colour that still reads. */
  SURFACE_DIM = TEXT;
  for (int k = 0; k <= 255; k += 8) {
    uint16_t cand = lerp565(DIM, TEXT, k);
    if (contrastOf(cand, SURFACE) >= 4.5f) {
      SURFACE_DIM = cand;
      break;
    }
  }
}

Rect panelM(LGFX_Sprite &g, int x, int y, int w, int h, const char *title,
            uint16_t titleColor) {
  g.fillRoundRect(x, y, w, h, 4, SURFACE);
  Rect c = {x + MPADX, y + MPADY, w - 2 * MPADX, h - 2 * MPADY};
  if (title && title[0]) {
    g.setFont(&F_TEXT);
    g.setTextSize(1);
    /* Lowercase, dim, and small: a label is the least important thing in the
     * tile and used to be drawn in chrome, competing with the number it was
     * introducing. */
    textAt(g, c.x, c.y, title, titleColor ? titleColor : SURFACE_DIM);
    int lh = g.fontHeight() + 2;
    c.y += lh;
    c.h -= lh;
  }
  return c;
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
