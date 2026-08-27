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

  /* Surfaces, not outlines. Four tiles on a 4 px grid, and ONE display-sized
   * number on the whole screen.
   *
   * That last rule is the actual change. This screen used to run the
   * temperature at 64 px AND the humidity at double-size, and two numbers that
   * large read as an argument rather than as a reading - the eye lands
   * somewhere different every time the screen comes round. Temperature keeps
   * the display role because it is the one number this room is consulted for;
   * everything else drops a step in the scale and becomes legible AS
   * secondary, which it always was.
   *
   *   4                 144 148                      316
   *   +-----------------+ +-------------------------+  26
   *   | температура     | | влажность               |
   *   |                 | +-------------------------+  78
   *   |    24,5 C       | | давление     батарея    |
   *   +-----------------+ +-------------------------+ 126
   *   +-------------------------------------------+   130
   *   | forecast, then the evidence under it       |
   *   +-------------------------------------------+   170
   */

  /* ── left, tall: the temperature, the one display element ────────────── */
  {
    Rect c = panelM(g, 4, 26, 140, 92, "температура");
    uint16_t col = TEXT;
    if (!stale && s.zbAlert) {
      if (s.zbTempMax < 99 && z.temp10 > s.zbTempMax * 10) col = CRIT;
      else if (s.zbTempMin > -99 && z.temp10 < s.zbTempMin * 10) col = INFO;
    }
    if (stale) col = DIM;
    if (z.temp10 != -32768) {
      int whole = z.temp10 / 10, frac = z.temp10 % 10;
      if (frac < 0) frac = -frac;
      char f[8];
      snprintf(v, sizeof(v), "%d", whole);
      snprintf(f, sizeof(f), ",%d", frac);
      hero(g, c.x, c.y + 6, v, f, "C", col);
    } else {
      g.setFont(&F_BIG);
      textAt(g, c.x, c.y + 24, "-", DIM);
    }
    trendArrow(g, c.x + c.w - 10, c.y, ui.gr.zbTemp, 6, 2);
  }

  /* ── right upper: humidity, dropped from display to title ────────────── */
  {
    Rect c = panelM(g, 148, 26, 168, 44, "влажность");
    if (z.humidity >= 0) {
      uint16_t col = INFO;
      if (!stale && s.zbAlert) {
        if (s.zbHumMax <= 100 && z.humidity > s.zbHumMax) col = CRIT;
        else if (s.zbHumMin >= 0 && z.humidity < s.zbHumMin) col = WARN;
      }
      if (stale) col = DIM;
      g.setFont(&F_BIG);
      snprintf(v, sizeof(v), "%d%%", z.humidity);
      textAt(g, c.x, c.y, v, col);
      int nw = g.textWidth(v);
      trendArrow(g, c.x + nw + 6, c.y, ui.gr.zbHum, 5, 2);
      /* Only once there is a line to draw. sparkline() frames itself before
       * it checks whether it has points, so an empty rectangle would sit here
       * for the first hour after every boot looking like a broken widget. */
      int sx = c.x + nw + 20;
      if (ui.gr.zbHum.count >= 2) {
        sparkline(g, sx, c.y - 2, c.x + c.w - sx, c.h + 2,
                  ui.gr.zbHum, stale ? DIM : INFO);
      } else {
        g.setFont(&F_TEXT);
        textAt(g, sx, c.y + 4, "график копится", DIM);
      }
    } else {
      g.setFont(&F_BIG);
      textAt(g, c.x, c.y, "-", DIM);
    }
  }

  /* ── right lower: the two secondary numbers ──────────────────────────── */
  {
    Rect c = panelM(g, 148, 74, 168, 44, "давление / батарея");
    if (z.pressure > 0) {
      /* Pressure is the one reading here that is about the OUTDOORS - a
       * building leaks, so the needle tracks the atmosphere. mmHg because
       * that is the unit a Russian forecast quotes. */
      snprintf(v, sizeof(v), "%d", (z.pressure * 3) / 4);
      uint16_t pc = tend == barometer::TEND_FALL_FAST   ? WARN
                    : tend == barometer::TEND_RISE_FAST ? INFO
                                                        : TEXT;
      bigVal(g, c.x, c.y, v, "мм", stale ? DIM : pc);
    }
    if (z.battery >= 0) {
      bool lowBat = s.zbBattMin > 0 && z.battery <= s.zbBattMin;
      snprintf(v, sizeof(v), "%d", z.battery);
      bigVal(g, c.x + c.w, c.y, v, "%",
             stale ? DIM : (lowBat ? CRIT : TEXT), true);
    }
  }

  /* ── bottom, full width: what the sky is doing, and how fresh this is ─── */
  {
    /* No label on this one. "ПРОГНОЗ" above an arrow and the sentence
     * "Погода без перемен" was a caption introducing something that already
     * introduces itself, and the row it cost is the row the evidence needs.
     * The trend window moves to the right of the claim, where it is still
     * feedback for the long press but is not spending a line of its own. */
    Rect c = panelM(g, 4, 122, 312, 48);
    const char *line = nullptr;
    uint16_t lc = TEXT;
    int ax = c.x;
    if (ui.st.zbTrendOk) {
      line = barometer::forecast(tend);
      lc = barometer::headacheWatch(tend)   ? WARN
           : tend == barometer::TEND_RISE_FAST ? INFO
                                               : TEXT;
      ax += baroArrow(g, c.x, c.y + 4, barometer::direction(tend),
                      barometer::isSharp(tend), stale ? DIM : lc);
    } else {
      /* No trend yet is not "steady" - say which, or the screen claims the
       * weather is settled when it simply has not been watching long enough. */
      line = "Барометр "
             "копит "
             "историю";
      lc = DIM;
    }
    const char *win = ui.homeMode == 1   ? "сутки"
                      : ui.homeMode == 2 ? "неделя"
                                         : "барограф";
    g.setFont(&F_TEXT);
    int ww = g.textWidth(win);
    textRight(g, c.x + c.w, c.y, win, DIM);

    g.setFont(&F_MED);
    g.setTextSize(1);
    char clipped[48];
    clipW(g, line, clipped, sizeof(clipped), c.x + c.w - ax - ww - 8);
    textAt(g, ax, c.y, clipped, stale ? DIM : lc);

    /* Second row: the sensor, its freshness, and the 3-hour delta the claim
     * above was derived from - the statement and its evidence together. */
    g.setFont(&F_TEXT);
    char age[40];
    fmtAge(z, age, sizeof(age));
    int y2 = c.y + c.h - 10;
    textAt(g, c.x, y2, z.name[0] ? z.name : NOCT_ZB_NET_NAME,
           stale ? DIM : ORANGE);
    if (ui.st.zbTrendOk) {
      int dp = ui.st.zbPress10Delta3h;
      snprintf(v, sizeof(v), "%+d.%d гПа/3ч",
               dp / 10, abs(dp % 10));
      textAt(g, c.x + 116, y2, v, DIM);
    }
    textRight(g, c.x + c.w, y2, age, stale ? CRIT : DIM);
  }

  /* More sensors than this screen shows: say so rather than hide them. */
  if (ui.st.zb.count > 1) {
    g.setFont(&F_SMALL);
    snprintf(v, sizeof(v), "+%d на ПОГОДЕ",
             ui.st.zb.count - 1);
    textRight(g, NOCT_W - 8, 22, v, DIM);
  }
}

} // namespace scenes
