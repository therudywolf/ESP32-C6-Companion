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

#include "core/Barometer.h"
#include "core/config.h"
#include "ui/Scenes.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace scenes {

using namespace theme;
using namespace widgets;

static bool isStale(const ZbSensor &z) {
  /* Over an hour: the sensor has missed at least one reporting interval. */
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

/* 64 px hero with a small unit beside it, ink-anchored — the same shape
 * heroTemp() draws on CPU and GPU, so this screen sits at the same optical
 * height as the rest of the ring. */
static void hero(LGFX_Sprite &g, int x, int y, const char *num,
                 const char *frac, const char *unit, uint16_t c) {
  g.setFont(&F_HUGE);
  g.setTextSize(2);
  int vw = g.textWidth(num);
  textAt(g, x, inkTop(g, y, 64), num, c);
  g.setTextSize(1);
  /* The tenth and the unit stack in one narrow column to the RIGHT of the
   * digits, tenth on top. Read downward it says "23 , 4 C" in that order.
   * The tenth used to sit in the tile's bottom-left corner, which put it
   * nowhere near the number it belongs to - it read as a stray mark rather
   * than as part of the reading. A room moves by tenths, so the digit has to
   * stay; it just has to stay ATTACHED. */
  if (frac) {
    /* F_MED, not F_TEXT. The tenth is part of the reading, not a footnote:
     * a room moves BY tenths, so this is the digit that changes while the
     * two big ones sit still. Drawn in the smallest face on the screen it
     * was invisible from the far side of the room - which is where this
     * board is read from. Same colour as the number it belongs to, for the
     * same reason. */
    g.setFont(&F_MED);
    textAt(g, x + vw + 4, y + 36, frac, c);
  }
  if (unit) {
    g.setFont(&F_MED);
    textAt(g, x + vw + 4, frac ? y + 60 : y + 44, unit, DIM);
  }
}

/* Big number + small unit on one baseline. Mirrors bigVal() in scenes_hw.cpp
 * for the same reason as inkTop: file-local there, and worth matching exactly
 * so the two screens line up. */
static void bigVal(LGFX_Sprite &g, int x, int y, const char *num,
                   const char *unit, uint16_t c, bool rightAlign = false) {
  g.setFont(&F_BIG);
  int nw = g.textWidth(num);
  g.setFont(&F_TEXT);
  int uw = unit ? g.textWidth(unit) : 0;
  int x0 = rightAlign ? x - nw - uw - 4 : x;
  g.setFont(&F_BIG);
  textAt(g, x0, y, num, c);
  if (unit) {
    g.setFont(&F_TEXT);
    textAt(g, x0 + nw + 4, y + 13, unit, DIM);
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

  auto tend = ui.st.zbTrendOk
                  ? barometer::classify(ui.st.zbPress10Delta3h, 3)
                  : barometer::TEND_UNKNOWN;

  /* Laid out on the CPU screen's grid, tile for tile: a tall hero on the left,
   * two stacked tiles on the right, one full-width strip along the bottom.
   * Same rhythm means the eye already knows where to look after ОБЗОР or CPU —
   * a screen that invents its own composition costs the reader a moment every
   * time it comes round the ring. */

  /* ── left, tall: the temperature ─────────────────────────────────────── */
  panel(g, 4, 26, 130, 88, "ТЕМПЕРАТУРА");
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
      char f[8];
      snprintf(v, sizeof(v), "%d", whole);
      snprintf(f, sizeof(f), ",%d", frac);
      hero(g, 14, 36, v, f, "C", c);
    } else {
      g.setFont(&F_BIG);
      textAt(g, 14, 56, "-", DIM);
    }
    trendArrow(g, 118, 32, ui.gr.zbTemp, 6, 2);
  }

  /* ── right upper: humidity, with its own trend ───────────────────────── */
  panel(g, 140, 26, 176, 50, "ВЛАЖНОСТЬ");
  {
    if (z.humidity >= 0) {
      uint16_t c = INFO;
      if (!stale && s.zbAlert) {
        if (s.zbHumMax <= 100 && z.humidity > s.zbHumMax) c = CRIT;
        else if (s.zbHumMin >= 0 && z.humidity < s.zbHumMin) c = WARN;
      }
      if (stale) c = DIM;
      g.setFont(&F_VALUE);
      g.setTextSize(2);
      snprintf(v, sizeof(v), "%d%%", z.humidity);
      textAt(g, 148, 36, v, c);
      g.setTextSize(1);
      trendArrow(g, 218, 34, ui.gr.zbHum, 6, 2);
      /* Only once there is a line to draw. sparkline() frames itself before
       * it checks whether it has points, which is invisible on the PC screens
       * - those fill at 1 Hz - but this sensor reports about every fifty
       * minutes, so an empty rectangle would sit here for the first hour
       * after every boot looking like a broken widget. Say what it is
       * instead: the graph is waiting, not missing. */
      if (ui.gr.zbHum.count >= 2) {
        sparkline(g, 232, 34, 76, 34, ui.gr.zbHum, stale ? DIM : INFO);
      } else {
        /* F_TEXT rather than F_SMALL: this is the tile explaining itself,
         * and an explanation nobody can read is worse than an empty box,
         * because it looks like it said something. */
        g.setFont(&F_TEXT);
        textAt(g, 236, 44, "график", DIM);
        textAt(g, 236, 58, "копится", DIM);
      }
    } else {
      g.setFont(&F_BIG);
      textAt(g, 148, 38, "-", DIM);
    }
  }

  /* ── right lower: the two secondary numbers ──────────────────────────── */
  panel(g, 140, 82, 176, 32, "ДАВЛЕНИЕ / БАТАРЕЯ");
  {
    if (z.pressure > 0) {
      /* Pressure is the one reading here that is about the OUTDOORS — a
       * building leaks, so the needle tracks the atmosphere. mmHg because that
       * is the unit a Russian forecast quotes. */
      snprintf(v, sizeof(v), "%d", (z.pressure * 3) / 4);
      uint16_t pc = tend == barometer::TEND_FALL_FAST   ? WARN
                    : tend == barometer::TEND_RISE_FAST ? INFO
                                                        : TEXT;
      bigVal(g, 148, 86, v, "мм", stale ? DIM : pc);
    }
    if (z.battery >= 0) {
      bool lowBat = s.zbBattMin > 0 && z.battery <= s.zbBattMin;
      snprintf(v, sizeof(v), "%d", z.battery);
      bigVal(g, 308, 86, v, "%", stale ? DIM : (lowBat ? CRIT : TEXT), true);
    }
  }

  /* ── bottom, full width: what the sky is doing, and how fresh this is ─── */
  /* The tab carries the trend window so a long press has feedback that
   * outlives the toast. */
  const char *strip = ui.homeMode == 1   ? "ПРОГНОЗ / СУТКИ"
                      : ui.homeMode == 2 ? "ПРОГНОЗ / НЕДЕЛЯ"
                                         : "ПРОГНОЗ / БАРОМЕТР";
  panel(g, 4, 120, 312, 48, strip);
  {
    /* The claim gets the arrow and the big type; the evidence goes small
     * underneath. Before, both rows were the same weight and the strip read as
     * two unrelated captions rather than as a statement with its reason. */
    const char *line = nullptr;
    uint16_t lc = TEXT;
    int ax = 12;
    if (ui.st.zbTrendOk) {
      line = barometer::forecast(tend);
      lc = barometer::headacheWatch(tend) ? WARN
           : tend == barometer::TEND_RISE_FAST ? INFO
                                               : TEXT;
      ax += baroArrow(g, 12, 130, barometer::direction(tend),
                      barometer::isSharp(tend), stale ? DIM : lc);
    } else {
      /* No trend yet is not "steady" — say which, or the screen claims the
       * weather is settled when it simply has not been watching long enough. */
      line = "Барометр копит историю";
      lc = DIM;
    }
    g.setFont(&F_MED);
    g.setTextSize(1);
    char clipped[48];
    clipW(g, line, clipped, sizeof(clipped), 306 - ax);
    textAt(g, ax, 126, clipped, stale ? DIM : lc);

    /* Second row: the sensor, its freshness, and the 3-hour delta that the
     * forecast above was derived from — the claim and its evidence together. */
    g.setFont(&F_TEXT);
    char age[40];
    fmtAge(z, age, sizeof(age));
    textAt(g, 12, 150, z.name[0] ? z.name : NOCT_ZB_NET_NAME,
           stale ? DIM : ORANGE);
    if (ui.st.zbTrendOk) {
      int dp = ui.st.zbPress10Delta3h;
      snprintf(v, sizeof(v), "%+d.%d гПа/3ч", dp / 10, abs(dp % 10));
      textAt(g, 120, 150, v, DIM);
    }
    textRight(g, 308, 150, age, stale ? CRIT : DIM);
  }

  /* More sensors than this screen shows: say so rather than hide them. */
  if (ui.st.zb.count > 1) {
    g.setFont(&F_SMALL);
    snprintf(v, sizeof(v), "+%d на ПОГОДЕ", ui.st.zb.count - 1);
    textRight(g, NOCT_W - 8, 22, v, DIM);
  }
}

} // namespace scenes
