/*
 * Nocturne C6 — climate patterns: reading more out of the same three numbers.
 *
 * Barometer.h answers one question over one window: what is the needle doing
 * per the WMO three-hour bands. That is the right answer to the right
 * question, and it is also the only question the board could ask, because
 * three hours was the only window anyone computed.
 *
 * The card holds months. This file is what becomes possible once you look at
 * several windows at once and let them disagree.
 *
 * The three ideas that make the difference:
 *
 *   1. SHAPE, not level. A fall of 4 hPa means one thing when the last hour
 *      fell hardest and the opposite when the last hour has already turned
 *      around. One window cannot tell those apart; two can.
 *   2. AGREEMENT between windows. A short window and a long window pointing
 *      opposite ways is not noise — it is the signature of a front passing.
 *   3. The board's OWN history as the yardstick. Absolute pressure is
 *      unusable here (see the altitude note below), but "the lowest this room
 *      has seen in a month" needs no calibration at all.
 *
 * ── What is deliberately NOT here ──────────────────────────────────────────
 *
 * ALTITUDE. Everything the sensor reports is STATION pressure, and reducing
 * it to sea level needs an elevation nobody has entered. So no textbook
 * threshold ("below 1000 hPa is a low") appears anywhere in this file: at
 * 150 m the same weather reads 18 hPa lower than at sea level, and a constant
 * offset would silently misclassify every single reading. Where an absolute
 * level is wanted, `percentile()` answers with the room's own distribution
 * instead, which is offset-free by construction.
 *
 * THE ATMOSPHERIC TIDE. The semidiurnal solar tide S2 is a real, clock-like
 * pressure wave, and correcting for it would look impressive. Its amplitude
 * falls off roughly as the cube of the cosine of latitude: about 1.2 hPa at
 * the equator, but near 0.25 hPa at 55 degrees north. Peak to peak that is
 * under half a hPa — already inside the WMO "steady" band. Subtracting it
 * here would be false precision dressed up as rigour, so it is left out and
 * said so.
 *
 * WEATHER FROM ROOM TEMPERATURE. A building leaks pressure but holds heat.
 * Indoor temperature describes the radiator and the window, never the sky.
 * It appears below only in patterns ABOUT the room - ventilation, heating,
 * condensation - and never as evidence about the weather.
 *
 * ── WHERE THE SENSOR IS, AND WHY IT DECIDES WHAT CAN BE SAID ──────────────
 *
 * The WSDCGQ11LM sits in the MIDDLE OF THE ROOM. That is the best place for
 * measuring the air and the worst place for guessing about surfaces, and
 * every conclusion below is shaped by that one fact.
 *
 * What it buys:
 *   - the readings are the room's BULK AIR, not a microclimate. No sun on the
 *     case, no radiator radiating into it, no draught off the window frame.
 *     Nothing here has to be corrected for a bad position, and a spike is a
 *     real event rather than the afternoon sun crossing a shelf.
 *   - the humidity is the air people actually breathe, so the comfort and
 *     health bands below apply to it directly.
 *
 * What it costs:
 *   - IT CANNOT SEE A SINGLE SURFACE. Condensation happens where a wall is
 *     cold, and the coldest wall is by definition not in the middle of the
 *     room. Every condensation verdict here therefore rests on an ASSUMED
 *     wall-to-air difference (kColdSurfaceDelta10), and the error runs one
 *     way: a room-centre sensor UNDERSTATES the risk, never overstates it.
 *     That is the safe direction to be wrong in, and it is still wrong.
 *   - IT LAGS. The room's air has thermal mass; an opened window shows up
 *     minutes late and damped, and a radiator switching on does the same.
 *     So the one-hour window is the shortest one worth trusting for the room,
 *     and anything claiming to detect an event "just now" from air alone is
 *     claiming more than the physics allows.
 *
 * ── RELATIVE HUMIDITY IS NOT A MEASURE OF WATER ───────────────────────────
 *
 * This is the single most useful thing in this file. Relative humidity is a
 * ratio against what the air COULD hold, and that capacity roughly doubles
 * every ten degrees. Heat a sealed room and RH falls with not one gram of
 * water having left. Every "the air got dry" reading in winter is mostly
 * this, and treating it as a humidifier problem is treating the thermometer.
 *
 * absHumidity10() answers the question RH cannot: how much water is actually
 * in the air. It moves only when water enters or leaves - people, cooking,
 * laundry, an open window, condensation on a wall. That is what makes it,
 * not RH, the right signal for "something is happening in this room".
 */
#ifndef NOCT_CLIMATE_ANALYSIS_H
#define NOCT_CLIMATE_ANALYSIS_H

/* No Arduino.h on purpose: nothing here needs String, Serial or a pin. That
 * keeps the whole module compilable on a host, which is the only reason its
 * thresholds have tests at all - waiting for a real front to check a
 * threshold is not a plan. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>

namespace analysis {

/* ── inputs ──────────────────────────────────────────────────────────────
 * Change over each window, in TENTHS (of hPa, of degrees) so a 0.7 drift is
 * not rounded away. Each carries its own validity flag: "no history that old
 * yet" and "no change" are different answers, and a zero standing in for the
 * first is the single easiest way to make this whole file lie. */
struct Windows {
  int dP10_1h = 0, dP10_3h = 0, dP10_6h = 0, dP10_12h = 0, dP10_24h = 0;
  bool okP1 = false, okP3 = false, okP6 = false, okP12 = false, okP24 = false;
  int dT10_1h = 0, dT10_3h = 0, dT10_6h = 0, dT10_24h = 0;
  bool okT1 = false, okT3 = false, okT6 = false, okT24 = false;
  int dH_1h = 0, dH_3h = 0, dH_6h = 0, dH_24h = 0;
  bool okH1 = false, okH3 = false, okH6 = false, okH24 = false;
};

/* ── dew point ───────────────────────────────────────────────────────────
 * Magnus-Tetens, the standard approximation, good to about 0.4 degrees over
 * the range a room ever sees.
 *
 * This is the number relative humidity cannot give you. 65 % at 18 degrees
 * and 65 % at 25 degrees are the same reading and completely different
 * situations: mould does not grow because air is humid, it grows because a
 * cold surface is below the dew point and water condenses on it. Relative
 * humidity alone cannot see that; the dew point is exactly it.
 *
 * Returns tenths of a degree. Returns -9999 when the inputs cannot support
 * an answer. */
inline int dewPoint10(int temp10, int rh) {
  if (temp10 == -32768 || rh <= 0 || rh > 100) return -9999;
  const float a = 17.27f, b = 237.7f;
  float t = temp10 / 10.0f;
  float g = logf(rh / 100.0f) + (a * t) / (b + t);
  float td = (b * g) / (a - g);
  if (td < -60.0f || td > 60.0f) return -9999;
  return (int)(td * 10.0f + (td < 0 ? -0.5f : 0.5f));
}

/* Absolute humidity in TENTHS of a gram per cubic metre.
 *
 * Saturation vapour pressure by the Magnus form, then the ideal-gas step to
 * mass per volume - the standard chain, good to well under a percent over
 * the range a room ever sees, which is far better than the sensor itself.
 *
 * The point of having it: RH answers "how close is this air to its own
 * limit", which changes when you merely heat the air. This answers "how much
 * water is in it", which changes only when water moves. Two readings an hour
 * apart with the same absolute humidity and different RH mean the heating
 * came on, not that anything dried out.
 *
 * Returns -9999 when the inputs cannot support an answer. */
inline int absHumidity10(int temp10, int rh) {
  if (temp10 == -32768 || rh <= 0 || rh > 100) return -9999;
  float t = temp10 / 10.0f;
  /* saturation vapour pressure, hPa */
  float es = 6.112f * expf((17.67f * t) / (t + 243.5f));
  /* g/m3 = es * RH * 2.1674 / (273.15 + T), the ideal-gas constant folded in */
  float ah = (es * rh * 2.1674f) / (273.15f + t);
  if (ah < 0.0f || ah > 60.0f) return -9999;
  return (int)(ah * 10.0f + 0.5f);
}

/* How much colder the coldest surface in a room runs than the air. An outside
 * wall or a window in a heated flat sits roughly 3-5 degrees under room
 * temperature; 4 is the middle of that and the number building-physics
 * guidance uses for a rule of thumb. It is an ASSUMPTION, stated here rather
 * than buried, because every condensation verdict below rests on it. */
static const int kColdSurfaceDelta10 = 40;

/* A NOTE ON ONE LETTER. The lowercase Cyrillic "e oborotnoye" is absent from
 * the F_TEXT face (haxrcorp4089) and draws as an empty box. Its capital form
 * is present, and every other face has both. The strings below are the small
 * print of the analysis screen and are drawn in F_TEXT, so they are written
 * without it - which is why a couple of them phrase things at a slight angle
 * to the obvious wording. Found by drawing the alphabet on the device; it is
 * not visible in the source, where the letter looks like any other.
 */
/* ── the patterns ────────────────────────────────────────────────────────── */
enum PatternId {
  PAT_NONE = 0,
  /* pressure, shape over several windows */
  PAT_STORM_IMMINENT,    /* very rapid fall, happening now */
  PAT_DEEPENING,         /* sustained multi-hour fall: a low is settling in */
  PAT_FALL_ACCELERATING, /* the last hour fell harder than the three before */
  PAT_FALL_EASING,       /* still falling, but slower: the worst has passed */
  PAT_FRONT_PASSED,      /* fell for hours, now rising: the trough went by */
  PAT_RIDGE_BUILDING,    /* sustained rise: high pressure moving in */
  PAT_RADIATIVE_NIGHT,   /* ridge + clearing: a cold night, frost in winter */
  PAT_PRESSURE_LOW,      /* low against the room's OWN record */
  PAT_PRESSURE_HIGH,
  PAT_HEADACHE_WATCH,    /* rate of fall in the range people report noticing */
  /* the room itself */
  PAT_CONDENSATION,      /* dew point at or above the cold-surface estimate */
  PAT_MOULD_WATCH,       /* close to it, and has been for a while */
  PAT_AIR_TOO_DRY,
  PAT_VENTILATION,       /* temperature dropping while pressure sits still */
  PAT_HEATING_UP,
  /* Water in the air, as opposed to the ratio. These are the ones RH alone
   * cannot express, and they are the reason absHumidity10 exists. */
  PAT_DRY_IS_JUST_HEAT,  /* RH fell, but the water did not: the heating came on */
  PAT_WATER_ENTERING,    /* absolute humidity climbing: cooking, laundry, people */
  PAT_AIRED_OUT,         /* absolute humidity dropped hard: the room was aired */
  /* Bands with a published basis, quoted rather than invented. */
  PAT_MITE_ZONE,         /* sustained above 60 %: dust mites breed */
  PAT_COLD_FOR_HEALTH,   /* under 18 C, the WHO minimum for a living room */
  PAT_WARM_FOR_SLEEP,    /* over 24 C at night: measurably worse sleep */
  /* Against the room's OWN record, the same trick the barometer uses. */
  PAT_TEMP_UNUSUAL,
  PAT_HUMIDITY_UNUSUAL,
  PAT_COUNT
};

struct Finding {
  PatternId id = PAT_NONE;
  /* 0 = worth knowing, 1 = worth a glance, 2 = worth interrupting for.
   * Nothing in this file reaches the level that takes over the screen: none
   * of it is an emergency, and treating weather like one is how an alert
   * channel gets muted. */
  int severity = 0;
  const char *title = "";
  const char *detail = "";
};

/* Rate over a window, normalised to tenths of hPa per 3 h so the WMO numbers
 * keep meaning what they are quoted to mean regardless of which window fed
 * them. */
inline int per3h(int d10, int hours) {
  if (hours < 1) return 0;
  return (int)((long)d10 * 3 / hours);
}

/* Fill `out` with what the numbers support, most important first, and return
 * how many were written.
 *
 * `pressPct` is the percentile from above, or -1 when unknown. `hourLocal` is
 * the local hour, used only to decide whether a cold-night warning is worth
 * saying yet - telling someone at 09:00 that tonight will be cold is noise.
 *
 * Every branch here is a statement about what the instruments measured. None
 * of them is a forecast of temperature, because this board has no idea what
 * the outside temperature is, and none of them diagnoses a person. */
inline int analyse(const Windows &w, int temp10, int rh, int hourLocal,
                   int pressPct, int tempPct, int humPct, Finding *out,
                   int cap) {
  int n = 0;
  auto add = [&](PatternId id, int sev, const char *title, const char *detail) {
    if (n < cap) {
      out[n].id = id;
      out[n].severity = sev;
      out[n].title = title;
      out[n].detail = detail;
      n++;
    }
  };

  /* ── pressure: shape first, because shape outranks magnitude ──────────── */
  const int r3 = w.okP3 ? per3h(w.dP10_3h, 3) : 0;
  const int r1 = w.okP1 ? per3h(w.dP10_1h, 1) : 0;
  const int r6 = w.okP6 ? per3h(w.dP10_6h, 6) : 0;

  if (w.okP3 && r3 <= -60) {
    add(PAT_STORM_IMMINENT, 2, "Резкий обвал давления",
        "падение больше 6 гПа за 3 часа: фронт уже здесь");
  }

  /* A reversal is the most informative thing two windows can say, so it is
   * tested before the plain directions - otherwise "still falling over 6 h"
   * would fire and hide the fact that the last hour already turned. */
  if (w.okP6 && w.okP1 && r6 <= -16 && r1 >= 10) {
    add(PAT_FRONT_PASSED, 1, "Ложбина прошла",
        "давление падало и теперь растет: погода начнет улучшаться");
  } else if (w.okP3 && w.okP1 && r3 <= -16) {
    /* Both still falling: is it getting worse or letting up? The one-hour
     * rate against the three-hour rate answers it, and the answer is the
     * difference between "leave now" and "it is nearly over". */
    if (r1 <= r3 - 10)
      add(PAT_FALL_ACCELERATING, 2, "Падение ускоряется",
          "последний час круче предыдущих: непогода приближается");
    else if (r1 >= r3 + 10)
      add(PAT_FALL_EASING, 0, "Падение замедляется",
          "худшее, похоже, уже позади");
  }

  if (w.okP12 && w.dP10_12h <= -80) {
    add(PAT_DEEPENING, 1, "Циклон углубляется",
        "больше 8 гПа за 12 часов: устойчивая непогода, не шквал");
  }
  if (w.okP12 && w.dP10_12h >= 80) {
    add(PAT_RIDGE_BUILDING, 0, "Антициклон строится",
        "больше 8 гПа за 12 часов вверх: устойчивая ясная погода");
    /* Clear skies radiate heat away, so the night under a building ridge runs
     * colder than the day suggests. This is about the SKY, inferred from
     * pressure - not from the room thermometer, which knows only the
     * radiator. Said in the evening, when it is still actionable. */
    if (hourLocal >= 16 && hourLocal <= 23)
      add(PAT_RADIATIVE_NIGHT, 1, "Ночь будет холодной",
          "ясное небо выпускает тепло, под утро заметно холоднее");
  }

  /* The rate people report noticing. Graded rather than binary, and only on
   * the falling side: the association in the literature is with drops, and
   * firing on rises too would double the noise for none of the signal. */
  if (w.okP3 && r3 <= -36) {
    add(PAT_HEADACHE_WATCH, 1, "Перепад давления",
        "на таких перепадах метеочувствительные обычно жалуются");
  }

  /* Absolute level, answered by the room's own record rather than by a
   * textbook number this board has no altitude to justify. */
  if (pressPct >= 0) {
    if (pressPct <= 5)
      add(PAT_PRESSURE_LOW, 1, "Давление на минимуме",
          "ниже, чем 95% всех показаний за время наблюдений");
    else if (pressPct >= 95)
      add(PAT_PRESSURE_HIGH, 0, "Давление на максимуме",
          "выше, чем 95% всех показаний за время наблюдений");
  }

  /* ── the room ─────────────────────────────────────────────────────────── */
  const int td10 = dewPoint10(temp10, rh);
  if (td10 != -9999 && temp10 != -32768) {
    const int coldSurface10 = temp10 - kColdSurfaceDelta10;
    if (td10 >= coldSurface10)
      add(PAT_CONDENSATION, 2, "Риск конденсата",
          "точка росы выше холодной стены: на ней выступит влага");
    else if (td10 >= coldSurface10 - 15)
      add(PAT_MOULD_WATCH, 1, "Влажно у холодных стен",
          "точка росы близко к стене, стоит проветрить");
  }

  /* ── water, as opposed to the ratio ────────────────────────────────────
   * Everything below compares absolute humidity now against absolute
   * humidity an hour ago, reconstructed from the same reading minus the
   * window's own deltas. That reconstruction is exact: dT and dH came from
   * the archive rows either side of the window, so T-dT and RH-dH ARE the
   * older row. No second card read, and no approximation.
   *
   * This is what separates "the air got dry" from "the heating came on".
   * They look identical in RH and are opposites in water. */
  const int ah10 = absHumidity10(temp10, rh);
  int dAh10 = 0;
  bool ahOk = false;
  if (ah10 != -9999 && w.okT1 && w.okH1) {
    int wasAh10 = absHumidity10(temp10 - w.dT10_1h, rh - w.dH_1h);
    if (wasAh10 != -9999) {
      dAh10 = ah10 - wasAh10;
      ahOk = true;
    }
  }

  if (ahOk) {
    /* 0.8 g/m3 in an hour is well clear of the sensor's own noise and of
     * the drift a closed, occupied room produces on its own. */
    if (dAh10 <= -8)
      add(PAT_AIRED_OUT, 0, "Комнату проветрили",
          "воды в воздухе резко убавилось: был обмен с улицей");
    else if (dAh10 >= 8)
      add(PAT_WATER_ENTERING, 1, "Влага прибывает",
          "воды в воздухе больше: готовка, сушка белья или люди");
    /* RH down, water flat: the capacity grew, nothing dried. Worth saying
     * out loud, because this is the reading that sends people out to buy a
     * humidifier in January when the answer is the radiator.
     *
     * "Flat" is 0.6 g/m3, and that number is the SENSOR'S, not a taste. The
     * WSDCGQ11LM is quoted at about +-3 % RH and +-0.3 C, which at room
     * conditions works out to roughly +-0.6 g/m3 of uncertainty in absolute
     * humidity. Demanding tighter agreement than the instrument can deliver
     * would make this pattern fire only by luck - which it did: at +1.5 C
     * and -6 points the arithmetic gives -0.45 g/m3, real heating that a
     * +-0.4 g/m3 window called water loss. Still well clear of the 0.8 that
     * marks water actually moving, so the three stay disjoint. */
    if (w.dH_1h <= -4 && dAh10 > -6 && dAh10 < 6 && w.okT1 && w.dT10_1h >= 5)
      add(PAT_DRY_IS_JUST_HEAT, 0, "Это не сушь, а нагрев",
          "влажность упала от прогрева, воды в воздухе столько же");
  }

  if (rh >= 0 && rh < 30)
    add(PAT_AIR_TOO_DRY, 1, "Воздух пересушен",
        "ниже 30% сохнут слизистые, дерево и книги");
  /* Above 60 % house dust mites breed freely; this is the number allergy
   * guidance is quoted against, and it sits well below the ~70 % where mould
   * starts on surfaces. The sensor is in the middle of the room, so this is
   * the air people breathe - which is exactly what the band is about. */
  else if (rh > 60)
    add(PAT_MITE_ZONE, 1, "Сыро для пылевых клещей",
        "выше 60% они размножаются свободно");

  /* Two temperature bands with a published basis rather than a taste.
   * 18 C is the WHO minimum for a healthy living room; above about 24 C at
   * night sleep measurably degrades. Both are about the ROOM AIR, which is
   * precisely what a sensor in the middle of the room measures. */
  if (temp10 != -32768) {
    if (temp10 < 180)
      add(PAT_COLD_FOR_HEALTH, 1, "Холодно для жилой комнаты",
          "ниже 18 C - это порог, который ВОЗ считает нижним");
    else if (temp10 > 240 && hourLocal >= 0 &&
             (hourLocal >= 22 || hourLocal <= 6))
      add(PAT_WARM_FOR_SLEEP, 0, "Тепло для сна",
          "выше 24 C ночью сон заметно хуже, лучше проветрить");
  }

  /* Against the room's own record. Same reasoning as the barometer: a
   * threshold picked once is a guess about this flat, while its own
   * distribution is a measurement of it. */
  if (tempPct >= 0) {
    if (tempPct <= 5)
      add(PAT_TEMP_UNUSUAL, 1, "Холоднее обычного",
          "ниже, чем 95% всех показаний за время наблюдений");
    else if (tempPct >= 95)
      add(PAT_TEMP_UNUSUAL, 1, "Теплее обычного",
          "выше, чем 95% всех показаний за время наблюдений");
  }
  if (humPct >= 0) {
    if (humPct <= 5)
      add(PAT_HUMIDITY_UNUSUAL, 0, "Суше обычного",
          "ниже, чем 95% всех показаний за время наблюдений");
    else if (humPct >= 95)
      add(PAT_HUMIDITY_UNUSUAL, 1, "Влажнее обычного",
          "выше, чем 95% всех показаний за время наблюдений");
  }

  /* Ventilation versus weather. A room cooling fast WHILE the pressure sits
   * still is a window, not a cold front: the atmosphere did not move, so
   * whatever changed is inside the flat. This is the one place indoor
   * temperature is allowed to mean something, and it means it only because
   * pressure vouches that the weather is not responsible.
   *
   * The room-centre sensor lags a real window by minutes and damps it, so
   * the threshold is deliberately generous - this is the air mass turning
   * over, not a draught crossing a probe. */
  if (w.okT1 && w.okP1) {
    bool pressureQuiet = r1 > -8 && r1 < 8;
    if (pressureQuiet && w.dT10_1h <= -20)
      add(PAT_VENTILATION, 1, "Похоже, открыто окно",
          "комната быстро остывает, а давление стоит на месте");
    else if (pressureQuiet && w.dT10_1h >= 20)
      add(PAT_HEATING_UP, 0, "Комната прогревается",
          "быстрый рост температуры при ровном давлении");
  }
  return n;
}

} // namespace analysis

#endif
