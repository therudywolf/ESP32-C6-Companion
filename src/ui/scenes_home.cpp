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
                      const char *label, uint16_t ink) {
  g.setFont(&F_SMALL);
  textAt(g, x, y, label, DIM);
  if (gr.count < 2) {
    textAt(g, x + 62, y, "копится...", DIM);
    return;
  }
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
  textRight(g, x + w, y, t, DIM);
  snprintf(t, sizeof(t), "%d", lo / div);
  textRight(g, x + w, y + h - 2, t, DIM);

  const int gy = y + 10, gh = h - 12, gw = w - 26;
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
                      const char *label, uint16_t ink) {
  g.setFont(&F_SMALL);
  textAt(g, x, y, label, DIM);
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
  textRight(g, x + w, y, t, DIM);
  snprintf(t, sizeof(t), "%d", lo / div);
  textRight(g, x + w, y + h - 2, t, DIM);

  const int gy = y + 10, gh = h - 12, gw = w - 26;
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

  /* ── ТЕМПЕРАТУРА ─────────────────────────────────────────────────────── */
  panel(g, 6, 26, 152, 70, "ТЕМПЕРАТУРА");
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
      /* F_HUGE at size ONE. At size 2 its ink is ~64 px against 58 px of
       * usable tile, and the digits spilled through the frame into the trend
       * strip below — the CPU screen affords its hero an 88 px tile, this one
       * cannot because the trends need the room. */
      g.setFont(&F_HUGE);
      snprintf(v, sizeof(v), "%d", whole);
      int vw = g.textWidth(v);
      textAt(g, 16, 42, v, c);
      g.setFont(&F_MED);
      snprintf(v, sizeof(v), ",%d", frac);
      textAt(g, 16 + vw + 2, 56, v, stale ? DIM : ORANGE);
      g.setFont(&F_TEXT);
      textAt(g, 16 + vw + 4, 40, "C", DIM);
    } else {
      g.setFont(&F_BIG);
      textAt(g, 16, 50, "-", DIM);
    }
  }

  /* ── ВЛАЖНОСТЬ ───────────────────────────────────────────────────────── */
  panel(g, 162, 26, 152, 70, "ВЛАЖНОСТЬ");
  {
    if (z.humidity >= 0) {
      uint16_t c = INFO;
      if (!stale && s.zbAlert) {
        if (s.zbHumMax <= 100 && z.humidity > s.zbHumMax) c = CRIT;
        else if (s.zbHumMin >= 0 && z.humidity < s.zbHumMin) c = WARN;
      }
      if (stale) c = DIM;
      g.setFont(&F_HUGE);
      snprintf(v, sizeof(v), "%d", z.humidity);
      int vw = g.textWidth(v);
      textAt(g, 172, 42, v, c);
      g.setFont(&F_TEXT);
      textAt(g, 172 + vw + 4, 40, "%", DIM);
      /* 30-60 % is the band everyone agrees on; outside it, say which way. */
      const char *verdict = z.humidity < 30   ? "сухо"
                            : z.humidity > 60 ? "сыро"
                                              : "норма";
      uint16_t vc = (z.humidity < 30 || z.humidity > 60) ? WARN : GOOD;
      g.setFont(&F_TEXT);
      textRight(g, 306, 74, verdict, stale ? DIM : vc);
    } else {
      g.setFont(&F_BIG);
      textAt(g, 172, 50, "-", DIM);
    }
  }

  /* ── the trend strip ─────────────────────────────────────────────────── */
  /* Titled by the window in force, so a long press has visible feedback beyond
   * the toast that fades. */
  const char *tTitle = ui.homeMode == 1   ? "ЗА СУТКИ"
                       : ui.homeMode == 2 ? "ЗА НЕДЕЛЮ"
                                          : "ПОСЛЕДНИЕ ОТЧЁТЫ";
  panel(g, 6, 100, 308, 50, tTitle);
  {
    const int ty = 110, th = 34;
    if (ui.homeMode > 0 && ui.climate && ui.climate->filled >= 2) {
      cardTrend(g, 16, ty, 140, th, ui.climate->temp10, ui.climate->cols, 10, 5,
                "температура", stale ? DIM : ORANGE);
      cardTrend(g, 168, ty, 138, th, ui.climate->hum, ui.climate->cols, 1, 6,
                "влажность", stale ? DIM : INFO);
    } else if (ui.homeMode > 0) {
      g.setFont(&F_TEXT);
      textCenter(g, NOCT_W / 2, 116, "за этот период записей ещё нет", DIM);
      g.setFont(&F_SMALL);
      textCenter(g, NOCT_W / 2, 136, "долгое нажатие — вернуть последние отчёты",
                 DIM);
    } else {
      liveTrend(g, 16, ty, 140, th, ui.gr.zbTemp, 10, 5, "температура",
                stale ? DIM : ORANGE);
      liveTrend(g, 168, ty, 138, th, ui.gr.zbHum, 1, 6, "влажность",
                stale ? DIM : INFO);
    }
  }

  /* ── footer: who, how fresh, and the two secondary readings ──────────── */
  g.setFont(&F_SMALL);
  textAt(g, 8, 156, z.name[0] ? z.name : NOCT_ZB_NET_NAME,
         stale ? DIM : ORANGE);
  {
    int x = 8 + g.textWidth(z.name[0] ? z.name : NOCT_ZB_NET_NAME) + 10;
    if (z.battery >= 0) {
      bool lowBat = s.zbBattMin > 0 && z.battery <= s.zbBattMin;
      snprintf(v, sizeof(v), "%d%%", z.battery);
      batteryBar(g, x, 157, 26, z.battery, stale);
      textAt(g, x + 30, 156, v, stale ? DIM : (lowBat ? CRIT : DIM));
      x += 30 + g.textWidth(v) + 10;
    }
    /* Only the WSDCGQ11LM has a barometer; the cheaper models do not, so this
     * appears only when there is a reading behind it. */
    if (z.pressure > 0) {
      snprintf(v, sizeof(v), "%d мм", (z.pressure * 3) / 4);
      textAt(g, x, 156, v, DIM);
    }
  }
  char age[40];
  fmtAge(z, age, sizeof(age));
  textRight(g, NOCT_W - 8, 156, age, stale ? CRIT : DIM);

  /* More sensors than this screen shows: say so rather than hide them. */
  if (ui.st.zb.count > 1) {
    snprintf(v, sizeof(v), "+%d на ПОГОДЕ", ui.st.zb.count - 1);
    textRight(g, NOCT_W - 8, 146, v, DIM);
  }
}

} // namespace scenes
