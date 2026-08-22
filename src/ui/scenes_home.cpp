/*
 * Nocturne C6 — ДОМ: the climate dashboard for the board's own Zigbee sensors.
 *
 * The ПОГОДА screen borrows one forecast tile for the room, which is the right
 * size for "and it's 23 inside". This screen is the other half: what the house
 * is doing over time, with the humidity and the battery that the tile has no
 * room for.
 *
 * Three layouts, chosen by how many sensors are paired:
 *   0  — how to pair one, with the join countdown when the window is open.
 *        A screen that is simply blank until you guess the right menu item is
 *        a screen that teaches nothing.
 *   1  — hero: big temperature, humidity, battery, and a day-long sparkline.
 *   2+ — a column per sensor, same numbers, no sparkline (no room, and the
 *        history is only kept for the first one anyway).
 *
 * Everything dims as one when a reading is over an hour old. Battery sensors
 * go quiet for half an hour at a time, and a stale number drawn as if it were
 * current is exactly the lie the "no signal" handling exists to prevent.
 */
#include "core/config.h"
#include "ui/Scenes.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace scenes {

using namespace theme;
using namespace widgets;

/* Over an hour: the sensor has missed at least one reporting interval. */
static bool isStale(const ZbSensor &z) {
  return z.ageSec < 0 || z.ageSec > 3600;
}

/* "2 мин", "3 ч", "вчера" — an age is only useful if it reads as one. */
static void fmtAge(const ZbSensor &z, char *out, size_t cap) {
  if (z.ageSec < 0) {
    snprintf(out, cap, "давность неизвестна");
  } else if (z.ageSec < 90) {
    snprintf(out, cap, "только что");
  } else if (z.ageSec < 3600) {
    snprintf(out, cap, "%d мин назад", z.ageSec / 60);
  } else if (z.ageSec < 86400) {
    snprintf(out, cap, "%d ч назад", z.ageSec / 3600);
  } else {
    snprintf(out, cap, "%d дн назад", z.ageSec / 86400);
  }
}

/* A short horizontal cell, drawn only when the sensor actually reported it. */
static void batteryBar(LGFX_Sprite &g, int x, int y, int w, int pct,
                       bool stale) {
  if (pct < 0) return;
  const int h = 9;
  uint16_t frame = stale ? DIM : ORANGE_DIM;
  g.drawRect(x, y, w - 3, h, frame);
  g.fillRect(x + w - 3, y + 2, 2, h - 4, frame); /* the nub */
  int fill = (w - 5) * pct / 100;
  if (fill < 1 && pct > 0) fill = 1;
  uint16_t c = pct <= 20 ? CRIT : (pct <= 40 ? WARN : GOOD);
  g.fillRect(x + 1, y + 1, fill, h - 2, stale ? DIM : c);
}

/* Nothing paired yet: say what to press, and count the window down when it is
 * open so "press the button on the sensor" has a deadline attached. */
static void drawEmpty(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  char v[72];

  g.setFont(&F_MED);
  textCenter(g, NOCT_W / 2, 34, NOCT_ZB_NET_NAME, ORANGE);

  int left = ui.st.zbJoinSecs;
  if (left > 0) {
    g.setFont(&F_BIG);
    snprintf(v, sizeof(v), "жду датчик: %d:%02d", left / 60, left % 60);
    textCenter(g, NOCT_W / 2, 62, v, GOOD);
    g.setFont(&F_TEXT);
    textCenter(g, NOCT_W / 2, 92, "зажми кнопку на датчике ~5 сек", TEXT);
    textCenter(g, NOCT_W / 2, 110, "и жми её коротко раз в 2 сек,", DIM);
    textCenter(g, NOCT_W / 2, 126, "пока идёт привязка", DIM);
    /* A bar that empties is worth more than the digits alone. */
    int w = NOCT_W - 80;
    g.drawRect(40, 140, w, 6, ORANGE_DIM);
    g.fillRect(41, 141, (w - 2) * left / NOCT_ZB_JOIN_SEC, 4, GOOD);
    return;
  }

  g.setFont(&F_TEXT);
  textCenter(g, NOCT_W / 2, 62, "датчиков пока нет", DIM);
  textCenter(g, NOCT_W / 2, 88, "Меню > Система > Подключить датчик", TEXT);
  textCenter(g, NOCT_W / 2, 108, "или  zb join  в консоли", DIM);
  snprintf(v, sizeof(v), "координатор %s, канал %d",
           ui.st.link.zbUp ? "работает" : "не запущен", NOCT_ZB_CHANNEL);
  textCenter(g, NOCT_W / 2, 134, v, ui.st.link.zbUp ? GOOD : CRIT);
}

/* One sensor: room to show it properly. */
static void drawHero(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  const ZbSensor &z = ui.st.zb.list[0];
  bool stale = isStale(z);
  uint16_t ink = stale ? DIM : TEXT;
  char v[40];

  /* Name across the top, so the screen says WHICH room before it says what. */
  g.setFont(&F_MED);
  textAt(g, 6, 24, z.name[0] ? z.name : NOCT_ZB_NET_NAME, stale ? DIM : ORANGE);
  char age[40];
  fmtAge(z, age, sizeof(age));
  g.setFont(&F_TEXT);
  textRight(g, NOCT_W - 6, 26, age, stale ? CRIT : DIM);

  /* Temperature, ink-anchored the way the CPU/GPU hero tiles are: the glyph
   * box is taller than the ink, so centring on the box leaves it visibly low. */
  if (z.temp10 != -32768) {
    g.setFont(&F_HUGE);
    g.setTextSize(2);
    int whole = z.temp10 / 10, frac = z.temp10 % 10;
    if (frac < 0) frac = -frac;
    snprintf(v, sizeof(v), "%d", whole);
    textAt(g, 8, 44 - (g.fontHeight() - 64) / 2, v, ink);
    int wNum = g.textWidth(v);
    g.setTextSize(1);
    /* The tenth in small type: the extra digit matters for a room, but not
     * enough to halve the size of the number you read from across the desk. */
    g.setFont(&F_BIG);
    snprintf(v, sizeof(v), ",%d", frac);
    textAt(g, 8 + wNum + 2, 76, v, stale ? DIM : ORANGE);
    g.setFont(&F_MED);
    textAt(g, 8 + wNum + 2, 50, "C", DIM);
  } else {
    g.setFont(&F_BIG);
    textAt(g, 8, 60, "-", DIM);
  }

  /* Humidity to the right of the temperature, same baseline family. */
  const int hx = 150;
  if (z.humidity >= 0) {
    g.setFont(&F_BIG);
    snprintf(v, sizeof(v), "%d%%", z.humidity);
    textAt(g, hx, 44, v, stale ? DIM : INFO);
    g.setFont(&F_TEXT);
    textAt(g, hx, 74, "влажность", DIM);
    /* Comfort band, because 30% and 60% look equally like "a number". */
    const char *verdict = z.humidity < 30   ? "сухо"
                          : z.humidity > 60 ? "сыро"
                                            : "норма";
    uint16_t vc = (z.humidity < 30 || z.humidity > 60) ? WARN : GOOD;
    textAt(g, hx + 96, 74, verdict, stale ? DIM : vc);
  }

  /* Battery under the humidity. */
  if (z.battery >= 0) {
    g.setFont(&F_TEXT);
    snprintf(v, sizeof(v), "батарея %d%%", z.battery);
    textAt(g, hx, 98, v, stale ? DIM : (z.battery <= 20 ? CRIT : TEXT));
    batteryBar(g, hx, 112, 100, z.battery, stale);
  }

  /* The day so far. 32 samples of a device that speaks every 20-60 minutes is
   * most of a day, which is the whole reason this screen exists rather than
   * just the ПОГОДА tile. */
  const RollingGraph &gr = ui.gr.zbTemp;
  if (gr.count >= 2) {
    const int gx = 8, gy = 118, gw = 130, gh = 28;
    int lo = gr.at(0), hi = gr.at(0);
    for (int i = 1; i < gr.count; i++) {
      int s = gr.at(i);
      if (s < lo) lo = s;
      if (s > hi) hi = s;
    }
    if (hi - lo < 5) { /* a flat room should look flat, not like noise */
      int mid = (hi + lo) / 2;
      lo = mid - 3;
      hi = mid + 3;
    }
    for (int i = 1; i < gr.count; i++) {
      int x0 = gx + (i - 1) * gw / (gr.count - 1);
      int x1 = gx + i * gw / (gr.count - 1);
      int y0 = gy + gh - (gr.at(i - 1) - lo) * gh / (hi - lo);
      int y1 = gy + gh - (gr.at(i) - lo) * gh / (hi - lo);
      g.drawLine(x0, y0, x1, y1, stale ? DIM : ORANGE);
    }
    g.setFont(&F_SMALL);
    snprintf(v, sizeof(v), "%d,%d", hi / 10, (hi % 10 + 10) % 10);
    textAt(g, gx + gw + 4, gy - 2, v, DIM);
    snprintf(v, sizeof(v), "%d,%d", lo / 10, (lo % 10 + 10) % 10);
    textAt(g, gx + gw + 4, gy + gh - 10, v, DIM);
  }
}

/* Two to four sensors: a column each, no sparkline. */
static void drawColumns(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  int n = ui.st.zb.count;
  if (n > ZigbeeData::kMax) n = ZigbeeData::kMax;
  int w = NOCT_W / n;
  char v[32];

  for (int i = 0; i < n; i++) {
    const ZbSensor &z = ui.st.zb.list[i];
    bool stale = isStale(z);
    int x = i * w, cx = x + w / 2;
    if (i) g.drawFastVLine(x, 26, 120, ORANGE_DIM);

    char tab[18];
    g.setFont(&F_SMALL);
    clipW(g, z.name[0] ? z.name : "-", tab, sizeof(tab), w - 4);
    textCenter(g, cx, 26, tab, stale ? DIM : ORANGE);

    if (z.temp10 != -32768) {
      g.setFont(&F_BIG);
      int whole = z.temp10 / 10, frac = z.temp10 % 10;
      if (frac < 0) frac = -frac;
      snprintf(v, sizeof(v), "%d,%d", whole, frac);
      textCenter(g, cx, 48, v, stale ? DIM : TEXT);
    }
    if (z.humidity >= 0) {
      g.setFont(&F_MED);
      snprintf(v, sizeof(v), "%d%%", z.humidity);
      textCenter(g, cx, 84, v, stale ? DIM : INFO);
    }
    if (z.battery >= 0) batteryBar(g, cx - 24, 110, 48, z.battery, stale);

    char age[40];
    fmtAge(z, age, sizeof(age));
    clipW(g, age, tab, sizeof(tab), w - 6);
    g.setFont(&F_SMALL);
    textCenter(g, cx, 128, tab, stale ? CRIT : DIM);
  }
}

void drawHome(UiCtx &ui) {
  if (ui.st.zb.count <= 0) {
    drawEmpty(ui);
  } else if (ui.st.zb.count == 1) {
    drawHero(ui);
  } else {
    drawColumns(ui);
  }
}

} // namespace scenes
