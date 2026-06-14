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

#endif
