/*
 * Nocturne C6 — text sanitiser. The device fonts (u8g2 *_t_cyrillic) only carry
 * ASCII + Cyrillic + the em-dash; anything else (guillemets «», bullets,
 * ellipsis, emoji, accented Latin, exotic punctuation) renders as a tofu box.
 * Server payloads (track/artist, alert text, process names, SSID…) and LLM
 * replies are arbitrary Unicode, so run them through stripGlyphs() before they
 * reach the screen. Keeps ASCII 0x20..0x7E, Cyrillic U+0400..04FF and the
 * em-dash; maps a couple of common marks to ASCII; drops the rest.
 */
#ifndef NOCT_TEXT_UTIL_H
#define NOCT_TEXT_UTIL_H

#include <Arduino.h>
#include <string.h>

inline String stripGlyphs(const String &in) {
  String out;
  out.reserve(in.length());
  int n = in.length();
  for (int i = 0; i < n;) {
    uint8_t c = (uint8_t)in[i];
    if (c < 0x80) { /* ASCII: keep printable + space, drop other control */
      if (c >= 0x20) out += (char)c;
      i += 1;
      continue;
    }
    int len = (c < 0xC0) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
    if (i + len > n) break; /* truncated trailing bytes */
    uint32_t cp = 0;
    if (len == 2)
      cp = ((c & 0x1F) << 6) | ((uint8_t)in[i + 1] & 0x3F);
    else if (len == 3)
      cp = ((c & 0x0F) << 12) | (((uint8_t)in[i + 1] & 0x3F) << 6) |
           ((uint8_t)in[i + 2] & 0x3F);
    else if (len == 4)
      cp = ((c & 0x07) << 18) | (((uint8_t)in[i + 1] & 0x3F) << 12) |
           (((uint8_t)in[i + 2] & 0x3F) << 6) | ((uint8_t)in[i + 3] & 0x3F);

    if ((cp >= 0x0400 && cp <= 0x04FF) || cp == 0x2014) {
      for (int k = 0; k < len; k++) out += in[i + k]; /* Cyrillic / em-dash */
    } else if (cp == 0x2026) {
      out += "..."; /* ellipsis */
    } else if (cp == 0x2013 || cp == 0x2022 || cp == 0x00B7) {
      out += '-'; /* en-dash / bullet / middle dot */
    } else if (cp == 0x00AB || cp == 0x00BB || cp == 0x201C || cp == 0x201D ||
               cp == 0x201E || cp == 0x2018 || cp == 0x2019) {
      /* guillemets «» and curly quotes — drop entirely */
    }
    /* everything else (emoji, accented Latin, symbols) dropped */
    i += len;
  }
  return out;
}

/* Byte length of the UTF-8 sequence a lead byte announces (1 for ASCII, and 1
 * for a stray continuation byte so callers always make progress). */
inline int utf8SeqLen(uint8_t lead) {
  if (lead < 0x80) return 1;
  if (lead < 0xC0) return 1; /* orphan continuation byte */
  if (lead < 0xE0) return 2;
  if (lead < 0xF0) return 3;
  return 4;
}

/* Copy `src` into a fixed char buffer, truncating on a whole codepoint.
 *
 * strncpy counts BYTES, so a Cyrillic string landing exactly on the boundary
 * leaves a half-written sequence behind and the renderer draws a tofu box. The
 * fix is to drop only an INCOMPLETE trailing sequence: walk back to the lead
 * byte and compare the bytes actually present against the length that lead
 * announces. A previous version unconditionally dropped the trailing lead byte,
 * which ate the last letter of every Cyrillic string even when it fit whole
 * ("диск" -> "дис"). dst is always NUL-terminated. */
inline void copyUtf8(char *dst, size_t cap, const char *src) {
  if (!dst || cap == 0) return;
  if (!src) src = "";
  size_t n = strlen(src);
  if (n > cap - 1) n = cap - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
  if (n == 0) return;

  /* find the lead byte of the last (possibly partial) sequence */
  size_t lead = n - 1;
  while (lead > 0 && ((uint8_t)dst[lead] & 0xC0) == 0x80) lead--;
  uint8_t lb = (uint8_t)dst[lead];
  if (lb < 0x80) return; /* ASCII tail — always complete */
  size_t need = (size_t)utf8SeqLen(lb);
  if (lead + need > n) dst[lead] = '\0'; /* incomplete: drop it whole */
}

/* Like stripGlyphs but ALSO preserves emoji-range codepoints (so the inline
 * emoji renderer can blit them from the atlas). Use only for fields drawn via
 * widgets::drawEmojiText (notification app/sender/body) — never for text drawn
 * with plain textAt, or the emoji would render as tofu. Non-atlas emoji in the
 * range are kept here but simply skipped by drawEmojiText. */
inline bool isEmojiCp(uint32_t cp) {
  return (cp >= 0x1F000 && cp <= 0x1FAFF) || (cp >= 0x2600 && cp <= 0x27BF) ||
         (cp >= 0x2300 && cp <= 0x23FF) || (cp >= 0x2B00 && cp <= 0x2BFF) ||
         (cp >= 0x2190 && cp <= 0x21FF) || cp == 0x2122 || cp == 0x2139;
}

inline String stripGlyphsEmoji(const String &in) {
  String out;
  out.reserve(in.length());
  int n = in.length();
  for (int i = 0; i < n;) {
    uint8_t c = (uint8_t)in[i];
    if (c < 0x80) {
      if (c >= 0x20) out += (char)c;
      i += 1;
      continue;
    }
    int len = (c < 0xC0) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
    if (i + len > n) break;
    uint32_t cp = 0;
    if (len == 2)
      cp = ((c & 0x1F) << 6) | ((uint8_t)in[i + 1] & 0x3F);
    else if (len == 3)
      cp = ((c & 0x0F) << 12) | (((uint8_t)in[i + 1] & 0x3F) << 6) |
           ((uint8_t)in[i + 2] & 0x3F);
    else if (len == 4)
      cp = ((c & 0x07) << 18) | (((uint8_t)in[i + 1] & 0x3F) << 12) |
           (((uint8_t)in[i + 2] & 0x3F) << 6) | ((uint8_t)in[i + 3] & 0x3F);

    if ((cp >= 0x0400 && cp <= 0x04FF) || cp == 0x2014 || isEmojiCp(cp)) {
      for (int k = 0; k < len; k++) out += in[i + k]; /* Cyrillic / em-dash / emoji */
    } else if (cp == 0x2026) {
      out += "...";
    } else if (cp == 0x2013 || cp == 0x2022 || cp == 0x00B7) {
      out += '-';
    }
    /* guillemets / curly quotes / accented latin dropped */
    i += len;
  }
  return out;
}

#endif
