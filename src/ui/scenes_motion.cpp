/*
 * Nocturne C6 — ДВИЖЕНИЕ: the Aqara RTCGQ11LM motion + illuminance sensors.
 *
 *   y26..y82    two compact tiles, one per sensor: how long since, light,
 *               battery
 *   y88..y132   the day, half an hour per column, one row per sensor
 *   y138..y168  the presence strip
 *
 * The layout changed because the first one answered only "how long ago", and
 * a single number is nearly the whole of what a PIR can be made to say
 * badly. Motion is an EVENT: the useful fact is not seventeen minutes, it is
 * that the room was busy all morning and has been dead since three. So the
 * timeline gets the middle of the screen and the tiles shrink to fit what
 * they actually hold — they were 88 px tall around about fifty of content,
 * and the emptiness read as a screen that had lost its data.
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
#include <time.h>

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
 * by a type byte nobody sets. */
static bool isMotion(const ZbSensor &z) {
  return z.motionAgeSec >= 0 || z.lux >= 0;
}

/* Raw lux means little on its own: nobody knows whether 40 is dim or bright.
 * The word is the reading; the number is the evidence. */
static const char *lightWord(int lux) {
  if (lux < 0) return "";
  if (lux <= 3) return "темно";
  if (lux <= 40) return "сумерки";
  if (lux <= 200) return "свет";
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
    textCenter(g, NOCT_W / 2, 82, "RTCGQ11LM: кнопка под крышкой,", TEXT);
    textCenter(g, NOCT_W / 2, 98, "держать 5 сек до тройного мигания", TEXT);
    textCenter(g, NOCT_W / 2, 114, "потом коротко раз в 2 сек", DIM);
    int w = NOCT_W - 100;
    g.drawRect(50, 132, w, 6, ORANGE_DIM);
    g.fillRect(51, 133, (w - 2) * left / NOCT_ZB_JOIN_SEC, 4, GOOD);
    return;
  }

  g.setFont(&F_MED);
  textCenter(g, NOCT_W / 2, 44, "датчик движения не привязан", DIM);
  g.setFont(&F_TEXT);
  textCenter(g, NOCT_W / 2, 74, "Меню > Система > Подключить датчик", TEXT);
  textCenter(g, NOCT_W / 2, 92, "или  zb join  в консоли", DIM);
  textCenter(g, NOCT_W / 2, 114, "если не находится: вынуть батарейку", DIM);
  textCenter(g, NOCT_W / 2, 130, "на 10 секунд и повторить", DIM);
}

/* "СЕЙЧАС" / "25 мин" / "3 ч" — the age as a phrase, with the number and its
 * unit split so the caller can size them differently. */
static void ageWords(int age, char *num, size_t cap, const char **unit) {
  if (age < 60) {
    snprintf(num, cap, "%s", "СЕЙЧАС");
    *unit = "";
  } else if (age < 3600) {
    snprintf(num, cap, "%d", age / 60);
    *unit = "мин";
  } else if (age < 86400) {
    snprintf(num, cap, "%d", age / 3600);
    *unit = "ч";
  } else {
    snprintf(num, cap, "%d", age / 86400);
    *unit = "дн";
  }
}

/* One sensor, compactly: the room, how long since, and the two facts that
 * give it context. */
static void drawTile(LGFX_Sprite &g, int x, const ZbSensor &z) {
  char v[40];
  const int w = 154;
  panel(g, x, 26, w, 56, z.name[0] ? z.name : "ДАТЧИК");

  int age = z.motionAgeSec;
  if (age < 0) {
    g.setFont(&F_MED);
    textAt(g, x + 10, 40, "ждет первого", DIM);
    g.setFont(&F_TEXT);
    textAt(g, x + 10, 62, "движения", DIM);
  } else if (age < 60) {
    /* Inside the hardware lockout: "now" is the whole truth available.
     *
     * F_MED doubled rather than F_BIG. F_BIG and F_HUGE are logisoso — digit
     * fonts — and drawing a Cyrillic word in one produces a row of empty
     * boxes. Every hero on every other screen is a NUMBER, so that gap had
     * never been hit before this screen existed. */
    g.setFont(&F_MED);
    g.setTextSize(2);
    textAt(g, x + 10, 36, "СЕЙЧАС", GOOD);
    g.setTextSize(1);
  } else {
    const char *unit;
    ageWords(age, v, sizeof(v), &unit);
    uint16_t c = age < 600 ? TEXT : DIM;
    g.setFont(&F_HUGE);
    g.setTextSize(1);
    int nw = g.textWidth(v);
    textAt(g, x + 10, 32, v, c);
    g.setFont(&F_MED);
    textAt(g, x + 10 + nw + 5, 50, unit, DIM);
    /* No "назад". There is no room for it under a 32 px numeral in a 56 px
     * tile, and pushed to the top corner it read as an orphan belonging to
     * nothing. The tile is titled with the room and the strip below says
     * what the number means; a third copy of the same idea was the only
     * thing it added. */
  }

  /* Light and battery on one line at the foot of the tile: both are context
   * for the number above, neither earns a line of its own here. */
  g.setFont(&F_TEXT);
  if (z.lux >= 0) {
    snprintf(v, sizeof(v), "%d лк %s", z.lux, lightWord(z.lux));
    textAt(g, x + 10, 66, v, INFO);
  } else {
    textAt(g, x + 10, 66, "света нет данных", DIM);
  }
  if (z.battery >= 0) {
    snprintf(v, sizeof(v), "%d%%", z.battery);
    textRight(g, x + w - 10, 66, v, z.battery <= 15 ? CRIT : DIM);
  }
}

/* The day as a strip: one column per half hour, one row per sensor.
 *
 * This is the part a single "how long ago" cannot give you. It answers the
 * question people actually have — when was anyone here — and it answers it
 * without a word. */
static void drawDay(UiCtx &ui, int nSensors, const ZbSensor *const *m) {
  LGFX_Sprite &g = ui.g;
  panel(g, 4, 88, 312, 44, "СУТКИ");

  const int x0 = 12, wCol = 6, gap = 0;
  const int cols = MotionDay::kBuckets;
  const int rowH = nSensors > 1 ? 9 : 14;

  /* Now, so the strip can say where in the day we are. Without a clock the
   * buckets have no meaning and the strip is not drawn at all — a timeline
   * with no time is decoration. */
  int nowBucket = -1;
  time_t t = time(nullptr);
  if (t > 1700000000L) {
    struct tm tmv;
    if (localtime_r(&t, &tmv))
      nowBucket = (tmv.tm_hour * 60 + tmv.tm_min) / 30;
  }
  if (nowBucket < 0) {
    g.setFont(&F_TEXT);
    textAt(g, 14, 104, "нет часов - ленту не построить", DIM);
    return;
  }

  for (int s = 0; s < nSensors && s < 2; s++) {
    const MotionDay &d = ui.st.motionDay[s];
    int y = 100 + s * (rowH + 3);
    for (int i = 0; i < cols; i++) {
      int x = x0 + i * (wCol + gap);
      bool on = d.at(i);
      bool nowCol = (i == nowBucket);
      uint16_t c = on ? (nowCol ? GOOD : INFO) : lerp565(BG, DIM, nowCol ? 90 : 34);
      g.fillRect(x, y, wCol - 1, rowH, c);
    }
    /* The sensor's initial at the left, so two rows are told apart without a
     * legend eating the width. */
    if (nSensors > 1 && m[s] && m[s]->name[0]) {
      g.setFont(&F_SMALL);
      char ini[5] = {m[s]->name[0], m[s]->name[1], 0, 0, 0};
      /* one UTF-8 character, however many bytes it took */
      if ((uint8_t)ini[0] < 0x80) ini[1] = 0;
      textRight(g, x0 - 2, y + rowH / 2 - 4, ini, DIM);
    }
  }

  /* Hour marks under the strip: midnight, 06, 12, 18. Four labels is enough
   * to place anything and few enough not to crowd 48 columns. */
  g.setFont(&F_SMALL);
  for (int h = 0; h <= 18; h += 6) {
    int i = h * 2;
    char lbl[4];
    snprintf(lbl, sizeof(lbl), "%02d", h);
    textAt(g, x0 + i * (wCol + gap) - 3, 124, lbl, DIM);
  }
}

void drawMotion(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  char v[72];

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
    /* One paired, one still to come. A short line, not a tile full of
     * instructions: the instructions belong on the empty screen, where
     * somebody is actually trying to pair something. */
    panel(g, 162, 26, 154, 56, "ВТОРОЙ ДАТЧИК");
    g.setFont(&F_MED);
    textAt(g, 172, 42, "не привязан", DIM);
    g.setFont(&F_TEXT);
    textAt(g, 172, 66, "zb join в консоли", DIM);
  }

  drawDay(ui, found, m);

  /* ── the presence strip ──────────────────────────────────────────────── */
  panel(g, 4, 138, 312, 30, "ПРИСУТСТВИЕ");
  {
    int best = -1;
    const char *where = "";
    for (int i = 0; i < found; i++) {
      int a = m[i]->motionAgeSec;
      if (a < 0) continue;
      if (best < 0 || a < best) {
        best = a;
        where = m[i]->name;
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
    textAt(g, 12, 146, claim, c);

    /* The evidence: which room, and how much of the day had anyone in it.
     * The second half is what the strip above shows as a picture — saying it
     * in one number as well costs nothing and survives a glance from too far
     * away to resolve the columns. */
    g.setFont(&F_TEXT);
    if (best >= 0) {
      int busy = 0;
      for (int i = 0; i < found && i < 2; i++) {
        int n = ui.st.motionDay[i].count();
        if (n > busy) busy = n;
      }
      /* Short, and MEASURED against the claim rather than assumed to fit.
       * The first version put the room name here too and ran "Движение прямо
       * сейчас" straight through "за сутки активно 2%" — the claim is F_MED
       * at ten pixels a character and the tile has 288 to give, so anything
       * over about eight characters here collides. The room name is already
       * the tile's own title; repeating it was what cost the width. */
      snprintf(v, sizeof(v), "сутки %d%%",
               busy * 100 / MotionDay::kBuckets);
      g.setFont(&F_MED);
      int claimW = g.textWidth(claim);
      g.setFont(&F_TEXT);
      if (12 + claimW + 8 + g.textWidth(v) <= 308)
        textRight(g, 308, 150, v, DIM);
    }
  }
}

} // namespace scenes
