/*
 * Host tests for HeapStats — the bookkeeping behind the board's `heap`
 * console report. The device feeds it the lowest free heap of each 10 ms
 * window; if the buckets, the tag attribution or the dip ring are wrong, the
 * report points at the wrong subsystem and the hunt for the culprit starts
 * in the wrong place.
 */
#include <unity.h>

#include <string.h>

#include "core/HeapStats.h"

void setUp() {}
void tearDown() {}

static void test_bucket_edges() {
  TEST_ASSERT_EQUAL_INT(0, HeapStats::bucketOf(0));
  TEST_ASSERT_EQUAL_INT(0, HeapStats::bucketOf(1023));
  TEST_ASSERT_EQUAL_INT(1, HeapStats::bucketOf(1024));
  TEST_ASSERT_EQUAL_INT(3, HeapStats::bucketOf(8191));
  TEST_ASSERT_EQUAL_INT(4, HeapStats::bucketOf(8192));
  TEST_ASSERT_EQUAL_INT(6, HeapStats::bucketOf(24575));
  TEST_ASSERT_EQUAL_INT(7, HeapStats::bucketOf(24576));
  TEST_ASSERT_EQUAL_INT(7, HeapStats::bucketOf(300000));
}

static void test_counts_and_tag_attribution() {
  HeapStats s;
  s.add(10, 30000, heaptag::DRAW);                  /* shallow, drawing */
  s.add(20, 5000, heaptag::DRAW | heaptag::COVER);  /* deep, both */
  s.add(30, 31000, 0);                              /* shallow, idle */
  TEST_ASSERT_EQUAL_UINT32(3, s.windows);
  TEST_ASSERT_EQUAL_UINT32(1, s.deepWindows);
  TEST_ASSERT_EQUAL_UINT32(2, s.buckets[7]);
  TEST_ASSERT_EQUAL_UINT32(1, s.buckets[3]);
  /* DRAW ran in two windows, one of them deep; COVER only in the deep one. */
  TEST_ASSERT_EQUAL_UINT32(2, s.tagAny[10]);
  TEST_ASSERT_EQUAL_UINT32(1, s.tagDeep[10]);
  TEST_ASSERT_EQUAL_UINT32(1, s.tagAny[3]);
  TEST_ASSERT_EQUAL_UINT32(1, s.tagDeep[3]);
  TEST_ASSERT_EQUAL_UINT32(5000, s.lowest);
  TEST_ASSERT_EQUAL_UINT32(20, s.lowestMs);
  TEST_ASSERT_EQUAL_UINT32(heaptag::DRAW | heaptag::COVER, s.lowestTags);
}

static void test_one_trough_is_one_dip_unless_it_deepens() {
  HeapStats s;
  s.add(1000, 6000, heaptag::PARSE);
  s.add(1010, 7000, heaptag::PARSE); /* same trough, shallower: not a new dip */
  s.add(1020, 4000, heaptag::LINK);  /* deeper: always recorded */
  s.add(1030, 5000, heaptag::LINK);  /* shallower again, within the gap */
  TEST_ASSERT_EQUAL_UINT32(2, s.dipsTotal);
  TEST_ASSERT_EQUAL_UINT32(4000, s.dip(0)->minBytes);
  TEST_ASSERT_EQUAL_UINT32(heaptag::LINK, s.dip(0)->tags);
  TEST_ASSERT_EQUAL_UINT32(6000, s.dip(1)->minBytes);
  /* A second later the same depth counts as a separate event. */
  s.add(2030, 5000, heaptag::SD);
  TEST_ASSERT_EQUAL_UINT32(3, s.dipsTotal);
  TEST_ASSERT_EQUAL_UINT32(heaptag::SD, s.dip(0)->tags);
  /* Shallow windows never become dips. */
  s.add(5000, 9000, heaptag::SD);
  TEST_ASSERT_EQUAL_UINT32(3, s.dipsTotal);
}

static void test_dip_ring_keeps_the_newest() {
  HeapStats s;
  for (uint32_t i = 0; i < HeapStats::kDips + 5; i++)
    s.add(1000 + i * 2000, 1000 + i, 0);
  TEST_ASSERT_EQUAL_UINT32(HeapStats::kDips + 5, s.dipsTotal);
  TEST_ASSERT_EQUAL_UINT32(1000 + HeapStats::kDips + 4, s.dip(0)->minBytes);
  TEST_ASSERT_EQUAL_UINT32(1000 + 5, s.dip(HeapStats::kDips - 1)->minBytes);
  TEST_ASSERT_NULL(s.dip(HeapStats::kDips));
}

static void test_dip_gap_survives_millis_wrap() {
  HeapStats s;
  s.add(0xFFFFFF00u, 3000, 0);
  s.add(0xFFFFFF00u + 2000u, 3500, 0); /* wrapped past zero, 2 s later */
  TEST_ASSERT_EQUAL_UINT32(2, s.dipsTotal);
}

static void test_reset_keeps_the_threshold() {
  HeapStats s;
  s.deepBytes = 4096;
  s.add(10, 100, heaptag::ZB);
  s.reset();
  TEST_ASSERT_EQUAL_UINT32(0, s.windows);
  TEST_ASSERT_EQUAL_UINT32(0, s.dipsTotal);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, s.lowest);
  TEST_ASSERT_NULL(s.dip(0));
  TEST_ASSERT_EQUAL_UINT32(4096, s.deepBytes);
}

static void test_mean_minimum_per_tag() {
  HeapStats s;
  s.add(10, 30000, 0);
  s.add(20, 20000, heaptag::PARSE);
  s.add(30, 10000, heaptag::PARSE);
  TEST_ASSERT_EQUAL_UINT32(20000, s.meanMin());
  /* PARSE costs: while it runs the window minimum averages 15000, not 20000. */
  TEST_ASSERT_EQUAL_UINT32(15000, s.tagMeanMin(0));
  TEST_ASSERT_EQUAL_UINT32(10000, s.tagLow[0]);
  TEST_ASSERT_EQUAL_UINT32(0, s.tagMeanMin(3)); /* never active */
  TEST_ASSERT_EQUAL_UINT32(0, s.tagMeanMin(99));
}

static void test_block_hist_bins() {
  TEST_ASSERT_EQUAL_INT(0, BlockHist::binOf(1));
  TEST_ASSERT_EQUAL_INT(0, BlockHist::binOf(32));
  TEST_ASSERT_EQUAL_INT(1, BlockHist::binOf(33));
  TEST_ASSERT_EQUAL_INT(6, BlockHist::binOf(1536));
  /* WiFi/lwIP frame buffers land in their own bin. */
  TEST_ASSERT_EQUAL_INT(7, BlockHist::binOf(1600));
  TEST_ASSERT_EQUAL_INT(7, BlockHist::binOf(1700));
  TEST_ASSERT_EQUAL_INT(8, BlockHist::binOf(1701));
  TEST_ASSERT_EQUAL_INT(13, BlockHist::binOf(32769));
  BlockHist h;
  h.add(1600, true);
  h.add(1650, true);
  h.add(100, true);
  h.add(5000, false);
  h.add(700, false);
  TEST_ASSERT_EQUAL_UINT32(3, h.usedBlocks);
  TEST_ASSERT_EQUAL_UINT32(3350, h.usedBytes);
  TEST_ASSERT_EQUAL_UINT32(2, h.count[7]);
  TEST_ASSERT_EQUAL_UINT32(3250, h.bytes[7]);
  TEST_ASSERT_EQUAL_UINT32(2, h.freeBlocks);
  TEST_ASSERT_EQUAL_UINT32(5700, h.freeBytes);
  TEST_ASSERT_EQUAL_UINT32(5000, h.largestFree);
}

static void test_tag_format() {
  char b[64];
  heaptag::format(0, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("-", b);
  heaptag::format(heaptag::PARSE | heaptag::DRAW, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("PARSE|DRAW", b);
  /* Too small: truncated, but always terminated inside the buffer. */
  char small[8];
  memset(small, 'x', sizeof(small));
  heaptag::format(heaptag::PARSE | heaptag::DRAW | heaptag::COVER, small,
                  sizeof(small));
  TEST_ASSERT_TRUE(memchr(small, 0, sizeof(small)) != nullptr);
  /* Every defined bit has a real name. */
  for (int b2 = 0; b2 < heaptag::kCount; b2++)
    TEST_ASSERT_TRUE(strcmp(heaptag::name(b2), "?") != 0);
  TEST_ASSERT_EQUAL_STRING("?", heaptag::name(heaptag::kCount));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_bucket_edges);
  RUN_TEST(test_counts_and_tag_attribution);
  RUN_TEST(test_one_trough_is_one_dip_unless_it_deepens);
  RUN_TEST(test_dip_ring_keeps_the_newest);
  RUN_TEST(test_dip_gap_survives_millis_wrap);
  RUN_TEST(test_reset_keeps_the_threshold);
  RUN_TEST(test_mean_minimum_per_tag);
  RUN_TEST(test_block_hist_bins);
  RUN_TEST(test_tag_format);
  return UNITY_END();
}
