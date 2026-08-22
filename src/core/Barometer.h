/*
 * Nocturne C6 — barometric tendency: what a falling needle actually means.
 *
 * The room sensor reports three things, and only ONE of them is about the
 * weather. A building is not airtight, so indoor pressure tracks the
 * atmosphere within a fraction of a hPa — a barometer indoors forecasts, while
 * an indoor thermometer only ever describes the radiator and an indoor
 * hygrometer describes the shower. Every hint below is derived from pressure
 * alone; nothing here is inferred from room temperature, on purpose.
 *
 * The window is THREE HOURS because that is the meteorological standard — the
 * tendency METAR reports, and the interval every published threshold is quoted
 * against. Using a different window and keeping the same numbers would be
 * borrowing the authority of a scale without its units.
 *
 * Scale (hPa per 3 h), after the WMO tendency bands:
 *
 *      |Δ| < 0.5   steady        — the pressure field is not moving
 *      0.5 .. 1.5  slow
 *      1.6 .. 3.5  moderate
 *      3.6 .. 6.0  rapid
 *          > 6.0   very rapid    — a front is arriving now
 *
 * Falling means a low approaching: cloud, wind, precipitation, milder nights.
 * Rising means a high building: clearing, calmer, and — because clear skies
 * radiate heat away — colder nights, sharply so in winter.
 *
 * On headaches. The association between falling barometric pressure and
 * migraine onset is real and documented, and the effect people report is tied
 * to the RATE of the drop rather than the absolute value. This code says what
 * the pressure did and notes that it is the change weather-sensitive people
 * notice. It does not diagnose anyone, and it must not start.
 */
#ifndef NOCT_BAROMETER_H
#define NOCT_BAROMETER_H

#include <Arduino.h>

namespace barometer {

/* What the needle is doing, coarse enough to act on. */
enum Tendency {
  TEND_UNKNOWN = 0, /* not enough history — different from "steady" */
  TEND_FALL_FAST,   /* < -3.6 hPa/3h */
  TEND_FALL,        /* -3.5 .. -1.6 */
  TEND_FALL_SLOW,   /* -1.5 .. -0.5 */
  TEND_STEADY,      /* |Δ| < 0.5 */
  TEND_RISE_SLOW,
  TEND_RISE,
  TEND_RISE_FAST,
};

/* `dPress10` is tenths of a hPa over `hours`, normalised here to the 3-hour
 * standard so the thresholds keep meaning what they are quoted to mean. */
inline Tendency classify(int dPress10, int hours) {
  if (hours < 1) return TEND_UNKNOWN;
  long per3h = (long)dPress10 * 3 / hours; /* tenths of hPa per 3 h */
  if (per3h <= -36) return TEND_FALL_FAST;
  if (per3h <= -16) return TEND_FALL;
  if (per3h <= -5) return TEND_FALL_SLOW;
  if (per3h < 5) return TEND_STEADY;
  if (per3h < 16) return TEND_RISE_SLOW;
  if (per3h < 36) return TEND_RISE;
  return TEND_RISE_FAST;
}

/* One short line for the screen — what to expect, not what happened. A reader
 * glancing at a tile wants the consequence; the number beside it already
 * carries the cause. */
inline const char *forecast(Tendency t) {
  /* Sentences a person would say, not telegraph. The first version read
   * "падает - возможен дождь": the number beside it already says the pressure
   * is falling, so repeating it spent half the line restating the cause and
   * left a stub for the part that matters. And a bare hyphen between two
   * fragments reads as a rendering fault, especially since the font has no em
   * dash to make it look deliberate. */
  switch (t) {
  case TEND_FALL_FAST: return "Идет непогода";
  case TEND_FALL:      return "Похоже, будет дождь";
  case TEND_FALL_SLOW: return "Небо затягивает";
  case TEND_STEADY:    return "Погода без перемен";
  case TEND_RISE_SLOW: return "Проясняется";
  case TEND_RISE:      return "Будет ясно";
  case TEND_RISE_FAST: return "Прояснится и похолодает";
  default:             return "";
  }
}

/* The same call, short enough for a status row that already carries the
 * battery, the pressure and the age. The long form stays for the toast and the
 * wolf, where there is a whole line to spend. */
inline const char *forecastShort(Tendency t) {
  switch (t) {
  case TEND_FALL_FAST: return "непогода";
  case TEND_FALL:      return "будет дождь";
  case TEND_FALL_SLOW: return "затягивает";
  case TEND_STEADY:    return "без перемен";
  case TEND_RISE_SLOW: return "проясняется";
  case TEND_RISE:      return "будет ясно";
  case TEND_RISE_FAST: return "ясно, холоднее";
  default:             return "";
  }
}

/* Direction as a number the caller can draw with: -1 falling, +1 rising,
 * 0 steady. The u8g2 cyrillic subset has no arrow glyphs and the ASCII stand-in
 * ("vv") read as a placeholder, so the arrow is drawn as a triangle the way
 * trendArrow() does it — same shape the hardware screens already use. */
inline int direction(Tendency t) {
  switch (t) {
  case TEND_FALL_FAST:
  case TEND_FALL:
  case TEND_FALL_SLOW: return -1;
  case TEND_RISE_SLOW:
  case TEND_RISE:
  case TEND_RISE_FAST: return 1;
  default:             return 0;
  }
}

/* True for the two extreme bands, where a doubled marker is worth the pixels. */
inline bool isSharp(Tendency t) {
  return t == TEND_FALL_FAST || t == TEND_RISE_FAST;
}

/* True while the drop is steep enough to be the kind weather-sensitive people
 * report noticing. Deliberately only on the FALLING side: the association in
 * the literature is with drops, and firing on a rise as well would double the
 * noise for none of the signal. */
inline bool headacheWatch(Tendency t) {
  return t == TEND_FALL_FAST;
}

} // namespace barometer

#endif
