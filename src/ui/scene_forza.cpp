/* Nocturne C6 — FORZA HUD, RPM-first fullscreen dash.
 * Hierarchy: shift lamp (24px strip) > 64px RPM > animated gear > pedals,
 * slip, fuel > info row (speed / boost / hp / lap·pos).
 * Untouchable: no chrome, no alert takeover (corner triangle only).
 * Gear changes play a 220ms slide animation with a border flash. */
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

  /* ── THE shift lamp: 12 fat segments, 24px, edge to edge ── */
  const int segs = 12, sx = 4, sw = (NOCT_W - 8) / segs;
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
      g.drawFastHLine(x + 2, 24, sw - 4, i < 5 ? GOOD : (i < 9 ? WARN : CRIT));
    }
  }

  /* ── hero RPM: 64px digits right of the gear box ── */
  int rpm = (int)f.rpm;
  if (rpm > 99999) rpm = 99999;
  snprintf(v, sizeof(v), "%d", rpm);
  g.setFont(&F_HUGE);
  g.setTextSize(2);
  uint16_t rc = shiftNow ? (flash ? CRIT : TEXT)
                         : (pct >= 0.80f ? WARN : TEXT);
  textCenter(g, 168, 34, v, rc);
  g.setTextSize(1);
  g.setFont(&F_TEXT);
  snprintf(v, sizeof(v), "ОБОРОТЫ  /  MAX %d", (int)f.maxRpm);
  textCenter(g, 168, 102, v, DIM);

  /* ── gear box with a shift animation (slide + border flash) ── */
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
  g.drawRoundRect(6, 32, 88, 80, 8, frame);
  g.drawRoundRect(7, 33, 86, 78, 8, animating ? ACCENT : PANEL);
  g.setFont(&F_HUGE);
  g.setTextSize(2);
  if (animating) {
    /* upshift: digit rolls up; downshift: rolls down */
    int dir = (curGear > prevGear && prevGear != 0) ? 1 : -1;
    int off = (int)(gt * 76);
    g.setClipRect(10, 36, 80, 72);
    textCenter(g, 50, 40 - dir * off, gearStr(prevGear),
               DIM);
    textCenter(g, 50, 40 + dir * (76 - off), gearStr(curGear), ORANGE);
    g.clearClipRect();
  } else {
    textCenter(g, 50, 40, gearStr(curGear),
               shiftNow ? (flash ? CRIT : TEXT) : ORANGE);
  }
  g.setTextSize(1);
  g.setFont(&F_TEXT);
  textCenter(g, 50, 116, "ПЕРЕДАЧА", DIM);

  /* ── pedals ── */
  vBar(g, 248, 32, 16, 80, f.throttle * 100 / 255, GOOD);
  vBar(g, 272, 32, 16, 80, f.brake * 100 / 255, CRIT);
  g.setFont(&F_TEXT);
  textCenter(g, 257, 116, "ГАЗ", GOOD);
  textCenter(g, 281, 116, "ТРМ", CRIT);

  /* ── slip 2x2 + fuel, far right ── */
  static const int tix[4] = {296, 310, 296, 310};
  static const int tiy[4] = {32, 32, 56, 56};
  for (int i = 0; i < 4; i++) {
    uint16_t c = slipColor(f.slip[i]);
    bool severe = f.slip[i] >= 1.0f;
    bool filled = severe ? flash : f.slip[i] >= 0.5f;
    if (filled)
      g.fillRoundRect(tix[i], tiy[i], 11, 21, 2, c);
    else
      g.drawRoundRect(tix[i], tiy[i], 11, 21, 2, c);
  }
  g.setFont(&F_TEXT);
  textCenter(g, 304, 80, "SLIP", DIM);
  int fuelPct = (int)(f.fuel * 100);
  vBar(g, 298, 90, 16, 22, fuelPct, fuelPct < 15 ? CRIT : ACCENT);
  textCenter(g, 306, 116, "FUEL", DIM);

  /* ── info row: speed | boost | power | race — one aligned grid ── */
  g.drawFastHLine(4, 127, NOCT_W - 8, PANEL);
  snprintf(v, sizeof(v), "%d", (int)(f.speedKmh + 0.5f));
  infoCell(g, 4, 80, "КМ/Ч", v, TEXT);
  if (f.boostPsi > 0.3f)
    snprintf(v, sizeof(v), "%.1f", f.boostPsi * 0.0689f);
  else
    snprintf(v, sizeof(v), "-");
  infoCell(g, 84, 80, "БУСТ BAR", v, INFO);
  int hp = (int)(f.powerW / 745.7f);
  snprintf(v, sizeof(v), "%d", hp > 0 ? hp : 0);
  infoCell(g, 164, 80, "Л.С.", v, ACCENT);
  if (f.lap > 0 || f.racePos > 0)
    snprintf(v, sizeof(v), "%d/%d", f.lap + 1, f.racePos);
  else
    snprintf(v, sizeof(v), "-");
  infoCell(g, 244, 72, "КРУГ/МЕСТО", v, TEXT);

  /* ── overlays ── */
  if (!f.raceOn) {
    g.setFont(&F_MED);
    g.fillRoundRect(118, 52, 100, 26, 4, PANEL);
    g.drawRoundRect(118, 52, 100, 26, 4, ACCENT);
    textCenter(g, 168, 55, "ПАУЗА", ACCENT);
  }
  if (ui.st.alertActive && ((ui.now / 300) & 1)) {
    /* free spot between the rpm caption and the info divider */
    g.fillTriangle(160, 124, 176, 124, 168, 112, CRIT);
  }
}

} // namespace scenes
