/* Nocturne C6 — FORZA dash: F1-style shift lights, huge gear, speed,
 * throttle/brake, tire slip, fuel. Auto-enters when telemetry arrives. */
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

void drawForza(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  const ForzaState *fz = ui.forza;
  char v[40];

  if (!ui.forzaLive || !fz) {
    /* waiting screen: where to point the game */
    g.setFont(&F_MED);
    g.setTextSize(1);
    bool blink = (ui.now / 700) & 1;
    textCenter(g, NOCT_W / 2, 48, "ЖДУ ТЕЛЕМЕТРИЮ", blink ? ORANGE : DIM);
    g.setTextSize(1);
    /* little bouncing car */
    int cx = NOCT_W / 2 - 20 + (int)(sinf(ui.now / 300.0f) * 30);
    g.fillRoundRect(cx, 84, 40, 10, 3, ORANGE_DIM);
    g.fillRoundRect(cx + 8, 77, 22, 9, 3, ORANGE_DIM);
    g.fillCircle(cx + 9, 95, 4, DIM);
    g.fillCircle(cx + 31, 95, 4, DIM);
    g.setFont(&F_SMALL);
    textCenter(g, NOCT_W / 2, 112, "Forza: HUD > Data Out", DIM);
    snprintf(v, sizeof(v), "IP: %s  порт: %d",
             WiFi.localIP().toString().c_str(), NOCT_FORZA_UDP_PORT);
    g.setFont(&F_TEXT);
    textCenter(g, NOCT_W / 2, 126, v, TEXT);
    return;
  }

  const ForzaState &f = *fz;
  float pct = f.rpmPct();
  bool shiftNow = pct >= NOCT_FORZA_SHIFT_PCT;
  bool flash = (ui.now / 80) & 1;

  /* ── shift light strip: 12 segments, F1 style ── */
  const int segs = 12, sx = 4, sw = (NOCT_W - 8) / segs;
  float lo = 0.45f;
  int lit = pct <= lo ? 0 : (int)((pct - lo) / (NOCT_FORZA_SHIFT_PCT - lo) *
                                  segs);
  if (lit > segs) lit = segs;
  for (int i = 0; i < segs; i++) {
    uint16_t c = i < 5 ? GOOD : (i < 9 ? WARN : CRIT);
    bool on = shiftNow ? flash : (i < lit);
    int x = sx + i * sw;
    if (on)
      g.fillRect(x + 1, 23, sw - 2, 13, c);
    else
      g.drawRect(x + 1, 23, sw - 2, 13, PANEL);
  }

  /* rpm line: rpm left, boost center, redline right */
  g.setFont(&F_SMALL);
  snprintf(v, sizeof(v), "%d RPM", (int)f.rpm);
  textAt(g, 6, 39, v, shiftNow && flash ? CRIT : DIM);
  if (f.boostPsi > 0.3f) {
    snprintf(v, sizeof(v), "BOOST %.2f bar", f.boostPsi * 0.0689f);
    textCenter(g, NOCT_W / 2, 39, v, INFO);
  }
  snprintf(v, sizeof(v), "max %d", (int)f.maxRpm);
  textRight(g, NOCT_W - 6, 39, v, PANEL);

  /* ── gear box ── */
  uint16_t gc = shiftNow ? (flash ? CRIT : TEXT) : ORANGE;
  g.drawRoundRect(8, 50, 76, 76, 6, shiftNow && flash ? CRIT : ORANGE_DIM);
  g.drawRoundRect(9, 51, 74, 74, 6, shiftNow && flash ? CRIT : PANEL);
  g.setFont(&F_HUGE);
  g.setTextSize(2);
  textCenter(g, 46, 56, gearStr(f.gear), gc);
  g.setTextSize(1);
  /* live horsepower under the gear (positive only — engine braking hides) */
  g.setFont(&F_TEXT);
  int hp = (int)(f.powerW / 745.7f);
  if (hp > 0) {
    snprintf(v, sizeof(v), "%d ЛС", hp);
    textCenter(g, 46, 130, v, ORANGE);
  } else {
    g.setFont(&F_SMALL);
    textCenter(g, 46, 130, "ПЕРЕДАЧА", DIM);
  }

  /* ── speed ── */
  int kmh = (int)(f.speedKmh + 0.5f);
  if (kmh < 0) kmh = 0;
  snprintf(v, sizeof(v), "%d", kmh);
  g.setFont(&F_HUGE);
  g.setTextSize(2);
  textCenter(g, 162, 54, v, TEXT);
  g.setTextSize(1);
  g.setFont(&F_TEXT);
  textCenter(g, 162, 122, "КМ/Ч", DIM);

  /* lap / position */
  if (f.lap > 0 || f.racePos > 0) {
    g.setFont(&F_TEXT);
    snprintf(v, sizeof(v), "КРУГ %d   МЕСТО %d", f.lap + 1, f.racePos);
    textCenter(g, 162, 136, v, ORANGE);
  }

  /* ── throttle / brake vertical bars ── */
  vBar(g, 244, 50, 16, 76, f.throttle * 100 / 255, GOOD);
  vBar(g, 268, 50, 16, 76, f.brake * 100 / 255, CRIT);
  g.setFont(&F_SMALL);
  textCenter(g, 252, 130, "ГАЗ", GOOD);
  textCenter(g, 276, 130, "ТРМ", CRIT);

  /* ── tire slip 2x2 (car layout) ── */
  static const int tix[4] = {292, 306, 292, 306};
  static const int tiy[4] = {52, 52, 72, 72};
  for (int i = 0; i < 4; i++) {
    uint16_t c = slipColor(f.slip[i]);
    if (f.slip[i] >= 1.0f && flash)
      g.fillRoundRect(tix[i], tiy[i], 12, 18, 2, c);
    else if (f.slip[i] >= 0.5f)
      g.fillRoundRect(tix[i], tiy[i], 12, 18, 2, c);
    else
      g.drawRoundRect(tix[i], tiy[i], 12, 18, 2, c);
  }
  g.setFont(&F_SMALL);
  textCenter(g, 305, 94, "SLIP", DIM);

  /* ── fuel ── */
  int fuelPct = (int)(f.fuel * 100);
  g.setFont(&F_SMALL);
  textAt(g, 292, 108, "FUEL", DIM);
  vBar(g, 296, 118, 12, 28, fuelPct, fuelPct < 15 ? CRIT : ORANGE);
}

} // namespace scenes
