/* Nocturne C6 — FORZA HUD, RPM-first fullscreen dash.
 * Zones (320x172): shift lamp 2..26 | mid band 32..120: gear box 4..92,
 * RPM hero 96..236, pedals 240..284, slip+fuel 288..318 | info row 127..166.
 * Big-font glyphs are placed via inkTop(): the u8g2 logisoso line box is
 * taller than the visible digits, so we anchor the INK, not the line. */
#include <WiFi.h>
#include <math.h>

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

/* seconds -> "m:ss.t"; "--:--" when no time yet */
static void fmtLap(char *out, size_t cap, float s) {
  if (s <= 0.01f || s > 5999.0f) {
    snprintf(out, cap, "--:--");
    return;
  }
  int tenths = (int)(s * 10.0f + 0.5f);
  snprintf(out, cap, "%d:%02d.%d", tenths / 600, (tenths / 10) % 60, tenths % 10);
}

/* y to pass to textAt so the visible glyph TOP lands at wantTop.
 * inkH = the glyph height we expect (64 for F_HUGE size2). */
static int inkTop(LGFX_Sprite &g, int wantTop, int inkH) {
  int off = g.fontHeight() - inkH;
  if (off < 0) off = 0;
  return wantTop - off / 2; /* leading splits above/below the ink */
}

/* friction circle: a dot at (lateral, longitudinal) g, edge = 1.5 g. Shows how
 * much grip you're using and which way — great for drift and free-ride. */
static void drawGMeter(LGFX_Sprite &g, int cx, int cy, int r, float lat,
                       float lon) {
  g.drawCircle(cx, cy, r, ORANGE_DIM);
  g.drawCircle(cx, cy, (r * 2) / 3, lerp565(BG, ORANGE_DIM, 120));
  g.drawFastHLine(cx - r, cy, 2 * r + 1, lerp565(BG, ORANGE_DIM, 90));
  g.drawFastVLine(cx, cy - r, 2 * r + 1, lerp565(BG, ORANGE_DIM, 90));
  float scale = r / 1.5f;
  float dx = lat * scale;       /* + = pushed right */
  float dy = -lon * scale;      /* + accel forward = dot up */
  float mag = sqrtf(dx * dx + dy * dy);
  if (mag > r) {
    dx = dx * r / mag;
    dy = dy * r / mag;
  }
  float gtot = sqrtf(lat * lat + lon * lon);
  uint16_t dc = gtot > 1.1f ? CRIT : (gtot > 0.6f ? WARN : GOOD);
  g.fillCircle(cx + (int)dx, cy + (int)dy, 3, dc);
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
    /* cache the IP string — localIP().toString() allocates every frame */
    static String ipCache;
    static unsigned long ipAt = 0;
    if (!ipCache.length() || ui.now - ipAt > 2000) {
      ipCache = WiFi.localIP().toString();
      ipAt = ui.now;
    }
    snprintf(v, sizeof(v), "%s : %d", ipCache.c_str(), NOCT_FORZA_UDP_PORT);
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

  /* ── hero RPM: scale to fit the 96..236 zone so wide 5-digit values never
   * slide onto the pedals (x240) ── */
  int rpm = (int)f.rpm;
  if (rpm > 99999) rpm = 99999;
  snprintf(v, sizeof(v), "%d", rpm);
  uint16_t rc = shiftNow ? (flash ? CRIT : TEXT)
                         : (pct >= 0.80f ? WARN : TEXT);
  g.setFont(&F_HUGE);
  g.setTextSize(2);
  if (g.textWidth(v) > 138) { /* 5-digit: drop to 32px so it fits */
    g.setTextSize(1);
    textCenter(g, 166, inkTop(g, 50, 32), v, rc);
  } else {
    textCenter(g, 166, inkTop(g, 34, 64), v, rc);
  }
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

  /* ── adaptive info zone (y117..171): the HUD reads the situation and shows
   * RACE data (position/laps), DRIFT data (G-meter/angle/timer) or FREE-RIDE
   * cruise stats. NOTE: Forza Data Out is ego-only — no opponent gap exists,
   * so place changes + lap deltas are the faithful substitute. ── */
  bool fl2 = (ui.now / 160) & 1;
  if (f.raceOn) {
    int mode = f.drifting ? 1 : (f.racePos > 0 ? 0 : 2);
    g.setFont(&F_TEXT);
    textCenter(g, 166, 118,
               mode == 1   ? "РЕЖИМ: ДРИФТ"
               : mode == 0 ? "РЕЖИМ: ГОНКА"
                           : "РЕЖИМ: СВОБОДНО",
               mode == 1 ? ACCENT : (mode == 0 ? GOOD : INFO));
    g.drawFastHLine(10, 127, NOCT_W - 20, PANEL);

    if (mode == 1) { /* DRIFT */
      drawGMeter(g, 34, 149, 18, f.latG(), f.longG());
      g.setFont(&F_BIG);
      snprintf(v, sizeof(v), "%.1f", fabsf(f.latG()));
      textCenter(g, 116, 134, v, fl2 ? CRIT : WARN);
      g.setFont(&F_TEXT);
      textCenter(g, 116, 161, "БОКОВАЯ G", DIM);
      g.setFont(&F_MED);
      textCenter(g, 214, 132, fl2 ? "ДРИФТ!" : "ДРИФТ", fl2 ? ACCENT : ORANGE);
      snprintf(v, sizeof(v), "%.1fс", (ui.now - f.driftMs) / 1000.0f);
      textCenter(g, 196, 150, v, TEXT);
      g.setFont(&F_TEXT);
      snprintf(v, sizeof(v), "пик %.1fG", f.driftPeakG);
      textCenter(g, 282, 150, v, INFO);
    } else if (mode == 2) { /* FREE RIDE */
      snprintf(v, sizeof(v), "%d", (int)(f.speedKmh + 0.5f));
      infoCell(g, 6, 84, "КМ/Ч", v, TEXT);
      int hp = (int)(f.powerW / 745.7f);
      snprintf(v, sizeof(v), "%d", hp > 0 ? hp : 0);
      infoCell(g, 96, 70, "Л.С.", v, ACCENT);
      snprintf(v, sizeof(v), "%.1f", f.distanceM / 1000.0f);
      infoCell(g, 168, 70, "КМ ПУТЬ", v, INFO);
      drawGMeter(g, 286, 147, 17, f.latG(), f.longG());
      g.setFont(&F_TEXT);
      textCenter(g, 286, 168, "G", DIM);
    } else { /* RACE */
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
      bool posChanged = f.posChangeMs && (ui.now - f.posChangeMs) < 2800;
      infoCell(g, 238, 72, "КРУГ/МЕСТО", v,
               posChanged ? (f.posGain > 0 ? GOOD : CRIT) : TEXT);

      bool newBest = f.bestLapMs && (ui.now - f.bestLapMs) < 3200;
      if (posChanged) {
        uint16_t bc = f.posGain > 0 ? GOOD : CRIT;
        g.fillRect(8, 159, NOCT_W - 16, 12, fl2 ? lerp565(BG, bc, 70) : BG);
        g.setFont(&F_TEXT);
        snprintf(v, sizeof(v), "%s  P%d  (%+d)",
                 f.posGain > 0 ? "ОБОГНАЛ!" : "ОТКАТ", f.racePos, f.posGain);
        textCenter(g, NOCT_W / 2, 162, v, bc);
      } else if (newBest) {
        g.fillRect(8, 159, NOCT_W - 16, 12, fl2 ? lerp565(BG, ACCENT, 55) : BG);
        g.setFont(&F_TEXT);
        textCenter(g, NOCT_W / 2, 162, "ЛУЧШИЙ КРУГ!", ACCENT);
      } else if (f.curLap > 0.01f || f.bestLap > 0.01f) {
        char lc[10], lb[10], ll[10];
        fmtLap(lc, sizeof(lc), f.curLap);
        fmtLap(lb, sizeof(lb), f.bestLap);
        fmtLap(ll, sizeof(ll), f.lastLap);
        g.setFont(&F_TEXT);
        snprintf(v, sizeof(v), "КРУГ %s   ЛУЧ %s   ПОСЛ %s", lc, lb, ll);
        textCenter(g, NOCT_W / 2, 162, v, DIM);
      }
    }
  } else {
    /* ── paused / between events: session summary card ── */
    int cx0 = 36, cy0 = 34, cw = 248, ch = 96;
    g.fillRoundRect(cx0, cy0, cw, ch, 6, PANEL);
    g.drawRoundRect(cx0, cy0, cw, ch, 6, ACCENT);
    bool blink = (ui.now / 400) & 1;
    g.setFont(&F_MED);
    textCenter(g, NOCT_W / 2, cy0 + 5, "ПАУЗА", blink ? ACCENT : DIM);
    g.setFont(&F_TEXT);
    textCenter(g, NOCT_W / 2, cy0 + 25, "СВОДКА СЕССИИ", DIM);
    char lb[10];
    fmtLap(lb, sizeof(lb), f.bestLap);
    const char *cap[4] = {"лучший круг", "макс км/ч", "макс G", "пик л.с."};
    char val[4][12];
    uint16_t col[4] = {GOOD, TEXT, WARN, ACCENT};
    snprintf(val[0], 12, "%s", lb);
    snprintf(val[1], 12, "%d", (int)(f.topSpeed + 0.5f));
    snprintf(val[2], 12, "%.1fG", f.maxG);
    snprintf(val[3], 12, "%d", f.peakHp);
    for (int i = 0; i < 4; i++) {
      int qx = cx0 + 16 + (i % 2) * 120;
      int qy = cy0 + 42 + (i / 2) * 28;
      g.setFont(&F_TEXT);
      textAt(g, qx, qy, cap[i], DIM);
      g.setFont(&F_MED);
      textAt(g, qx, qy + 9, val[i], col[i]);
    }
  }
  if (ui.st.alertActive && ((ui.now / 300) & 1)) {
    g.fillTriangle(160, 124, 176, 124, 168, 112, CRIT);
  }
}

} // namespace scenes
