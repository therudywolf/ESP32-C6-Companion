/*
 * Nocturne C6 — ДВИЖЕНИЕ: the Aqara RTCGQ11LM motion + illuminance sensors.
 *
 *   y26..y114  two tiles side by side, one per sensor
 *   y120..168  the presence strip: how long the flat has been quiet
 *
 * The one fact this hardware CANNOT report is "no motion". Aqara firmware
 * sends an occupancy report when the PIR fires and nothing at all otherwise —
 * there is no clear, no heartbeat, no "still empty". So every number here is
 * "how long since motion was last seen", never a verdict about whether anyone
 * is home. A person reading a book does not move enough for a PIR; claiming
 * the room is empty because the sensor is quiet would be the same lie the
 * stale-reading handling exists to prevent elsewhere.
 *
 * The hardware retrigger lockout is 60 s: two events closer together than that
 * are physically indistinguishable, which is why the freshest bucket is a
 * whole minute wide rather than pretending to second resolution.
 */
#include "core/config.h"
#include "ui/Scenes.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace scenes {

using namespace theme;
using namespace widgets;

/* A device is a motion sensor if it has ever reported either of the two things
 * only a motion sensor reports. Same convention `pressure` already uses to
 * mark a barometer: the kind is told by which fields are populated rather than
 * by a type byte nobody sets. A motion sensor paired but never yet triggered
 * looks like nothing at all — which is correct, because so far it IS nothing. */
static bool isMotion(const ZbSensor &z) {
  return z.motionAgeSec >= 0 || z.lux >= 0;
}

/* y to pass to textAt so the visible glyph TOP lands at wantTop — logisoso
 * carries its leading above the ink. Same helper ДОМ and CPU keep file-local. */
static int inkTop(LGFX_Sprite &g, int wantTop, int inkH) {
  int off = g.fontHeight() - inkH;
  if (off < 0) off = 0;
  return wantTop - off / 2;
}

/* Raw lux means little on its own: nobody knows whether 40 is dim or bright.
 * The word is the reading; the number is the evidence. */
static const char *lightWord(int lux) {
  if (lux < 0) return "";
  if (lux <= 3) return "темно";
  if (lux <= 40) return "сумерки";
  if (lux <= 200) return "свет в комнате";
  return "светло";
}

/* Nothing paired: say exactly which button on which device, and how long the
 * window stays open. A blank screen teaches nothing. */
static void drawEmpty(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  char v[80];
  panel(g, 6, 26, 308, 124, "ДАТЧИКИ ДВИЖЕНИЯ");

  int left = ui.st.zbJoinSecs;
  if (left > 0) {
    g.setFont(&F_BIG);
    snprintf(v, sizeof(v), "жду датчик: %d:%02d", left / 60, left % 60);
    textCenter(g, NOCT_W / 2, 46, v, GOOD);
    g.setFont(&F_TEXT);
    textCenter(g, NOCT_W / 2, 82, "RTCGQ11LM: жми кнопку под крышкой", TEXT);
    textCenter(g, NOCT_W / 2, 100, "3 раза с паузой в секунду", DIM);
    int w = NOCT_W - 100;
    g.drawRect(50, 124, w, 6, ORANGE_DIM);
    g.fillRect(51, 125, (w - 2) * left / NOCT_ZB_JOIN_SEC, 4, GOOD);
    return;
  }

  g.setFont(&F_MED);
  textCenter(g, NOCT_W / 2, 44, "датчики движения не привязаны", DIM);
  g.setFont(&F_TEXT);
  textCenter(g, NOCT_W / 2, 74, "Меню > Система > Подключить датчик", TEXT);
  textCenter(g, NOCT_W / 2, 92, "или  zb join  в консоли", DIM);
  textCenter(g, NOCT_W / 2, 112, "привязывать по одному: второй после", DIM);
  textCenter(g, NOCT_W / 2, 128, "того, как первый показал движение", DIM);
}

/* One sensor. The hero is the time since motion, because that is the only
 * thing the device actually knows. */
static void drawTile(LGFX_Sprite &g, int x, const ZbSensor &z) {
  char v[40];
  const int w = 154;
  panel(g, x, 26, w, 88, z.name[0] ? z.name : "ДАТЧИК");

  int age = z.motionAgeSec;
  if (age < 0) {
    g.setFont(&F_MED);
    textAt(g, x + 10, 52, "ждет первого", DIM);
    textAt(g, x + 10, 72, "движения", DIM);
  } else if (age < 60) {
    /* Inside the hardware lockout: "now" is the whole truth available.
     *
     * F_MED doubled rather than F_BIG. F_BIG and F_HUGE are logisoso - digit
     * fonts - and drawing a Cyrillic word in one produces a row of empty
     * boxes, which is exactly what the first screenshot of this screen
     * showed. Every hero on every other screen is a NUMBER, so the gap had
     * never been hit before. */
    g.setFont(&F_MED);
    g.setTextSize(2);
    textAt(g, x + 10, 44, "СЕЙЧАС", GOOD);
    g.setTextSize(1);
    g.setFont(&F_TEXT);
    textAt(g, x + 10, 80, "движение в комнате", GOOD);
  } else {
    int n = age < 3600 ? age / 60 : age / 3600;
    const char *unit = age < 3600 ? "мин" : "ч";
    uint16_t c = age < 600 ? TEXT : DIM;
    g.setFont(&F_HUGE);
    g.setTextSize(1);
    snprintf(v, sizeof(v), "%d", n);
    int nw = g.textWidth(v);
    textAt(g, x + 10, inkTop(g, 40, 32), v, c);
    g.setFont(&F_MED);
    textAt(g, x + 10 + nw + 5, 58, unit, DIM);
    g.setFont(&F_TEXT);
    textAt(g, x + 10, 80, "назад", DIM);
  }

  /* Light and battery share the bottom rule: both are context for the number
   * above, neither is worth a line of its own on a tile this size. */
  g.drawFastHLine(x + 8, 92, w - 16, lerp565(PANEL, DIM, 90));
  g.setFont(&F_TEXT);
  if (z.lux >= 0) {
    snprintf(v, sizeof(v), "%d лк", z.lux);
    textAt(g, x + 10, 97, v, INFO);
    int used = g.textWidth(v);
    g.setFont(&F_SMALL);
    textAt(g, x + 10 + used + 6, 100, lightWord(z.lux), DIM);
  } else {
    textAt(g, x + 10, 97, "света нет данных", DIM);
  }
  if (z.battery >= 0) {
    g.setFont(&F_TEXT);
    snprintf(v, sizeof(v), "%d%%", z.battery);
    textRight(g, x + w - 10, 97, v, z.battery <= 15 ? CRIT : DIM);
  }
}

void drawMotion(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  char v[72];

  /* Collect the motion sensors out of the shared list. They share it with the
   * climate sensor, and their order there is join order, not meaning. */
  const ZbSensor *m[2] = {nullptr, nullptr};
  int found = 0;
  for (int i = 0; i < ui.st.zb.count && found < 2; i++)
    if (isMotion(ui.st.zb.list[i])) m[found++] = &ui.st.zb.list[i];

  if (found == 0) {
    drawEmpty(ui);
    return;
  }

  drawTile(g, 4, *m[0]);
  if (found > 1) {
    drawTile(g, 162, *m[1]);
  } else {
    /* One paired, one still to come. Say so rather than leaving a hole the
     * reader has to interpret. */
    panel(g, 162, 26, 154, 88, "ВТОРОЙ ДАТЧИК");
    g.setFont(&F_MED);
    textAt(g, 172, 54, "не привязан", DIM);
    g.setFont(&F_TEXT);
    textAt(g, 172, 80, "zb join, потом", DIM);
    textAt(g, 172, 96, "кнопка 3 раза", DIM);
  }

  /* ── the presence strip ──────────────────────────────────────────────── */
  panel(g, 4, 120, 312, 48, "ПРИСУТСТВИЕ");
  {
    /* Freshest motion across every sensor: the flat is as awake as its most
     * recently triggered room. */
    int best = -1, bestLux = -1;
    const char *where = "";
    for (int i = 0; i < found; i++) {
      int a = m[i]->motionAgeSec;
      if (a < 0) continue;
      if (best < 0 || a < best) {
        best = a;
        where = m[i]->name;
        /* The light must come from the SAME sensor as the name beside it.
         * Taking whichever sensor happened to be last in the list made the
         * strip say "Прихожая" and then quote the kitchen's darkness - two
         * true facts assembled into a false sentence. */
        bestLux = m[i]->lux;
      }
    }

    const char *claim;
    uint16_t c;
    if (best < 0) {
      claim = "Движения еще не было";
      c = DIM;
    } else if (best < 60) {
      claim = "Движение прямо сейчас";
      c = GOOD;
    } else if (best < 600) {
      claim = "Кто-то рядом";
      c = GOOD;
    } else if (best < 3600) {
      snprintf(v, sizeof(v), "Тихо %d мин", best / 60);
      claim = v;
      c = TEXT;
    } else {
      snprintf(v, sizeof(v), "Тихо %d ч", best / 3600);
      claim = v;
      c = DIM;
    }
    g.setFont(&F_MED);
    textAt(g, 12, 132, claim, c);

    /* The evidence line, in the ДОМ strip's grammar: which sensor, how long,
     * how bright — the facts the claim above was made from. */
    g.setFont(&F_TEXT);
    if (best >= 0) {
      textAt(g, 12, 154, where, DIM);
      if (bestLux >= 0) {
        snprintf(v, sizeof(v), "%s, %d лк", lightWord(bestLux), bestLux);
        textRight(g, 308, 154, v, DIM);
      }
    } else {
      textAt(g, 12, 154, "датчик на связи, но еще не срабатывал", DIM);
    }
  }
}

} // namespace scenes
