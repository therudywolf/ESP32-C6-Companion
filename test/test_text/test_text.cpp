/*
 * Host tests for the text pipeline every server string and LLM reply goes
 * through. This is where the bugs have actually been: v1.8.6's "UTF-8-safe"
 * truncation dropped the last Cyrillic letter of every string it touched, even
 * when the string fitted whole — invisible in a build, obvious in one assert.
 */
#include <unity.h>

#include "core/TextUtil.h"

void setUp() {}
void tearDown() {}

/* ── copyUtf8: truncate on a codepoint, never mangle a complete one ─────── */

static void test_copy_ascii_fits() {
  char b[16];
  copyUtf8(b, sizeof(b), "up");
  TEST_ASSERT_EQUAL_STRING("up", b);
}

static void test_copy_cyrillic_fits_whole() {
  /* the regression: "диск" fits in 21 bytes and must come back intact */
  char b[21];
  copyUtf8(b, sizeof(b), "диск");
  TEST_ASSERT_EQUAL_STRING("диск", b);
}

static void test_copy_long_cyrillic_fits_whole() {
  char b[61];
  copyUtf8(b, sizeof(b), "Высокая загрузка");
  TEST_ASSERT_EQUAL_STRING("Высокая загрузка", b);
}

static void test_copy_truncates_on_boundary() {
  /* "абв" is 6 bytes; a 6-byte buffer holds 5 -> "аб" plus a half "в" that
   * must be dropped, leaving 4 bytes. */
  char b[6];
  copyUtf8(b, sizeof(b), "абв");
  TEST_ASSERT_EQUAL_STRING("аб", b);
  TEST_ASSERT_EQUAL_INT(4, (int)strlen(b));
}

static void test_copy_truncation_is_valid_utf8() {
  /* every prefix length must end on a whole codepoint */
  for (size_t cap = 1; cap <= 20; cap++) {
    char b[24];
    copyUtf8(b, cap, "температура");
    for (size_t i = 0; i < strlen(b);) {
      int len = utf8SeqLen((uint8_t)b[i]);
      TEST_ASSERT_TRUE(i + (size_t)len <= strlen(b));
      i += (size_t)len;
    }
  }
}

static void test_copy_empty_and_null() {
  char b[8];
  copyUtf8(b, sizeof(b), "");
  TEST_ASSERT_EQUAL_STRING("", b);
  copyUtf8(b, sizeof(b), nullptr);
  TEST_ASSERT_EQUAL_STRING("", b);
}

/* ── stripGlyphs: the fonts only carry ASCII + Cyrillic + em-dash ───────── */

static void test_strip_keeps_ascii_and_cyrillic() {
  TEST_ASSERT_EQUAL_STRING("CPU 80 Ядро",
                           stripGlyphs(String("CPU 80 Ядро")).c_str());
}

static void test_strip_drops_emoji_and_accents() {
  TEST_ASSERT_EQUAL_STRING("ok ", stripGlyphs(String("ok 🐺é")).c_str());
}

static void test_strip_maps_punctuation() {
  TEST_ASSERT_EQUAL_STRING("...", stripGlyphs(String("…")).c_str());
  TEST_ASSERT_EQUAL_STRING("-", stripGlyphs(String("·")).c_str());
  TEST_ASSERT_EQUAL_STRING("да", stripGlyphs(String("«да»")).c_str());
}

static void test_strip_emoji_variant_keeps_emoji() {
  /* the notification renderer blits these from the atlas, so they survive */
  TEST_ASSERT_EQUAL_STRING("🐺", stripGlyphsEmoji(String("🐺")).c_str());
  TEST_ASSERT_EQUAL_STRING("", stripGlyphs(String("🐺")).c_str());
}

static void test_strip_survives_truncated_utf8() {
  /* a payload cut mid-sequence must not read past the end */
  char broken[] = {(char)0xD0, (char)0xB4, (char)0xD0, '\0'}; /* "д" + half */
  TEST_ASSERT_EQUAL_STRING("д", stripGlyphs(String(broken)).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_copy_ascii_fits);
  RUN_TEST(test_copy_cyrillic_fits_whole);
  RUN_TEST(test_copy_long_cyrillic_fits_whole);
  RUN_TEST(test_copy_truncates_on_boundary);
  RUN_TEST(test_copy_truncation_is_valid_utf8);
  RUN_TEST(test_copy_empty_and_null);
  RUN_TEST(test_strip_keeps_ascii_and_cyrillic);
  RUN_TEST(test_strip_drops_emoji_and_accents);
  RUN_TEST(test_strip_maps_punctuation);
  RUN_TEST(test_strip_emoji_variant_keeps_emoji);
  RUN_TEST(test_strip_survives_truncated_utf8);
  return UNITY_END();
}
