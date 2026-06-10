#include "ui/Theme.h"

#include <U8g2lib.h> /* font data only */

namespace theme {

const lgfx::U8g2font F_SMALL(u8g2_font_5x8_t_cyrillic);
const lgfx::U8g2font F_TEXT(u8g2_font_haxrcorp4089_t_cyrillic);
const lgfx::U8g2font F_MED(u8g2_font_10x20_t_cyrillic);
const lgfx::U8g2font F_VALUE(u8g2_font_helvB10_tr);
const lgfx::U8g2font F_BIG(u8g2_font_logisoso24_tr);
const lgfx::U8g2font F_HUGE(u8g2_font_logisoso32_tr);

void ditherRect(LGFX_Sprite &g, int x, int y, int w, int h, uint16_t color) {
  for (int yy = y; yy < y + h; yy++) {
    for (int xx = x + ((yy ^ x) & 1); xx < x + w; xx += 2) {
      g.drawPixel(xx, yy, color);
    }
  }
}

void panel(LGFX_Sprite &g, int x, int y, int w, int h, const char *title,
           uint16_t color, uint16_t titleColor) {
  const int cut = 4; /* cut corner, bottom-right */
  g.drawFastHLine(x, y, w - 1, color);
  g.drawFastVLine(x, y, h - 1, color);
  g.drawFastVLine(x + w - 1, y, h - cut, color);
  g.drawFastHLine(x, y + h - 1, w - cut, color);
  g.drawLine(x + w - 1, y + h - 1 - cut, x + w - 1 - cut, y + h - 1, color);
  if (title && title[0]) {
    g.setFont(&F_TEXT);
    g.setTextSize(1);
    int tw = g.textWidth(title);
    g.fillRect(x + 5, y - 5, tw + 8, 11, BG);
    g.setTextColor(titleColor);
    g.setCursor(x + 9, y - 4);
    g.print(title);
  }
}

void hBar(LGFX_Sprite &g, int x, int y, int w, int h, int pct, uint16_t color) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  g.drawRect(x, y, w, h, ORANGE_DIM);
  int fill = (w - 4) * pct / 100;
  if (fill > 0) {
    ditherRect(g, x + 2, y + 2, fill, h - 4, color);
    g.drawFastVLine(x + 2 + fill - 1, y + 2, h - 4, color); /* solid tip */
  }
}

void vBar(LGFX_Sprite &g, int x, int y, int w, int h, int pct, uint16_t color) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  g.drawRect(x, y, w, h, ORANGE_DIM);
  int fill = (h - 4) * pct / 100;
  if (fill > 0) {
    ditherRect(g, x + 2, y + h - 2 - fill, w - 4, fill, color);
    g.drawFastHLine(x + 2, y + h - 2 - fill, w - 4, color);
  }
}

void textAt(LGFX_Sprite &g, int x, int y, const char *s, uint16_t color) {
  g.setTextColor(color);
  g.setCursor(x, y);
  g.print(s);
}

void textRight(LGFX_Sprite &g, int xRight, int y, const char *s,
               uint16_t color) {
  int w = g.textWidth(s);
  textAt(g, xRight - w, y, s, color);
}

void textCenter(LGFX_Sprite &g, int xCenter, int y, const char *s,
                uint16_t color) {
  int w = g.textWidth(s);
  textAt(g, xCenter - w / 2, y, s, color);
}

} // namespace theme
