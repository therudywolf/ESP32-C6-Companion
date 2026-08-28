/*
 * Host tests for the carousel's round builder.
 *
 * The properties worth pinning are the ones the owner asked for in words —
 * "неважные каждый 5й круг, важные по 2 раза за круг" — because each of them
 * is a claim about a SEQUENCE, and a sequence bug looks like nothing at all
 * on the bench: the board rotates, screens appear, and only after ten minutes
 * of watching does it turn out that ПОГОДА never comes round or that ОБЗОР's
 * two turns are back to back.
 *
 * So: every rule gets a round it must hold in, and the twice-per-round rule
 * additionally gets a SPACING assertion, which is the half of it that a naive
 * "insert it twice" implementation silently fails.
 */
#include <unity.h>

#include "ui/Carousel.h"

using namespace carousel;

void setUp() {}
void tearDown() {}

static const uint32_t ALL = 0xFFFFFFFFu;

static int countOf(const uint8_t *r, int n, int scene) {
  int k = 0;
  for (int i = 0; i < n; i++)
    if (r[i] == scene) k++;
  return k;
}

static int firstOf(const uint8_t *r, int n, int scene) {
  for (int i = 0; i < n; i++)
    if (r[i] == scene) return i;
  return -1;
}

static void test_every_means_every_round() {
  uint8_t r[ROUND_MAX];
  const uint8_t *f = PRESETS[1].freq; /* «за компом» */
  for (int round = 0; round < 12; round++) {
    int n = buildRound(f, ALL, round, nullptr, nullptr, r);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, countOf(r, n, SCENE_CPU),
                                  "CPU должен быть в каждом круге ровно раз");
  }
}

static void test_twice_appears_twice_and_is_spread() {
  uint8_t r[ROUND_MAX];
  const uint8_t *f = PRESETS[1].freq; /* ОБЗОР = 2 раза */
  int n = buildRound(f, ALL, 0, nullptr, nullptr, r);
  TEST_ASSERT_EQUAL_INT(2, countOf(r, n, SCENE_DASH));
  /* The two turns must not be adjacent: back to back reads as the carousel
   * being stuck rather than as the screen being important. */
  int a = -1, b = -1;
  for (int i = 0; i < n; i++)
    if (r[i] == SCENE_DASH) {
      if (a < 0) a = i;
      else b = i;
    }
  TEST_ASSERT_TRUE_MESSAGE(b - a > 2, "два показа ОБЗОРА встали подряд");
}

static void test_fifth_appears_once_in_five() {
  uint8_t r[ROUND_MAX];
  const uint8_t *f = PRESETS[1].freq; /* ПОГОДА = каждый 5-й */
  int seen = 0;
  for (int round = 0; round < 15; round++) {
    int n = buildRound(f, ALL, round, nullptr, nullptr, r);
    int k = countOf(r, n, SCENE_WEATHER);
    if (round % 5 == 0) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(1, k, "ПОГОДА пропущена в свой круг");
      seen++;
    } else {
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, k, "ПОГОДА пришла не в свой круг");
    }
  }
  TEST_ASSERT_EQUAL_INT(3, seen);
}

static void test_off_never_appears() {
  uint8_t r[ROUND_MAX];
  const uint8_t *f = PRESETS[1].freq;
  for (int round = 0; round < 15; round++) {
    int n = buildRound(f, ALL, round, nullptr, nullptr, r);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, countOf(r, n, SCENE_FORZA),
                                  "FORZA не должна крутиться сама");
  }
}

/* The mask is the owner's own on/off list from the menu. A scene switched off
 * there must not come back because a preset thinks it is important. */
static void test_mask_wins_over_frequency() {
  uint8_t r[ROUND_MAX];
  const uint8_t *f = PRESETS[1].freq;
  uint32_t mask = ALL & ~(1u << SCENE_DASH);
  int n = buildRound(f, mask, 0, nullptr, nullptr, r);
  TEST_ASSERT_EQUAL_INT(0, countOf(r, n, SCENE_DASH));
  TEST_ASSERT_TRUE(n > 0);
}

static bool skipPcScenes(int scene, void *) { return sceneNeedsPc(scene); }

static void test_skip_removes_and_round_survives() {
  uint8_t r[ROUND_MAX];
  const uint8_t *f = PRESETS[1].freq;
  int n = buildRound(f, ALL, 0, skipPcScenes, nullptr, r);
  TEST_ASSERT_TRUE_MESSAGE(n > 0, "с тёмным ПК круг не должен пустеть");
  TEST_ASSERT_EQUAL_INT(0, countOf(r, n, SCENE_CPU));
  TEST_ASSERT_TRUE(countOf(r, n, SCENE_HOME) > 0);
}

/* Every scene the ring can ever show has to be IN the order table, or it is
 * unreachable by the carousel however it is configured. Caught here because
 * the failure mode is a screen that simply never comes up — which reads as
 * the screen being broken. */
static void test_order_covers_every_scene_but_forza() {
  for (int sc = 0; sc < SCENE_FORZA; sc++) {
    bool found = false;
    for (int i = 0; i < ORDER_N; i++)
      if (ORDER[i] == sc) found = true;
    TEST_ASSERT_TRUE_MESSAGE(found, "сцены нет в порядке карусели");
  }
  TEST_ASSERT_EQUAL_INT(SCENE_FORZA, ORDER_N);
}

static void test_no_scene_listed_twice_in_order() {
  for (int i = 0; i < ORDER_N; i++)
    for (int j = i + 1; j < ORDER_N; j++)
      TEST_ASSERT_TRUE_MESSAGE(ORDER[i] != ORDER[j],
                               "сцена дважды в порядке карусели");
}

/* An empty round must be reported as empty rather than as a round of one
 * arbitrary scene: the caller falls back to ЛОГОВО, and it can only do that
 * if it is told. */
static void test_everything_off_gives_empty_round() {
  uint8_t r[ROUND_MAX];
  uint8_t f[SCENE_COUNT];
  for (int i = 0; i < SCENE_COUNT; i++) f[i] = FQ_OFF;
  TEST_ASSERT_EQUAL_INT(0, buildRound(f, ALL, 0, nullptr, nullptr, r));
}

/* Never write past the array. A table of all-TWICE is the worst case and the
 * one an over-enthusiastic web panel can actually produce. */
static void test_all_twice_fits() {
  uint8_t r[ROUND_MAX + 8];
  uint8_t f[SCENE_COUNT];
  for (int i = 0; i < SCENE_COUNT; i++) f[i] = FQ_TWICE;
  for (int i = 0; i < ROUND_MAX + 8; i++) r[i] = 0xEE;
  int n = buildRound(f, ALL, 0, nullptr, nullptr, r);
  TEST_ASSERT_TRUE(n <= ROUND_MAX);
  for (int i = ROUND_MAX; i < ROUND_MAX + 8; i++)
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xEE, r[i], "запись за границу массива");
}

/* Presets are a promise about what a mode does. «тихо» claiming to be quiet
 * while listing eight screens would be the kind of thing nobody checks. */
static void test_presets_are_what_they_say() {
  uint8_t r[ROUND_MAX];
  int quiet = buildRound(PRESETS[4].freq, ALL, 0, nullptr, nullptr, r);
  int all = buildRound(PRESETS[5].freq, ALL, 0, nullptr, nullptr, r);
  TEST_ASSERT_TRUE_MESSAGE(quiet <= 4, "«тихо» показывает слишком много");
  TEST_ASSERT_EQUAL_INT_MESSAGE(ORDER_N - 1, all,
                                "«всё поровну» должно показать всё, кроме FORZA");
  for (int p = 0; p < PRESET_N; p++) {
    int n = buildRound(PRESETS[p].freq, ALL, 0, nullptr, nullptr, r);
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "режим, который ничего не показывает");
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_means_every_round);
  RUN_TEST(test_twice_appears_twice_and_is_spread);
  RUN_TEST(test_fifth_appears_once_in_five);
  RUN_TEST(test_off_never_appears);
  RUN_TEST(test_mask_wins_over_frequency);
  RUN_TEST(test_skip_removes_and_round_survives);
  RUN_TEST(test_order_covers_every_scene_but_forza);
  RUN_TEST(test_no_scene_listed_twice_in_order);
  RUN_TEST(test_everything_off_gives_empty_round);
  RUN_TEST(test_all_twice_fits);
  RUN_TEST(test_presets_are_what_they_say);
  return UNITY_END();
}
