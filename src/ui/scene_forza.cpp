/* Nocturne C6 — FORZA HUD, RPM-first fullscreen dash.
 * Zones (320x172): shift lamp 2..26 | mid band 32..120: gear box 4..92,
 * RPM hero 96..236, pedals 240..284, slip+fuel 288..318 | info row 127..166.
 * Big-font glyphs are placed via inkTop(): the u8g2 logisoso line box is
 * taller than the visible digits, so we anchor the INK, not the line. */
#include <WiFi.h>

#include "core/config.h"
#include "ui/Scenes.h"

using namespace theme;
using namespace widgets;

namespace scenes {

static const char *gearStr(int g) {
  static char buf[12];
  if (g == 0) return "R";
  if (g >= 11) return "N";
  snprintf(buf, sizeof(buf), "%d", g);
  return buf;
}

static uint16_t slipColor(float s) {
  if (s >= 1.0f) return CRIT;
  if (s >= 0.5f) return WARN;
  return GOOD;
}

/* y to pass to textAt so the visible glyph TOP lands at wantTop.
 * inkH = the glyph height we expect (64 for F_HUGE size2). */
static int inkTop(LGFX_Sprite &g, int wantTop, int inkH) {
  int off = g.fontHeight() - inkH;
  if (off < 0) off = 0;
  return wantTop - off / 2; /* leading splits above/below the ink */
}

/* one cell of the bottom info row: small caption, 20px value */
static void infoCell(LGFX_Sprite &g, int x, int w, const char *cap,
                     const char *val, uint16_t c) {
  g.setFont(&F_TEXT);
  textCenter(g, x + w / 2, 132, cap, DIM);
  g.setFont(&F_MED);
  textCenter(g, x + w / 2, 144, val, c);
}

void drawForza(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  const ForzaState *fz = ui.forza;
  char v[48];

  if (!ui.forzaLive || !fz) {
    g.setFont(&F_MED);
    bool blink = (ui.now / 700) & 1;
    textCenter(g, NOCT_W / 2, 36, "FORZA HUD", ORANGE);
    textCenter(g, NOCT_W / 2, 62, "ЖДУ ТЕЛЕМЕТРИЮ", blink ? TEXT : DIM);
    int cx = NOCT_W / 2 - 20 + (int)(sinf(ui.now / 300.0f) * 30);
    g.fillRoundRect(cx, 96, 40, 10, 3, ORANGE_DIM);
    g.fillRoundRect(cx + 8, 89, 22, 9, 3, ORANGE_DIM);
    g.fillCircle(cx + 9, 107, 4, DIM);
    g.fillCircle(cx + 31, 107, 4, DIM);
    g.setFont(&F_TEXT);
    textCenter(g, NOCT_W / 2, 122, "Forza: HUD > Data Out > IP устройства:",
               DIM);
    snprintf(v, sizeof(v), "%s : %d", WiFi.localIP().toString().c_str(),
             NOCT_FORZA_UDP_PORT);
    g.setFont(&F_MED);
    textCenter(g, NOCT_W / 2, 140, v, INFO);
    return;
  }

  const ForzaState &f = *fz;
  float pct = f.rpmPct();
  bool shiftNow = pct >= NOCT_FORZA_SHIFT_PCT;
  bool flash = (ui.now / 80) & 1;

  /* ── THE shift lamp: 12 fat segments, 24px, corner-safe insets ── */
  const int segs = 12, sx = 10, sw = (NOCT_W - 20) / segs;
  float lo = 0.40f;
  int lit =
      pct <= lo ? 0 : (int)((pct - lo) / (NOCT_FORZA_SHIFT_PCT - lo) * segs);
  if (lit > segs) lit = segs;
  for (int i = 0; i < segs; i++) {
    uint16_t c = i < 5 ? GOOD : (i < 9 ? WARN : CRIT);
    bool on = shiftNow ? flash : (i < lit);
    int x = sx + i * sw;
    if (on) {
      g.fillRect(x + 1, 2, sw - 2, 24, c);
    } else {
      g.drawRect(x + 1, 2, sw - 2, 24, ORANGE_DIM);
      g.drawFastHLine(x + 2, 24, sw - 4, c);
    }
  }

  /* ── gear box (left), ink-anchored glyph + roll animation ── */
  static int curGear = -1, prevGear = -1;
  static unsigned long gearAt = 0;
  if (f.gear != curGear) {
    prevGear = curGear;
    curGear = f.gear;
    if (prevGear != -1) gearAt = ui.now;
  }
  float gt = gearAt ? (ui.now - gearAt) / 220.0f : 1.0f;
  if (gt > 1.0f) gt = 1.0f;
  bool animating = gt < 1.0f && prevGear != -1;
  uint16_t frame = animating ? ACCENT
                   : (shiftNow && flash ? CRIT : ORANGE_DIM);
  g.drawRoundRect(4, 32, 88, 88, 8, frame);
  g.drawRoundRect(5, 33, 86, 86, 8, animating ? ACCENT : PANEL);
  g.setFont(&F_HUGE);
  g.setTextSize(2);
  /* center the 64px ink inside the 88px box: ink top = 32+(88-64)/2 = 44 */
  int gy = inkTop(g, 44, 64);
  if (animating) {
    int dir = (curGear > prevGear && prevGear != 0) ? 1 : -1;
    int off = (int)(gt * 84);
    g.setClipRect(8, 36, 80, 80);
    textCenter(g, 48, gy - dir * off, gearStr(prevGear), DIM);
    textCenter(g, 48, gy + dir * (84 - off), gearStr(curGear), ORANGE);
    g.clearClipRect();
  } else {
    textCenter(g, 48, gy, gearStr(curGear),
               shiftNow ? (flash ? CRIT : TEXT) : ORANGE);
  }
  g.setTextSize(1);

  /* ── hero RPM: 64px ink at 32..96, caption under ── */
  int rpm = (int)f.rpm;
  if (rpm > 99999) rpm = 99999;
  snprintf(v, sizeof(v), "%d", rpm);
  g.setFont(&F_HUGE);
  g.setTextSize(2);
  uint16_t rc = shiftNow ? (flash ? CRIT : TEXT)
                         : (pct >= 0.80f ? WARN : TEXT);
  textCenter(g, 166, inkTop(g, 34, 64), v, rc);
  g.setTextSize(1);
  g.setFont(&F_TEXT);
  if (f.maxRpm > 1000) /* game menus send no engine data */
    snprintf(v, sizeof(v), "ОБОРОТЫ / MAX %d", (int)f.maxRpm);
  else
    snprintf(v, sizeof(v), "ОБОРОТЫ");
  textCenter(g, 166, 104, v, DIM);

  /* ── pedals (240..284) ── */
  vBar(g, 240, 32, 19, 80, f.throttle * 100 / 255, GOOD);
  vBar(g, 264, 32, 19, 80, f.brake * 100 / 255, CRIT);
  g.setFont(&F_TEXT);
  textCenter(g, 249, 116, "ГАЗ", GOOD);
  textCenter(g, 273, 116, "ТРМ", CRIT);

  /* ── slip 2x2 (bigger, on-screen) + fuel (288..318) ── */
  static const int tix[4] = {289, 304, 289, 304};
  static const int tiy[4] = {32, 32, 58, 58};
  for (int i = 0; i < 4; i++) {
    uint16_t c = slipColor(f.slip[i]);
    bool severe = f.slip[i] >= 1.0f;
    bool filled = severe ? flash : f.slip[i] >= 0.5f;
    if (filled)
      g.fillRoundRect(tix[i], tiy[i], 13, 23, 2, c);
    else
      g.drawRoundRect(tix[i], tiy[i], 13, 23, 2, c);
  }
  g.setFont(&F_TEXT);
  textCenter(g, 303, 84, "SLIP", DIM);
  int fuelPct = (int)(f.fuel * 100);
  hBar(g, 288, 96, 30, 14, fuelPct, fuelPct < 15 ? CRIT : ACCENT);
  textCenter(g, 303, 114, "FUEL", DIM);

  /* ── info row: speed | boost | power | race — corner-safe grid ── */
  g.drawFastHLine(10, 127, NOCT_W - 20, PANEL);
  snprintf(v, sizeof(v), "%d", (int)(f.speedKmh + 0.5f));
  infoCell(g, 10, 76, "КМ/Ч", v, TEXT);
  if (f.boostPsi > 0.3f)
    snprintf(v, sizeof(v), "%.1f", f.boostPsi * 0.0689f);
  else
    snprintf(v, sizeof(v), "-");
  infoCell(g, 86, 76, "БУСТ BAR", v, INFO);
  int hp = (int)(f.powerW / 745.7f);
  snprintf(v, sizeof(v), "%d", hp > 0 ? hp : 0);
  infoCell(g, 162, 76, "Л.С.", v, ACCENT);
  if (f.lap > 0 || f.racePos > 0)
    snprintf(v, sizeof(v), "%d/%d", f.lap + 1, f.racePos);
  else
    snprintf(v, sizeof(v), "-");
  infoCell(g, 238, 72, "КРУГ/МЕСТО", v, TEXT);

  /* ── overlays ── */
  if (!f.raceOn) {
    g.setFont(&F_MED);
    g.fillRoundRect(116, 50, 104, 26, 4, PANEL);
    g.drawRoundRect(116, 50, 104, 26, 4, ACCENT);
    textCenter(g, 168, 53, "ПАУЗА", ACCENT);
  }
  if (ui.st.alertActive && ((ui.now / 300) & 1)) {
    g.fillTriangle(160, 124, 176, 124, 168, 112, CRIT);
  }
}

} // namespace scenes
