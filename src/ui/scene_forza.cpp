/* Nocturne C6 — FORZA dash: F1 shift lights, 64px gear & speed, live HP,
 * boost, throttle/brake, tire slip, fuel. Auto-enters on telemetry; also
 * reachable from the menu ("Forza HUD"). */
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
  char v[48];

  if (!ui.forzaLive || !fz) {
    /* waiting screen: where to point the game */
    g.setFont(&F_MED);
    bool blink = (ui.now / 700) & 1;
    textCenter(g, NOCT_W / 2, 42, "ЖДУ ТЕЛЕМЕТРИЮ", blink ? ORANGE : DIM);
    /* little bouncing car */
    int cx = NOCT_W / 2 - 20 + (int)(sinf(ui.now / 300.0f) * 30);
    g.fillRoundRect(cx, 78, 40, 10, 3, ORANGE_DIM);
    g.fillRoundRect(cx + 8, 71, 22, 9, 3, ORANGE_DIM);
    g.fillCircle(cx + 9, 89, 4, DIM);
    g.fillCircle(cx + 31, 89, 4, DIM);
    g.setFont(&F_TEXT);
    textCenter(g, NOCT_W / 2, 102, "Forza: HUD > Data Out > IP устройства:",
               DIM);
    snprintf(v, sizeof(v), "%s : %d", WiFi.localIP().toString().c_str(),
             NOCT_FORZA_UDP_PORT);
    g.setFont(&F_MED);
    textCenter(g, NOCT_W / 2, 118, v, INFO);
    return;
  }

  const ForzaState &f = *fz;
  if (!f.raceOn) { /* game menu / replay: data frozen */
    g.setFont(&F_MED);
    g.fillRoundRect(118, 24, 84, 24, 4, PANEL);
    g.drawRoundRect(118, 24, 84, 24, 4, ACCENT);
    textCenter(g, 160, 27, "ПАУЗА", ACCENT);
  }
  float pct = f.rpmPct();
  bool shiftNow = pct >= NOCT_FORZA_SHIFT_PCT;
  bool flash = (ui.now / 80) & 1;

  /* ── shift light strip: 12 segments, F1 style ── */
  const int segs = 12, sx = 4, sw = (NOCT_W - 8) / segs;
  float lo = 0.45f;
  int lit =
      pct <= lo ? 0 : (int)((pct - lo) / (NOCT_FORZA_SHIFT_PCT - lo) * segs);
  if (lit > segs) lit = segs;
  for (int i = 0; i < segs; i++) {
    uint16_t c = i < 5 ? GOOD : (i < 9 ? WARN : CRIT);
    bool on = shiftNow ? flash : (i < lit);
    int x = sx + i * sw;
    if (on)
      g.fillRect(x + 1, 22, sw - 2, 14, c);
    else
      g.drawRect(x + 1, 22, sw - 2, 14, PANEL);
  }

  /* ── data line: RPM | BOOST | HP — all big ── */
  g.setFont(&F_MED);
  snprintf(v, sizeof(v), "%d", (int)f.rpm);
  int rw = g.textWidth(v);
  textAt(g, 6, 40, v, shiftNow && flash ? CRIT : TEXT);
  g.setFont(&F_TEXT);
  textAt(g, 10 + rw, 46, "RPM", DIM);
  if (f.boostPsi > 0.3f) {
    g.setFont(&F_MED);
    snprintf(v, sizeof(v), "%.1fbar", f.boostPsi * 0.0689f);
    textCenter(g, NOCT_W / 2 + 10, 40, v, INFO);
  }
  int hp = (int)(f.powerW / 745.7f);
  if (hp > 0) {
    g.setFont(&F_MED);
    snprintf(v, sizeof(v), "%d ЛС", hp);
    textRight(g, NOCT_W - 6, 40, v, ACCENT);
  }

  /* ── gear box (left) ── */
  uint16_t gc = shiftNow ? (flash ? CRIT : TEXT) : ORANGE;
  g.drawRoundRect(8, 62, 76, 66, 6, shiftNow && flash ? CRIT : ORANGE_DIM);
  g.drawRoundRect(9, 63, 74, 64, 6, shiftNow && flash ? CRIT : PANEL);
  g.setFont(&F_HUGE);
  g.setTextSize(2);
  textCenter(g, 46, 63, gearStr(f.gear), gc);
  g.setTextSize(1);

  /* ── speed (center, 64px) ── */
  int kmh = (int)(f.speedKmh + 0.5f);
  if (kmh < 0) kmh = 0;
  snprintf(v, sizeof(v), "%d", kmh);
  g.setFont(&F_HUGE);
  g.setTextSize(2);
  textCenter(g, 162, 62, v, TEXT);
  g.setTextSize(1);

  /* bottom line: units + race position */
  g.setFont(&F_MED);
  if (f.lap > 0 || f.racePos > 0)
    snprintf(v, sizeof(v), "КМ/Ч  КРУГ %d  МЕСТО %d", f.lap + 1, f.racePos);
  else
    snprintf(v, sizeof(v), "КМ/Ч");
  textCenter(g, 162, 131, v, ORANGE);

  /* ── throttle / brake (right) ── */
  vBar(g, 244, 62, 16, 62, f.throttle * 100 / 255, GOOD);
  vBar(g, 266, 62, 16, 62, f.brake * 100 / 255, CRIT);
  g.setFont(&F_TEXT);
  textCenter(g, 252, 128, "ГАЗ", GOOD);
  textCenter(g, 274, 128, "ТРМ", CRIT);

  /* ── tire slip 2x2 (car layout) + fuel (far right) ── */
  static const int tix[4] = {292, 306, 292, 306};
  static const int tiy[4] = {62, 62, 82, 82};
  for (int i = 0; i < 4; i++) {
    uint16_t c = slipColor(f.slip[i]);
    bool severe = f.slip[i] >= 1.0f; /* severe slip blinks */
    bool filled = severe ? flash : f.slip[i] >= 0.5f;
    if (filled)
      g.fillRoundRect(tix[i], tiy[i], 12, 18, 2, c);
    else
      g.drawRoundRect(tix[i], tiy[i], 12, 18, 2, c);
  }
  g.setFont(&F_TEXT);
  textCenter(g, 305, 102, "SLIP", DIM);

  int fuelPct = (int)(f.fuel * 100);
  vBar(g, 294, 114, 14, 30, fuelPct, fuelPct < 15 ? CRIT : ACCENT);
  textCenter(g, 301, 145, "FUEL", DIM);
}

} // namespace scenes
