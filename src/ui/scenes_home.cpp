/*
 * Nocturne C6 — ДОМ: the climate screen for the ForestHome sensor.
 *
 * One sensor, by design. The ПОГОДА tile answers "and it's 23 inside"; this
 * answers what the room has been DOING.
 *
 * Laid out in the same grammar as every other data screen — panel() tiles with
 * a title tab, a hero number per tile, a trend where there is history. The
 * first version floated bare text on the background and was the only screen in
 * the ring that did; next to CPU or КУЛЕРЫ it read as unfinished, and a number
 * with no frame around it has nothing telling the eye where to stop.
 *
 *   y26..y96   ТЕМПЕРАТУРА | ВЛАЖНОСТЬ   two heroes side by side
 *   y100..y150 the trend strip, titled by whichever window is selected
 *   y156       name · battery · pressure · age
 *
 * A reading over an hour old dims EVERYTHING at once. Battery sensors go quiet
 * for half an hour at a time, and a stale number drawn as if it were current is
 * exactly the lie the "no signal" handling exists to prevent. A number outside
 * the owner's alert band wears the alert colour, so the screen agrees with the
 * toast that just fired instead of looking calm beside it.
 */
#include <limits.h>

#include "core/config.h"
#include "ui/Scenes.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace scenes {

using namespace theme;
using namespace widgets;

/* y to pass to textAt so the visible glyph TOP lands at wantTop. The u8g2
 * logisoso line box is taller than the digits — leading sits above — and
 * without this the heroes punched through the bottom of their tiles. Same
 * helper the CPU/GPU screens use; duplicated rather than shared because it is
 * four lines and scenes_hw.cpp keeps it file-local. */
static int inkTop(LGFX_Sprite &g, int wantTop, int inkH) {
  int off = g.fontHeight() - inkH;
  if (off < 0) off = 0;
  return wantTop - off / 2;
}

static bool isStale(const ZbSensor &z) {
  return z.ageSec < 0 || z.ageSec > 3600;
}

/* "только что", "12 мин назад" — an age is only useful if it reads as one. */
static void fmtAge(const ZbSensor &z, char *out, size_t cap) {
  if (z.ageSec < 0) snprintf(out, cap, "давность неизвестна");
  else if (z.ageSec < 90) snprintf(out, cap, "только что");
  else if (z.ageSec < 3600) snprintf(out, cap, "%d мин назад", z.ageSec / 60);
  else if (z.ageSec < 86400) snprintf(out, cap, "%d ч назад", z.ageSec / 3600);
  else snprintf(out, cap, "%d дн назад", z.ageSec / 86400);
}

static void batteryBar(LGFX_Sprite &g, int x, int y, int w, int pct,
                       bool stale) {
  const int h = 8;
  uint16_t frame = stale ? DIM : ORANGE_DIM;
  g.drawRect(x, y, w - 3, h, frame);
  g.fillRect(x + w - 3, y + 2, 2, h - 4, frame); /* the nub */
  int fill = (w - 5) * pct / 100;
  if (fill < 1 && pct > 0) fill = 1;
  uint16_t c = pct <= 20 ? CRIT : (pct <= 40 ? WARN : GOOD);
  g.fillRect(x + 1, y + 1, fill, h - 2, stale ? DIM : c);
}

/* One half of the trend strip, over the live RAM series. */
static void liveTrend(LGFX_Sprite &g, int x, int y, int w, int h,
                      const RollingGraph &gr, int div, int flatSpan,
                      uint16_t ink) {
  if (gr.count < 2) {
    /* A dotted baseline rather than a word: "график будет здесь" needs no
     * text, and a truncated one ("копит") reads as a rendering fault. */
    for (int i = 0; i < w - 22; i += 4) g.drawPixel(x + i, y + h - 10, DIM);
    return;
  }
  g.setFont(&F_SMALL);
  int lo = gr.at(0), hi = gr.at(0);
  for (int i = 1; i < gr.count; i++) {
    int v = gr.at(i);
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  /* A steady room should look steady: without a floor on the span, a tenth of
   * a degree of sensor noise fills the box and reads as a storm. */
  if (hi - lo < flatSpan) {
    int mid = (hi + lo) / 2;
    lo = mid - flatSpan / 2;
    hi = mid + flatSpan / 2;
  }
  char t[12];
  snprintf(t, sizeof(t), "%d", hi / div);
  textRight(g, x + w, y - 2, t, DIM);
  snprintf(t, sizeof(t), "%d", lo / div);
  textRight(g, x + w, y + h - 8, t, DIM);

  /* The line gets the whole box; only the two scale labels are inset. */
  const int gy = y, gh = h - 8, gw = w - 22;
  for (int i = 1; i < gr.count; i++) {
    int x0 = x + (i - 1) * gw / (gr.count - 1);
    int x1 = x + i * gw / (gr.count - 1);
    int y0 = gy + gh - (gr.at(i - 1) - lo) * gh / (hi - lo);
    int y1 = gy + gh - (gr.at(i) - lo) * gh / (hi - lo);
    g.drawLine(x0, y0, x1, y1, ink);
  }
}

/* The same, over a series loaded from the card — which HAS GAPS. A bucket no
 * reading fell into lifts the pen instead of drawing through, because a
 * straight segment across six silent hours is a claim the sensor never made. */
static void cardTrend(LGFX_Sprite &g, int x, int y, int w, int h,
                      const int *vals, int cols, int div, int flatSpan,
                      uint16_t ink) {
  g.setFont(&F_SMALL);
  int lo = 0, hi = 0;
  bool any = false;
  for (int i = 0; i < cols; i++) {
    int v = vals[i];
    if (v == INT_MIN || v < -1000) continue;
    if (!any) { lo = hi = v; any = true; }
    else { if (v < lo) lo = v; if (v > hi) hi = v; }
  }
  if (!any) return;
  if (hi - lo < flatSpan) {
    int mid = (hi + lo) / 2;
    lo = mid - flatSpan / 2;
    hi = mid + flatSpan / 2;
  }
  char t[12];
  snprintf(t, sizeof(t), "%d", hi / div);
  textRight(g, x + w, y - 2, t, DIM);
  snprintf(t, sizeof(t), "%d", lo / div);
  textRight(g, x + w, y + h - 8, t, DIM);

  const int gy = y, gh = h - 8, gw = w - 22;
  int px = -1, py = 0;
  for (int i = 0; i < cols; i++) {
    int v = vals[i];
    if (v == INT_MIN || v < -1000) { px = -1; continue; } /* gap: lift the pen */
    int cx = x + i * gw / (cols - 1);
    int cy = gy + gh - (v - lo) * gh / (hi - lo);
    if (px >= 0) g.drawLine(px, py, cx, cy, ink);
    else g.drawPixel(cx, cy, ink);
    px = cx;
    py = cy;
  }
}

/* Nothing paired yet: say what to press, and count the join window down so
 * "press the button on the sensor" has a deadline attached. A screen that
 * stays blank until you guess the right menu item teaches nothing. */
static void drawEmpty(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  char v[72];
  panel(g, 6, 26, 308, 124, NOCT_ZB_NET_NAME);

  int left = ui.st.zbJoinSecs;
  if (left > 0) {
    g.setFont(&F_BIG);
    snprintf(v, sizeof(v), "жду датчик: %d:%02d", left / 60, left % 60);
    textCenter(g, NOCT_W / 2, 46, v, GOOD);
    g.setFont(&F_TEXT);
    textCenter(g, NOCT_W / 2, 82, "зажми кнопку на датчике ~5 сек,", TEXT);
    textCenter(g, NOCT_W / 2, 100, "потом жми коротко раз в 2 сек", DIM);
    int w = NOCT_W - 100;
    g.drawRect(50, 124, w, 6, ORANGE_DIM);
    g.fillRect(51, 125, (w - 2) * left / NOCT_ZB_JOIN_SEC, 4, GOOD);
    return;
  }

  g.setFont(&F_MED);
  textCenter(g, NOCT_W / 2, 46, "датчик не привязан", DIM);
  g.setFont(&F_TEXT);
  textCenter(g, NOCT_W / 2, 78, "Меню > Система > Подключить датчик", TEXT);
  textCenter(g, NOCT_W / 2, 98, "или  zb join  в консоли", DIM);
  snprintf(v, sizeof(v), "координатор %s, канал %d",
           ui.st.link.zbUp ? "работает" : "не запущен", NOCT_ZB_CHANNEL);
  textCenter(g, NOCT_W / 2, 124, v, ui.st.link.zbUp ? GOOD : CRIT);
}

void drawHome(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  if (ui.st.zb.count <= 0) {
    drawEmpty(ui);
    return;
  }

  const ZbSensor &z = ui.st.zb.list[0];
  const Settings &s = ui.st.settings;
  bool stale = isStale(z);
  char v[48];

  /* Tiles sized by what goes IN them rather than split down the middle: the
   * temperature carries a decimal and a trend, the humidity carries neither.
   * Two equal boxes left the humidity one two-thirds empty and squeezed the
   * temperature's fraction against the frame. */
  const int tx = 6, tw = 186, hx = 196, hw = 118, ty = 26, th = 84;

  /* ── ТЕМПЕРАТУРА ─────────────────────────────────────────────────────── */
  /* The tile title carries the trend window, so a long press has feedback that
   * outlives the toast and the graph is never unlabelled. */
  const char *tTitle = ui.homeMode == 1   ? "ТЕМПЕРАТУРА / СУТКИ"
                       : ui.homeMode == 2 ? "ТЕМПЕРАТУРА / НЕДЕЛЯ"
                                          : "ТЕМПЕРАТУРА";
  panel(g, tx, ty, tw, th, tTitle);
  {
    uint16_t c = TEXT;
    if (!stale && s.zbAlert) {
      if (s.zbTempMax < 99 && z.temp10 > s.zbTempMax * 10) c = CRIT;
      else if (s.zbTempMin > -99 && z.temp10 < s.zbTempMin * 10) c = INFO;
    }
    if (stale) c = DIM;

    if (z.temp10 != -32768) {
      int whole = z.temp10 / 10, frac = z.temp10 % 10;
      if (frac < 0) frac = -frac;
      g.setFont(&F_HUGE);
      g.setTextSize(2);
      snprintf(v, sizeof(v), "%d", whole);
      int vw = g.textWidth(v);
      /* Ink-anchored the way heroTemp does it on CPU/GPU: the glyph box is
       * taller than the ink, so centring on the box sits the number low. */
      textAt(g, tx + 10, inkTop(g, ty + 16, 64), v, c);
      g.setTextSize(1);
      g.setFont(&F_BIG);
      snprintf(v, sizeof(v), ",%d", frac);
      textAt(g, tx + 12 + vw, ty + 52, v, stale ? DIM : ORANGE);
      g.setFont(&F_MED);
      textAt(g, tx + 12 + vw, ty + 20, "C", DIM);
    } else {
      g.setFont(&F_BIG);
      textAt(g, tx + 10, ty + 34, "-", DIM);
    }

    /* The trend rides INSIDE the tile, the way НАГРУЗКА does on the CPU
     * screen. A separate strip below left both boxes half empty. */
    /* Clear of the fraction: "23" is 64 px wide and ",0" another 30, so a
     * graph starting at +116 was touching the comma. */
    const int gx = tx + 128, gw2 = 50, gy = ty + 24, gh = 44;
    if (ui.homeMode > 0 && ui.climate && ui.climate->filled >= 2)
      cardTrend(g, gx, gy, gw2, gh, ui.climate->temp10, ui.climate->cols, 10, 5,
                stale ? DIM : ORANGE);
    else if (ui.homeMode > 0) {
      for (int i = 0; i < gw2 - 22; i += 4)
        g.drawPixel(gx + i, gy + gh - 10, DIM);
    } else
      liveTrend(g, gx, gy, gw2, gh, ui.gr.zbTemp, 10, 5, stale ? DIM : ORANGE);
  }

  /* ── ВЛАЖНОСТЬ ───────────────────────────────────────────────────────── */
  panel(g, hx, ty, hw, th, "ВЛАЖНОСТЬ");
  {
    if (z.humidity >= 0) {
      uint16_t c = INFO;
      if (!stale && s.zbAlert) {
        if (s.zbHumMax <= 100 && z.humidity > s.zbHumMax) c = CRIT;
        else if (s.zbHumMin >= 0 && z.humidity < s.zbHumMin) c = WARN;
      }
      if (stale) c = DIM;
      g.setFont(&F_HUGE);
      g.setTextSize(2);
      snprintf(v, sizeof(v), "%d", z.humidity);
      int vw = g.textWidth(v);
      textAt(g, hx + 10, inkTop(g, ty + 16, 64), v, c);
      g.setTextSize(1);
      g.setFont(&F_MED);
      textAt(g, hx + 12 + vw, ty + 20, "%", DIM);
    } else {
      g.setFont(&F_BIG);
      textAt(g, hx + 10, ty + 34, "-", DIM);
    }
  }

  /* ── ДАТЧИК: everything secondary on one honest row ──────────────────── */
  panel(g, tx, 114, 308, 36, "ДАТЧИК");
  {
    g.setFont(&F_TEXT);
    int x = tx + 10;
    textAt(g, x, 126, z.name[0] ? z.name : NOCT_ZB_NET_NAME,
           stale ? DIM : ORANGE);
    x += g.textWidth(z.name[0] ? z.name : NOCT_ZB_NET_NAME) + 14;

    if (z.battery >= 0) {
      bool lowBat = s.zbBattMin > 0 && z.battery <= s.zbBattMin;
      batteryBar(g, x, 128, 28, z.battery, stale);
      snprintf(v, sizeof(v), "%d%%", z.battery);
      textAt(g, x + 32, 126, v, stale ? DIM : (lowBat ? CRIT : DIM));
      x += 32 + g.textWidth(v) + 14;
    }
    /* Only the WSDCGQ11LM has a barometer; the cheaper models do not, so this
     * appears only when there is a reading behind it. */
    if (z.pressure > 0) {
      snprintf(v, sizeof(v), "%d мм", (z.pressure * 3) / 4);
      textAt(g, x, 126, v, DIM);
    }

    /* 30-60 % is the band everyone agrees on; outside it, say which way. The
     * colour on the number already carries the alert; this carries the plain
     * reading of it, and there is no room for it beside a 64 px hero. */
    if (z.humidity >= 0) {
      const char *verdict = z.humidity < 30   ? "сухо"
                            : z.humidity > 60 ? "сыро"
                                              : "норма";
      uint16_t vc = (z.humidity < 30 || z.humidity > 60) ? WARN : GOOD;
      textAt(g, x + 60, 126, verdict, stale ? DIM : vc);
    }

    char age[40];
    fmtAge(z, age, sizeof(age));
    textRight(g, tx + 308 - 10, 126, age, stale ? CRIT : DIM);
  }

  /* More sensors than this screen shows: say so rather than hide them. */
  if (ui.st.zb.count > 1) {
    g.setFont(&F_SMALL);
    snprintf(v, sizeof(v), "+%d на ПОГОДЕ", ui.st.zb.count - 1);
    textRight(g, NOCT_W - 8, 156, v, DIM);
  }
}

} // namespace scenes
