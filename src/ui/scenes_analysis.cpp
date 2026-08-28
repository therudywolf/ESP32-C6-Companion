/*
 * Nocturne C6 — ДАВЛЕНИЕ: the three things the sensor measures, and what the
 * room is going to read later.
 *
 *   y26..y96    влажность | температура | давление — value and two windows
 *   y100..y170  прогноз по датчику, or why there isn't one
 *
 * ── Why it looks like this ────────────────────────────────────────────────
 *
 * It used to be five pressure windows in a strip over a findings panel, and
 * the owner's verdict was "тут как-то непонятно всё скомпоновано. Нужно тогда
 * 3 карточки. Влажность, температура, давление и отдельно прогноз погоды
 * карточка именно по датчику".
 *
 * He is right about the reason, not only the shape. The five-window strip
 * answered a question nobody had asked out loud — "compare the one-hour and
 * twelve-hour pressure trends" — while the three numbers the sensor actually
 * reports were nowhere on the screen that is named after them. Two windows
 * per quantity is what survives: three hours is the WMO tendency band, and
 * twenty-four is a day, and between them they say both "what is happening"
 * and "compared to yesterday" without needing a legend.
 *
 * ── The forecast is the PC's answer, not this file's ─────────────────────
 *
 * Predicting the room needs a lag model fitted over weeks of the room paired
 * against the street, which is roomcast.py on the server. What arrives here
 * is the answer and its band, or the refusal.
 *
 * Both are drawn. A card that goes blank when the model declines is a card
 * that looks broken; "мало данных: 41 пар из 60" is a sentence that tells the
 * owner exactly when to expect an answer, and it costs one line.
 */
#include <stdlib.h>

#include "core/Barometer.h"
#include "core/ClimateAnalysis.h"
#include "core/config.h"
#include "ui/Scenes.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace scenes {

using namespace theme;
using namespace widgets;

/* One signed change, formatted so the sign is the first thing read. Tenths,
 * because whole units hide everything a barometer does in three hours. */
static void fmtDelta(char *out, size_t cap, int d10, const char *unit) {
  int whole = abs(d10) / 10, frac = abs(d10) % 10;
  snprintf(out, cap, "%s%d.%d %s", d10 > 0 ? "+" : (d10 < 0 ? "-" : ""), whole,
           frac, unit);
}

/* A quantity card: label, the reading, and two windows under it.
 *
 * The windows are drawn in F_TEXT and the reading in F_BIG on purpose — the
 * whole card is one statement with its evidence, and evidence that competes
 * with the statement for size makes the eye choose. */
static void quantity(LGFX_Sprite &g, int x, int y, int w, int h,
                     const char *label, const char *value, const char *unit,
                     uint16_t col, const char *w3, const char *w24) {
  Rect c = panelM(g, x, y, w, h, label);
  g.setFont(&F_BIG);
  g.setTextSize(1);
  int nw = g.textWidth(value);
  int ty = inkY(INK_BIG, c.y, INK_BIG.height + 4);
  textAt(g, c.x, ty, value, col);
  if (unit && unit[0]) {
    g.setFont(&F_TEXT);
    /* On the number's baseline, so the pair reads as one reading rather than
     * as two things at different heights. */
    int base = ty + INK_BIG.top + INK_BIG.height;
    textAt(g, c.x + nw + 3, base - INK_TEXT.top - INK_TEXT.height, unit, DIM);
  }
  g.setFont(&F_TEXT);
  int wy = c.y + INK_BIG.height + 6;
  if (w3) textAt(g, c.x, wy, w3, DIM);
  if (w24) textAt(g, c.x, wy + INK_TEXT.box + 1, w24, DIM);
}

/* One horizon of the forecast: "через 3 ч", the value with its band, and the
 * humidity that goes with it. */
static void horizon(LGFX_Sprite &g, int x, int y, int w, int hours, int t10,
                    int sd10, int rh, bool haveStreet, int street10) {
  char v[40];
  g.setFont(&F_TEXT);
  g.setTextSize(1);
  snprintf(v, sizeof(v), "через %d ч", hours);
  textAt(g, x, y + 4, v, DIM);

  g.setFont(&F_MED);
  int whole = t10 / 10, frac = abs(t10 % 10);
  snprintf(v, sizeof(v), "%d,%d", whole, frac);
  textAt(g, x + 62, y, v, TEXT);
  int nw = g.textWidth(v);

  /* The band, drawn small. It is the honest half of the number and it must
   * not be the loud half: quoted at the same size it reads as a second
   * measurement rather than as this one's uncertainty.
   *
   * The plus-minus is DRAWN, not typed. None of the board's three Cyrillic
   * faces carries U+00B1 — they cover ASCII plus the Cyrillic block and
   * nothing else — so the character that makes this a band rather than a
   * second reading would have been a hollow rectangle. Five pixels of line
   * work says it and cannot go missing. */
  g.setFont(&F_TEXT);
  const int px = x + 66 + nw, py = y + 6;
  g.drawFastHLine(px, py + 2, 5, DIM);
  g.drawFastVLine(px + 2, py, 5, DIM);
  g.drawFastHLine(px, py + 6, 5, DIM);
  snprintf(v, sizeof(v), "%d,%d C", sd10 / 10, abs(sd10 % 10));
  textAt(g, px + 8, y + 4, v, DIM);

  int rx = x + w - 4;
  if (rh >= 0) {
    g.setFont(&F_MED);
    snprintf(v, sizeof(v), "%d%%", rh);
    textRight(g, rx, y, v, INFO);
    rx -= g.textWidth(v) + 8;
  }
  if (haveStreet) {
    g.setFont(&F_TEXT);
    snprintf(v, sizeof(v), "улица %d", (street10 + (street10 < 0 ? -5 : 5)) / 10);
    textRight(g, rx, y + 4, v, DIM);
  }
}

void drawAnalysis(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  const AppState &st = ui.st;
  const analysis::Windows &w = st.zbWin;
  char v[48], a[24], b[24];

  const bool have = st.zb.count > 0;
  const ZbSensor &z = have ? st.zb.list[0] : st.zb.list[0];

  /* ── три карточки: то, что датчик действительно меряет ───────────────── */
  /* 72, не 70: вторая строка окна ложилась чернилами на нижнюю кромку
   * карточки — на всех трёх сразу, потому что все три считались одинаково. */
  const int CW = 101, CH = 72;

  /* влажность */
  if (have && z.humidity >= 0) {
    snprintf(v, sizeof(v), "%d", z.humidity);
    snprintf(a, sizeof(a), w.okH3 ? "3ч %+d" : "3ч -", w.dH_3h);
    snprintf(b, sizeof(b), w.okH24 ? "сут %+d" : "сут -", w.dH_24h);
    quantity(g, 4, 26, CW, CH, "влажность", v, "%", INFO, a, b);
  } else {
    quantity(g, 4, 26, CW, CH, "влажность", "-", nullptr, DIM, nullptr,
             nullptr);
  }

  /* температура */
  if (have && z.temp10 != -32768) {
    snprintf(v, sizeof(v), "%d,%d", z.temp10 / 10, abs(z.temp10 % 10));
    if (w.okT3) fmtDelta(a, sizeof(a), w.dT10_3h, "за 3ч");
    else snprintf(a, sizeof(a), "3ч -");
    if (w.okT24) fmtDelta(b, sizeof(b), w.dT10_24h, "за сут");
    else snprintf(b, sizeof(b), "сут -");
    quantity(g, 109, 26, CW, CH, "температура", v, "C", TEXT, a, b);
  } else {
    quantity(g, 109, 26, CW, CH, "температура", "-", nullptr, DIM, nullptr,
             nullptr);
  }

  /* давление */
  if (have && z.pressure > 0) {
    auto tend = st.zbTrendOk ? barometer::classify(st.zbPress10Delta3h, 3)
                             : barometer::TEND_UNKNOWN;
    uint16_t pc = tend == barometer::TEND_FALL_FAST   ? WARN
                  : tend == barometer::TEND_RISE_FAST ? INFO
                                                      : TEXT;
    snprintf(v, sizeof(v), "%d", z.pressure);
    if (w.okP3) fmtDelta(a, sizeof(a), w.dP10_3h, "за 3ч");
    else snprintf(a, sizeof(a), "3ч -");
    if (w.okP24) fmtDelta(b, sizeof(b), w.dP10_24h, "за сут");
    else snprintf(b, sizeof(b), "сут -");
    quantity(g, 214, 26, 102, CH, "давление", v, "гПа", pc, a, b);
  } else {
    quantity(g, 214, 26, 102, CH, "давление", "-", nullptr, DIM, nullptr,
             nullptr);
  }

  /* ── прогноз по датчику ──────────────────────────────────────────────── */
  {
    /* Ярлык несёт СОСТОЯНИЕ. «прогноз по датчику» над строкой «прогноза пока
     * нет» — это заголовок, который спорит со своим содержимым, и он же
     * съедал ряд, которого потом не хватало причине отказа. */
    Rect c = panelM(g, 4, 102, 312, 68,
                    st.roomcast.ok ? "прогноз по датчику" : "прогноза пока нет");
    const RoomForecast &rc = st.roomcast;

    if (rc.ok) {
      horizon(g, c.x, c.y + 2, c.w, rc.hours[0], rc.temp10[0], rc.sd10[0],
              rc.rh[0], rc.haveStreet, rc.street10[0]);
      horizon(g, c.x, c.y + 22, c.w, rc.hours[1], rc.temp10[1], rc.sd10[1],
              rc.rh[1], rc.haveStreet, rc.street10[1]);
      g.setFont(&F_TEXT);
      g.setTextSize(1);
      if (rc.risk.length()) {
        /* The one line that is a PROBABILITY rather than a value. It only
         * appears when the model puts at least 15 % on it, so its presence
         * is itself the news. */
        textAt(g, c.x, c.y + 46, rc.risk.c_str(), WARN);
      } else if (st.zbPressPct >= 0) {
        snprintf(v, sizeof(v), "давление выше %d%% всех показаний за все время",
                 st.zbPressPct);
        textAt(g, c.x, c.y + 46, v, DIM);
      }
    } else {
      /* No forecast is a fact with a reason, and the reason is the useful
       * part: it says what is missing and therefore when to look again. */
      g.setFont(&F_TEXT);
      g.setTextSize(1);
      const char *why = rc.why.length()
                            ? rc.why.c_str()
                            : (rc.received ? "сервер не прислал причину"
                                           : "нет связи с сервером");
      textWrap(g, why, c.x, c.y + 2, c.w, 14, 2, DIM);
      if (st.zbFindCount > 0) {
        /* Something to say while the model warms up: the strongest pattern
         * the on-board analysis found, which needs no history but today's.
         * F_TEXT, not F_MED — на F_MED эта строка вылезала на восемь рядов
         * ниже карточки, а места между ней и причиной отказа нет. */
        const analysis::Finding &f = st.zbFind[0];
        uint16_t fc = f.severity >= 2 ? CRIT : (f.severity == 1 ? WARN : GOOD);
        char ft[48];
        clipW(g, f.title, ft, sizeof(ft), c.w);
        textAt(g, c.x, c.y + 30, ft, fc);
      }
    }
  }
}

} // namespace scenes
