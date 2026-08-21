/*
 * Host tests for the two ring buffers behind every graph on the device.
 * They are header-only, so nothing device-specific has to be stubbed.
 * at(0) must be the OLDEST sample both before and after wrap — get that
 * backwards and the curves silently draw time in reverse.
 */
#include <unity.h>

#include "core/Graphs.h"
#include "core/HourHistory.h"

void setUp() {}
void tearDown() {}

static void test_rolling_order_before_wrap() {
  RollingGraph g;
  for (int i = 1; i <= 5; i++) g.push(i);
  TEST_ASSERT_EQUAL_INT(5, g.count);
  TEST_ASSERT_EQUAL_INT(1, g.at(0));
  TEST_ASSERT_EQUAL_INT(5, g.at(g.count - 1));
}

static void test_rolling_order_after_wrap() {
  RollingGraph g;
  for (int i = 0; i < RollingGraph::kSamples + 7; i++) g.push(i);
  TEST_ASSERT_EQUAL_INT(RollingGraph::kSamples, g.count);
  /* oldest surviving sample is (total - capacity) */
  TEST_ASSERT_EQUAL_INT(7, g.at(0));
  TEST_ASSERT_EQUAL_INT(RollingGraph::kSamples + 6, g.at(g.count - 1));
}

static void test_rolling_max_floor() {
  RollingGraph g;
  g.push(4);
  TEST_ASSERT_EQUAL_INT(100, g.maxVal(100)); /* floor wins */
  TEST_ASSERT_EQUAL_INT(4, g.maxVal(1));
}

static void test_hourgraph_default_cap() {
  HourGraph h;
  for (int i = 0; i < HourGraph::N + 3; i++) h.push(i);
  TEST_ASSERT_EQUAL_INT(HourGraph::N, h.count);
  TEST_ASSERT_EQUAL_INT(3, h.at(0));
  TEST_ASSERT_EQUAL_INT(HourGraph::N + 2, h.now());
}

static void test_hourgraph_small_cap_wraps_at_cap() {
  /* the day series reuses the same struct with 24 slots */
  HourGraph d;
  d.setCap(24);
  for (int i = 0; i < 30; i++) d.push(i);
  TEST_ASSERT_EQUAL_INT(24, d.count);
  TEST_ASSERT_EQUAL_INT(6, d.at(0));
  TEST_ASSERT_EQUAL_INT(29, d.now());
}

static void test_hourgraph_minmax() {
  HourGraph h;
  h.push(30);
  h.push(70);
  h.push(50);
  TEST_ASSERT_EQUAL_INT(30, h.minVal());
  TEST_ASSERT_EQUAL_INT(70, h.maxVal(1));
  TEST_ASSERT_EQUAL_INT(50, h.now());
}

static void test_empty_graph_is_safe() {
  HourGraph h;
  TEST_ASSERT_EQUAL_INT(0, h.count);
  TEST_ASSERT_EQUAL_INT(0, h.now());
  TEST_ASSERT_EQUAL_INT(0, h.minVal());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_rolling_order_before_wrap);
  RUN_TEST(test_rolling_order_after_wrap);
  RUN_TEST(test_rolling_max_floor);
  RUN_TEST(test_hourgraph_default_cap);
  RUN_TEST(test_hourgraph_small_cap_wraps_at_cap);
  RUN_TEST(test_hourgraph_minmax);
  RUN_TEST(test_empty_graph_is_safe);
  return UNITY_END();
}
