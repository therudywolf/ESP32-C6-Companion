/*
 * Host tests for the climate pattern matcher.
 *
 * These are the cheapest tests in the project to write and the most expensive
 * behaviour to check any other way: every branch here is a claim about the
 * weather, and the weather does not arrive on demand. The board has a `baro`
 * injection command precisely because waiting for a real front to test a
 * threshold is not a plan — but injection still needs a flash cycle and a
 * screenshot, while these run in a second.
 *
 * What they mostly guard is the SHAPE logic: the reason this module exists at
 * all is that several windows can disagree, and the ways they disagree are
 * exactly the cases nobody thinks to check by hand.
 */
#include <unity.h>

#include "core/ClimateAnalysis.h"

void setUp() {}
void tearDown() {}

/* Somewhere to put findings without repeating the boilerplate. */
static analysis::Finding gF[8];

static bool has(int n, analysis::PatternId id) {
  for (int i = 0; i < n; i++)
    if (gF[i].id == id) return true;
  return false;
}

/* ── the dew point ──────────────────────────────────────────────────────── */

static void test_dew_point_known_value() {
  /* 20.0 C at 50 % RH is 9.3 C by the Magnus approximation. This is the whole
   * reason the module bothers: 50 % says nothing on its own about whether a
   * wall will sweat, and 9.3 C says everything. */
  int td = analysis::dewPoint10(200, 50);
  TEST_ASSERT_INT_WITHIN(3, 93, td);
}

static void test_dew_point_saturated_equals_air() {
  /* At 100 % the dew point IS the air temperature, by definition. */
  int td = analysis::dewPoint10(180, 100);
  TEST_ASSERT_INT_WITHIN(3, 180, td);
}

static void test_dew_point_rejects_impossible_inputs() {
  TEST_ASSERT_EQUAL_INT(-9999, analysis::dewPoint10(-32768, 50));
  TEST_ASSERT_EQUAL_INT(-9999, analysis::dewPoint10(200, 0));
  TEST_ASSERT_EQUAL_INT(-9999, analysis::dewPoint10(200, 101));
}

/* ── window normalisation ───────────────────────────────────────────────── */

static void test_per3h_normalises_to_the_wmo_window() {
  /* The WMO bands are quoted per three hours. A 12-hour change must be scaled
   * before those thresholds mean what they are quoted to mean; keeping the
   * numbers and changing the window would borrow a scale's authority without
   * its units. */
  TEST_ASSERT_EQUAL_INT(-10, analysis::per3h(-40, 12));
  TEST_ASSERT_EQUAL_INT(-60, analysis::per3h(-20, 1));
  TEST_ASSERT_EQUAL_INT(0, analysis::per3h(-40, 0)); /* no window, no rate */
}

/* ── shape: the point of the module ─────────────────────────────────────── */

static void test_trough_passed_beats_still_falling() {
  /* Six hours down, last hour up. This is a trough that has gone by, and the
   * regression it guards is the obvious one: report "still falling" from the
   * long window and hide the fact that the short window already turned. */
  analysis::Windows w;
  w.okP6 = w.okP3 = w.okP1 = true;
  w.dP10_6h = -60; /* -30 per 3 h */
  w.dP10_3h = -30;
  w.dP10_1h = +8; /* +24 per 3 h: rising now */
  int n = analysis::analyse(w, 220, 45, 12, -1, -1, -1, gF, 8);
  TEST_ASSERT_TRUE(has(n, analysis::PAT_FRONT_PASSED));
  TEST_ASSERT_FALSE(has(n, analysis::PAT_FALL_ACCELERATING));
  TEST_ASSERT_FALSE(has(n, analysis::PAT_FALL_EASING));
}

static void test_accelerating_and_easing_are_opposites() {
  analysis::Windows w;
  w.okP3 = w.okP1 = true;
  w.dP10_3h = -30; /* -30 per 3 h */
  w.dP10_1h = -20; /* -60 per 3 h: the last hour is far steeper */
  int n = analysis::analyse(w, 220, 45, 12, -1, -1, -1, gF, 8);
  TEST_ASSERT_TRUE(has(n, analysis::PAT_FALL_ACCELERATING));

  w.dP10_1h = -2; /* -6 per 3 h: barely moving now */
  n = analysis::analyse(w, 220, 45, 12, -1, -1, -1, gF, 8);
  TEST_ASSERT_TRUE(has(n, analysis::PAT_FALL_EASING));
  TEST_ASSERT_FALSE(has(n, analysis::PAT_FALL_ACCELERATING));
}

static void test_storm_needs_the_wmo_very_rapid_band() {
  analysis::Windows w;
  w.okP3 = true;
  w.dP10_3h = -55; /* 5.5 hPa: rapid, but not the top band */
  TEST_ASSERT_FALSE(
      has(analysis::analyse(w, 220, 45, 12, -1, -1, -1, gF, 8),
          analysis::PAT_STORM_IMMINENT));
  w.dP10_3h = -70; /* over 6 hPa/3h */
  TEST_ASSERT_TRUE(
      has(analysis::analyse(w, 220, 45, 12, -1, -1, -1, gF, 8),
          analysis::PAT_STORM_IMMINENT));
}

static void test_unset_windows_produce_nothing() {
  /* Every ok flag false. A module that reads 0 as "no change" would report a
   * calm, settled atmosphere from an empty card — confidently, and wrongly. */
  analysis::Windows w;
  int n = analysis::analyse(w, -32768, -1, 12, -1, -1, -1, gF, 8);
  TEST_ASSERT_EQUAL_INT(0, n);
}

/* ── the room, not the sky ──────────────────────────────────────────────── */

static void test_ventilation_needs_pressure_to_vouch_for_it() {
  /* A room cooling fast while the pressure sits still is a window. The same
   * cooling WITH the pressure moving is weather, and must not be reported as
   * an open window. Indoor temperature is only allowed to mean something
   * because pressure vouches that the atmosphere did not move. */
  analysis::Windows w;
  w.okT1 = w.okP1 = true;
  w.dT10_1h = -30; /* 3 degrees in an hour */
  w.dP10_1h = 0;   /* pressure flat */
  TEST_ASSERT_TRUE(has(analysis::analyse(w, 200, 45, 12, -1, -1, -1, gF, 8),
                       analysis::PAT_VENTILATION));

  w.dP10_1h = -20; /* the atmosphere IS moving: not a window */
  TEST_ASSERT_FALSE(has(analysis::analyse(w, 200, 45, 12, -1, -1, -1, gF, 8),
                        analysis::PAT_VENTILATION));
}

static void test_condensation_fires_on_the_dew_point_not_the_humidity() {
  /* The case the plain "over 65 %" threshold gets wrong in both directions.
   * 60 % at 24 C is a dew point of ~15.8 C against a 20 C wall: dry enough.
   * 60 % at 12 C is ~4.6 C against an 8 C wall: also fine. Push the humidity
   * up at the low temperature and the wall sweats while RH is still under
   * any fixed threshold anyone would have set. */
  analysis::Windows w;
  TEST_ASSERT_FALSE(has(analysis::analyse(w, 240, 60, 12, -1, -1, -1, gF, 8),
                        analysis::PAT_CONDENSATION));
  int n = analysis::analyse(w, 120, 85, 12, -1, -1, -1, gF, 8);
  TEST_ASSERT_TRUE(has(n, analysis::PAT_CONDENSATION));
}

static void test_cold_night_is_only_said_while_it_is_still_actionable() {
  analysis::Windows w;
  w.okP12 = true;
  w.dP10_12h = 100; /* a ridge building */
  /* At nine in the morning, telling someone tonight will be cold is noise. */
  TEST_ASSERT_FALSE(has(analysis::analyse(w, 220, 45, 9, -1, -1, -1, gF, 8),
                        analysis::PAT_RADIATIVE_NIGHT));
  TEST_ASSERT_TRUE(has(analysis::analyse(w, 220, 45, 19, -1, -1, -1, gF, 8),
                       analysis::PAT_RADIATIVE_NIGHT));
}

static void test_percentile_extremes_only() {
  analysis::Windows w;
  TEST_ASSERT_TRUE(has(analysis::analyse(w, 220, 45, 12, 2, -1, -1, gF, 8),
                       analysis::PAT_PRESSURE_LOW));
  TEST_ASSERT_TRUE(has(analysis::analyse(w, 220, 45, 12, 98, -1, -1, gF, 8),
                       analysis::PAT_PRESSURE_HIGH));
  /* Mid-range says nothing, and -1 (archive too short) says nothing either. */
  int n = analysis::analyse(w, 220, 45, 12, 50, -1, -1, gF, 8);
  TEST_ASSERT_FALSE(has(n, analysis::PAT_PRESSURE_LOW));
  TEST_ASSERT_FALSE(has(n, analysis::PAT_PRESSURE_HIGH));
  n = analysis::analyse(w, 220, 45, 12, -1, -1, -1, gF, 8);
  TEST_ASSERT_FALSE(has(n, analysis::PAT_PRESSURE_LOW));
  TEST_ASSERT_FALSE(has(n, analysis::PAT_PRESSURE_HIGH));
}

static void test_output_is_capped_not_overflowed() {
  /* Every pressure pattern at once, into a buffer of two. */
  analysis::Windows w;
  w.okP1 = w.okP3 = w.okP6 = w.okP12 = true;
  w.dP10_3h = -80;
  w.dP10_1h = -40;
  w.dP10_6h = -120;
  w.dP10_12h = -200;
  analysis::Finding small[2];
  int n = analysis::analyse(w, 120, 90, 20, 1, 50, 50, small, 2);
  TEST_ASSERT_EQUAL_INT(2, n);
}


/* ── the room ───────────────────────────────────────────────────────────── */

static void test_absolute_humidity_known_value() {
  /* 20.0 C at 50 % RH holds about 8.6 g of water per cubic metre. This is
   * the anchor: get the Magnus chain wrong and every conclusion built on
   * "water moved" is wrong with it. */
  TEST_ASSERT_INT_WITHIN(3, 86, analysis::absHumidity10(200, 50));
  /* Same water, ten degrees warmer, is a much lower RH - which is the whole
   * point of having this function. 30 C at 27 % is close to the same 8.6. */
  TEST_ASSERT_INT_WITHIN(6, 86, analysis::absHumidity10(300, 28));
  TEST_ASSERT_EQUAL_INT(-9999, analysis::absHumidity10(-32768, 50));
  TEST_ASSERT_EQUAL_INT(-9999, analysis::absHumidity10(200, 0));
}

static void test_heating_does_not_dry_the_air() {
  /* The reading that sends people out to buy a humidifier in January. The
   * room warmed 1.5 C and RH fell 6 points; the water did not move. The
   * analyser has to say so rather than call it dryness. */
  analysis::Windows w;
  w.okT1 = w.okH1 = w.okP1 = true;
  w.dT10_1h = 15;
  w.dH_1h = -6;
  int n = analysis::analyse(w, 230, 40, 12, -1, -1, -1, gF, 8);
  TEST_ASSERT_TRUE(has(n, analysis::PAT_DRY_IS_JUST_HEAT));
  /* and it must NOT claim water arrived or left */
  TEST_ASSERT_FALSE(has(n, analysis::PAT_WATER_ENTERING));
  TEST_ASSERT_FALSE(has(n, analysis::PAT_AIRED_OUT));

  /* The same RH fall with the room NOT warming is real drying, and must not
   * be excused as heat. This is the pair that makes the pattern mean
   * something rather than just matching any fall in humidity. */
  analysis::Windows c;
  c.okT1 = c.okH1 = c.okP1 = true;
  c.dT10_1h = 0;
  c.dH_1h = -6;
  n = analysis::analyse(c, 230, 40, 12, -1, -1, -1, gF, 8);
  TEST_ASSERT_FALSE(has(n, analysis::PAT_DRY_IS_JUST_HEAT));
}

static void test_water_entering_and_airing_out_are_opposites() {
  /* Water arriving: RH up hard at a steady temperature. */
  analysis::Windows a;
  a.okT1 = a.okH1 = a.okP1 = true;
  a.dT10_1h = 0;
  a.dH_1h = 12;
  int n = analysis::analyse(a, 220, 60, 12, -1, -1, -1, gF, 8);
  TEST_ASSERT_TRUE(has(n, analysis::PAT_WATER_ENTERING));
  TEST_ASSERT_FALSE(has(n, analysis::PAT_AIRED_OUT));

  /* Water leaving: the same room aired out. */
  analysis::Windows b;
  b.okT1 = b.okH1 = b.okP1 = true;
  b.dT10_1h = 0;
  b.dH_1h = -12;
  n = analysis::analyse(b, 220, 40, 12, -1, -1, -1, gF, 8);
  TEST_ASSERT_TRUE(has(n, analysis::PAT_AIRED_OUT));
  TEST_ASSERT_FALSE(has(n, analysis::PAT_WATER_ENTERING));
}

static void test_health_bands_use_published_numbers() {
  analysis::Windows w;
  /* 17.9 C is under the WHO living-room minimum; 18.1 is not. A band that
   * fires on both sides of its own threshold is not a band. */
  int n = analysis::analyse(w, 179, 45, 12, -1, -1, -1, gF, 8);
  TEST_ASSERT_TRUE(has(n, analysis::PAT_COLD_FOR_HEALTH));
  n = analysis::analyse(w, 181, 45, 12, -1, -1, -1, gF, 8);
  TEST_ASSERT_FALSE(has(n, analysis::PAT_COLD_FOR_HEALTH));

  /* Above 60 % dust mites breed; 60 exactly is still inside the band. */
  n = analysis::analyse(w, 220, 61, 12, -1, -1, -1, gF, 8);
  TEST_ASSERT_TRUE(has(n, analysis::PAT_MITE_ZONE));
  n = analysis::analyse(w, 220, 60, 12, -1, -1, -1, gF, 8);
  TEST_ASSERT_FALSE(has(n, analysis::PAT_MITE_ZONE));
  /* Dry and mite-ridden are mutually exclusive by construction. */
  n = analysis::analyse(w, 220, 25, 12, -1, -1, -1, gF, 8);
  TEST_ASSERT_TRUE(has(n, analysis::PAT_AIR_TOO_DRY));
  TEST_ASSERT_FALSE(has(n, analysis::PAT_MITE_ZONE));
}

static void test_warm_for_sleep_only_at_night() {
  /* 25 C is 25 C at noon and at midnight; only one of them is worth saying,
   * because only one of them is about sleep. */
  analysis::Windows w;
  int n = analysis::analyse(w, 250, 45, 23, -1, -1, -1, gF, 8);
  TEST_ASSERT_TRUE(has(n, analysis::PAT_WARM_FOR_SLEEP));
  n = analysis::analyse(w, 250, 45, 13, -1, -1, -1, gF, 8);
  TEST_ASSERT_FALSE(has(n, analysis::PAT_WARM_FOR_SLEEP));
  /* An unknown hour must not be treated as midnight. */
  n = analysis::analyse(w, 250, 45, -1, -1, -1, -1, gF, 8);
  TEST_ASSERT_FALSE(has(n, analysis::PAT_WARM_FOR_SLEEP));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_dew_point_known_value);
  RUN_TEST(test_dew_point_saturated_equals_air);
  RUN_TEST(test_dew_point_rejects_impossible_inputs);
  RUN_TEST(test_per3h_normalises_to_the_wmo_window);
  RUN_TEST(test_trough_passed_beats_still_falling);
  RUN_TEST(test_accelerating_and_easing_are_opposites);
  RUN_TEST(test_storm_needs_the_wmo_very_rapid_band);
  RUN_TEST(test_unset_windows_produce_nothing);
  RUN_TEST(test_ventilation_needs_pressure_to_vouch_for_it);
  RUN_TEST(test_condensation_fires_on_the_dew_point_not_the_humidity);
  RUN_TEST(test_cold_night_is_only_said_while_it_is_still_actionable);
  RUN_TEST(test_percentile_extremes_only);
  RUN_TEST(test_output_is_capped_not_overflowed);
  RUN_TEST(test_absolute_humidity_known_value);
  RUN_TEST(test_heating_does_not_dry_the_air);
  RUN_TEST(test_water_entering_and_airing_out_are_opposites);
  RUN_TEST(test_health_bands_use_published_numbers);
  RUN_TEST(test_warm_for_sleep_only_at_night);
  return UNITY_END();
}
