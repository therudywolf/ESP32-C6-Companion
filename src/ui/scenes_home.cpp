/*
 * Nocturne C6 — ДОМ: the climate screen for the ForestHome sensor.
 *
 * One sensor, by design. The ПОГОДА tile answers "and it's 23 inside"; this
 * answers what the room has been DOING — which needs the space a multi-sensor
 * grid was spending on columns nobody has.
 *
 * Layout, y26..y164:
 *   left half   temperature, hero-sized, with the tenth in small type
 *   right half  humidity, a comfort verdict, and the battery bar
 *   bottom      two sparklines side by side, each with its own hi/lo — a room
 *               drifting warm and a room getting damp are different problems
 *
 * A reading over an hour old dims EVERYTHING at once. Battery sensors go quiet
 * for half an hour at a time, and a stale number drawn as if it were current is
 * exactly the lie the "no signal" handling exists to prevent. A number outside
 * the owner's alert band wears the alert colour, so the screen agrees with the
 * toast that just fired instead of looking calm beside it.
 */
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
  const int h = 9;
  uint16_t frame = stale ? DIM : ORANGE_DIM;
  g.drawRect(x, y, w - 3, h, frame);
  g.fillRect(x + w - 3, y + 2, 2, h - 4, frame); /* the nub */
  int fill = (w - 5) * pct / 100;
  if (fill < 1 && pct > 0) fill = 1;
  uint16_t c = pct <= 20 ? CRIT : (pct <= 40 ? WARN : GOOD);
  g.fillRect(x + 1, y + 1, fill, h - 2, stale ? DIM : c);
}

/* A sparkline with its own scale and hi/lo labels. `div` scales the labels back
 * to human units (10 for temperature-x10, 1 for humidity). */
static void trendBox(LGFX_Sprite &g, int x, int y, int w, int h,
                     const RollingGraph &gr, int div, int flatSpan,
                     const char *label, uint16_t ink, bool stale) {
  g.setFont(&F_SMALL);
  textAt(g, x, y - 4, label, DIM);
  if (gr.count < 2) {
    textAt(g, x + 62, y - 4, "копится...", DIM);
    return;
  }
  int lo = gr.at(0), hi = gr.at(0);
  for (int i = 1; i < gr.count; i++) {
    int s = gr.at(i);
    if (s < lo) lo = s;
    if (s > hi) hi = s;
  }
  /* A steady room should look steady. Without a floor on the span, a tenth of
   * a degree of sensor noise fills the box and reads as a storm. */
  if (hi - lo < flatSpan) {
    int mid = (hi + lo) / 2;
    lo = mid - flatSpan / 2;
    hi = mid + flatSpan / 2;
  }
  char v[12];
  snprintf(v, sizeof(v), "%d", hi / div);
  textRight(g, x + w, y - 4, v, DIM);
  snprintf(v, sizeof(v), "%d", lo / div);
  textRight(g, x + w, y + h, v, DIM);

  const int gw = w - 24;
  for (int i = 1; i < gr.count; i++) {
    int x0 = x + (i - 1) * gw / (gr.count - 1);
    int x1 = x + i * gw / (gr.count - 1);
    int y0 = y + h - (gr.at(i - 1) - lo) * h / (hi - lo);
    int y1 = y + h - (gr.at(i) - lo) * h / (hi - lo);
    g.drawLine(x0, y0, x1, y1, stale ? DIM : ink);
  }
}

/* Nothing paired yet: say what to press, and count the join window down so
 * "press the button on the sensor" has a deadline attached. A screen that
 * stays blank until you guess the right menu item teaches nothing. */
static void drawEmpty(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  char v[72];

  g.setFont(&F_MED);
  textCenter(g, NOCT_W / 2, 32, NOCT_ZB_NET_NAME, ORANGE);

  int left = ui.st.zbJoinSecs;
  if (left > 0) {
    g.setFont(&F_BIG);
    snprintf(v, sizeof(v), "жду датчик: %d:%02d", left / 60, left % 60);
    textCenter(g, NOCT_W / 2, 60, v, GOOD);
    g.setFont(&F_TEXT);
    textCenter(g, NOCT_W / 2, 92, "зажми кнопку на датчике ~5 сек,", TEXT);
    textCenter(g, NOCT_W / 2, 110, "потом жми коротко раз в 2 сек", DIM);
    int w = NOCT_W - 80;
    g.drawRect(40, 132, w, 6, ORANGE_DIM);
    g.fillRect(41, 133, (w - 2) * left / NOCT_ZB_JOIN_SEC, 4, GOOD);
    return;
  }

  g.setFont(&F_TEXT);
  textCenter(g, NOCT_W / 2, 60, "датчик не привязан", DIM);
  textCenter(g, NOCT_W / 2, 86, "Меню > Система > Подключить датчик", TEXT);
  textCenter(g, NOCT_W / 2, 106, "или  zb join  в консоли", DIM);
  snprintf(v, sizeof(v), "координатор %s, канал %d",
           ui.st.link.zbUp ? "работает" : "не запущен", NOCT_ZB_CHANNEL);
  textCenter(g, NOCT_W / 2, 132, v, ui.st.link.zbUp ? GOOD : CRIT);
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
  char v[40];

  /* Header: which room, and how fresh. */
  g.setFont(&F_MED);
  textAt(g, 6, 24, z.name[0] ? z.name : NOCT_ZB_NET_NAME, stale ? DIM : ORANGE);
  char age[40];
  fmtAge(z, age, sizeof(age));
  g.setFont(&F_TEXT);
  textRight(g, NOCT_W - 6, 26, age, stale ? CRIT : DIM);

  /* ── temperature, left half ──────────────────────────────────────────── */
  if (z.temp10 != -32768) {
    uint16_t tc = TEXT;
    if (!stale && s.zbAlert) {
      if (s.zbTempMax < 99 && z.temp10 > s.zbTempMax * 10) tc = CRIT;
      else if (s.zbTempMin > -99 && z.temp10 < s.zbTempMin * 10) tc = INFO;
    }
    if (stale) tc = DIM;

    int whole = z.temp10 / 10, frac = z.temp10 % 10;
    if (frac < 0) frac = -frac;
    g.setFont(&F_HUGE);
    g.setTextSize(2);
    snprintf(v, sizeof(v), "%d", whole);
    /* Ink-anchored like the CPU/GPU hero tiles: the glyph box is taller than
     * the ink, so centring on the box leaves the number visibly low. */
    textAt(g, 8, 40 - (g.fontHeight() - 64) / 2, v, tc);
    int wNum = g.textWidth(v);
    g.setTextSize(1);
    g.setFont(&F_BIG);
    snprintf(v, sizeof(v), ",%d", frac);
    textAt(g, 8 + wNum + 2, 72, v, stale ? DIM : ORANGE);
    g.setFont(&F_MED);
    textAt(g, 8 + wNum + 2, 46, "C", DIM);
  } else {
    g.setFont(&F_BIG);
    textAt(g, 8, 58, "-", DIM);
  }

  /* ── humidity + battery, right half ──────────────────────────────────── */
  const int hx = 168;
  if (z.humidity >= 0) {
    uint16_t hc = INFO;
    if (!stale && s.zbAlert) {
      if (s.zbHumMax <= 100 && z.humidity > s.zbHumMax) hc = CRIT;
      else if (s.zbHumMin >= 0 && z.humidity < s.zbHumMin) hc = WARN;
    }
    if (stale) hc = DIM;
    g.setFont(&F_HUGE);
    snprintf(v, sizeof(v), "%d%%", z.humidity);
    textAt(g, hx, 38, v, hc);
    g.setFont(&F_TEXT);
    textAt(g, hx, 72, "влажность", DIM);
    /* 30–60% is the band everyone agrees on; outside it, say which way. */
    const char *verdict = z.humidity < 30   ? "сухо"
                          : z.humidity > 60 ? "сыро"
                                            : "норма";
    uint16_t vc = (z.humidity < 30 || z.humidity > 60) ? WARN : GOOD;
    textRight(g, NOCT_W - 8, 72, verdict, stale ? DIM : vc);
  }

  /* The WSDCGQ11LM has a barometer; the cheaper models do not, so this line
   * appears only when there is a reading behind it. mmHg, because that is the
   * unit a Russian forecast quotes. */
  if (z.pressure > 0) {
    g.setFont(&F_TEXT);
    snprintf(v, sizeof(v), "%d мм рт.ст.", (z.pressure * 3) / 4);
    textAt(g, 8, 96, v, DIM);
  }

  if (z.battery >= 0) {
    bool lowBat = s.zbBattMin > 0 && z.battery <= s.zbBattMin;
    g.setFont(&F_SMALL);
    snprintf(v, sizeof(v), "батарея %d%%", z.battery);
    textAt(g, hx, 92, v, stale ? DIM : (lowBat ? CRIT : DIM));
    /* F_SMALL's ink runs to y100; the bar started at y98 and struck the label
     * through. */
    batteryBar(g, hx, 104, NOCT_W - hx - 8, z.battery, stale);
  }

  /* ── the day so far: one trend per quantity ──────────────────────────── */
  /* 32 samples of a device that speaks every 20–60 minutes reach back most of
   * a day, which is the whole reason this screen exists rather than the tile. */
  trendBox(g, 8, 128, 140, 28, ui.gr.zbTemp, 10, 5, "температура",
           stale ? DIM : ORANGE, stale);
  trendBox(g, hx, 128, NOCT_W - hx - 8, 28, ui.gr.zbHum, 1, 6, "влажность",
           stale ? DIM : INFO, stale);

  /* More sensors than this screen shows: say so rather than hide them — the
   * ПОГОДА tile has room for the next one. */
  if (ui.st.zb.count > 1) {
    g.setFont(&F_SMALL);
    snprintf(v, sizeof(v), "+%d на ПОГОДЕ", ui.st.zb.count - 1);
    textRight(g, NOCT_W - 6, 114, v, DIM);
  }
}

} // namespace scenes
