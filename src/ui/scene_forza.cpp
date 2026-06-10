/* Nocturne C6 — FORZA HUD: a fullscreen racing dash (no status bar, no
 * footer — SceneManager skips the chrome for this scene). Untouchable:
 * alerts, carousel and dimming never hijack it. Auto-enters on telemetry;
 * also reachable from the menu. SHORT press leaves to the den. */
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
    textCenter(g, NOCT_W / 2, 36, "FORZA HUD", ORANGE);
    textCenter(g, NOCT_W / 2, 62, "ЖДУ ТЕЛЕМЕТРИЮ", blink ? TEXT : DIM);
    /* little bouncing car */
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

  /* ── shift light strip across the very top, 18 px tall ── */
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
      g.fillRect(x + 1, 2, sw - 2, 18, c);
    else
      g.drawRect(x + 1, 2, sw - 2, 18, PANEL);
  }

  /* ── data line: RPM | BOOST | live HP ── */
  g.setFont(&F_MED);
  snprintf(v, sizeof(v), "%d", (int)f.rpm);
  int rw = g.textWidth(v);
  textAt(g, 6, 26, v, shiftNow && flash ? CRIT : TEXT);
  g.setFont(&F_TEXT);
  textAt(g, 10 + rw, 32, "RPM", DIM);
  if (f.boostPsi > 0.3f) {
    g.setFont(&F_MED);
    snprintf(v, sizeof(v), "%.1fbar", f.boostPsi * 0.0689f);
    textCenter(g, NOCT_W / 2 + 4, 26, v, INFO);
  }
  int hp = (int)(f.powerW / 745.7f);
  if (hp > 0) {
    g.setFont(&F_MED);
    snprintf(v, sizeof(v), "%d ЛС", hp);
    textRight(g, NOCT_W - 6, 26, v, ACCENT);
  }

  /* ── gear box (left, big) ── */
  uint16_t gc = shiftNow ? (flash ? CRIT : TEXT) : ORANGE;
  g.drawRoundRect(6, 52, 88, 92, 8, shiftNow && flash ? CRIT : ORANGE_DIM);
  g.drawRoundRect(7, 53, 86, 90, 8, shiftNow && flash ? CRIT : PANEL);
  g.setFont(&F_HUGE);
  g.setTextSize(2);
  textCenter(g, 50, 66, gearStr(f.gear), gc);
  g.setTextSize(1);
  g.setFont(&F_TEXT);
  textCenter(g, 50, 148, "ПЕРЕДАЧА", DIM);

  /* ── speed (center, 64px) + units ── */
  int kmh = (int)(f.speedKmh + 0.5f);
  if (kmh < 0) kmh = 0;
  snprintf(v, sizeof(v), "%d", kmh);
  g.setFont(&F_HUGE);
  g.setTextSize(2);
  textCenter(g, 168, 56, v, TEXT);
  g.setTextSize(1);
  g.setFont(&F_MED);
  textCenter(g, 168, 124, "КМ/Ч", ORANGE);

  /* lap / position */
  if (f.lap > 0 || f.racePos > 0) {
    g.setFont(&F_MED);
    snprintf(v, sizeof(v), "КРУГ %d  МЕСТО %d", f.lap + 1, f.racePos);
    textCenter(g, 168, 150, v, DIM);
  }

  /* ── throttle / brake (tall) ── */
  vBar(g, 246, 52, 18, 92, f.throttle * 100 / 255, GOOD);
  vBar(g, 270, 52, 18, 92, f.brake * 100 / 255, CRIT);
  g.setFont(&F_TEXT);
  textCenter(g, 255, 148, "ГАЗ", GOOD);
  textCenter(g, 279, 148, "ТРМ", CRIT);

  /* ── tire slip 2x2 + fuel (far right) ── */
  static const int tix[4] = {296, 310, 296, 310};
  static const int tiy[4] = {52, 52, 76, 76};
  for (int i = 0; i < 4; i++) {
    uint16_t c = slipColor(f.slip[i]);
    bool severe = f.slip[i] >= 1.0f; /* severe slip blinks */
    bool filled = severe ? flash : f.slip[i] >= 0.5f;
    if (filled)
      g.fillRoundRect(tix[i], tiy[i], 11, 21, 2, c);
    else
      g.drawRoundRect(tix[i], tiy[i], 11, 21, 2, c);
  }
  g.setFont(&F_TEXT);
  textCenter(g, 304, 100, "SLIP", DIM);

  int fuelPct = (int)(f.fuel * 100);
  vBar(g, 298, 112, 16, 32, fuelPct, fuelPct < 15 ? CRIT : ACCENT);
  textCenter(g, 306, 148, "FUEL", DIM);

  /* ── overlays ── */
  if (!f.raceOn) { /* game menu / replay: data frozen */
    g.setFont(&F_MED);
    g.fillRoundRect(118, 64, 100, 26, 4, PANEL);
    g.drawRoundRect(118, 64, 100, 26, 4, ACCENT);
    textCenter(g, 168, 67, "ПАУЗА", ACCENT);
  }
  if (ui.st.alertActive && ((ui.now / 300) & 1)) {
    /* PC overheating while racing: small corner triangle, never a takeover */
    g.fillTriangle(306, 16, 318, 16, 312, 4, CRIT);
  }
}

} // namespace scenes
