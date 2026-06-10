#include "ui/Widgets.h"

#include "core/config.h"

using namespace theme;

namespace widgets {

void fmtRate(char *out, size_t cap, int kbs) {
  if (kbs >= 10000)
    snprintf(out, cap, "%d.%dM", kbs / 1024, (kbs % 1024) * 10 / 1024);
  else
    snprintf(out, cap, "%dK", kbs);
}

void statusBar(UiCtx &ui, const char *title) {
  LGFX_Sprite &g = ui.g;
  g.fillRect(0, 0, NOCT_W, NOCT_STATUS_H, BG);
  g.drawFastHLine(0, NOCT_STATUS_H - 1, NOCT_W, ORANGE_DIM);

  g.setFont(&F_TEXT);
  g.setTextSize(2);
  textAt(g, 4, 2, title, ORANGE);
  g.setTextSize(1);

  int x = NOCT_W - 4;

  /* clock from server payload */
  if (ui.st.pcClock[0]) {
    g.setFont(&F_TEXT);
    g.setTextSize(2);
    int w = g.textWidth(ui.st.pcClock);
    textAt(g, x - w, 2, ui.st.pcClock, TEXT);
    x -= w + 8;
    g.setTextSize(1);
  }

  /* WiFi RSSI bars */
  {
    int rssi = ui.st.link.rssi;
    int bars = !ui.st.link.wifiConnected ? 0
               : rssi > -55              ? 4
               : rssi > -65              ? 3
               : rssi > -75              ? 2
                                         : 1;
    for (int i = 0; i < 4; i++) {
      int bh = 3 + i * 3;
      uint16_t c = i < bars ? GOOD : PANEL;
      g.fillRect(x - 16 + i * 4, 14 - bh, 3, bh, c);
    }
    if (!ui.st.link.wifiConnected) {
      g.drawLine(x - 16, 3, x - 4, 14, CRIT);
    }
    x -= 22;
  }

  /* TCP link dot */
  {
    uint16_t c = ui.st.link.tcpConnected
                     ? (ui.st.link.signalLost ? WARN : GOOD)
                     : CRIT;
    g.fillCircle(x - 4, 9, 3, c);
    x -= 14;
  }

  /* SD chip */
  if (ui.st.link.sdOk) {
    g.drawRect(x - 9, 4, 9, 11, DIM);
    g.drawFastHLine(x - 7, 7, 5, DIM);
    g.drawFastHLine(x - 7, 10, 5, DIM);
    x -= 14;
  }

  /* LLM spark: cyan, blinks while a request is in flight */
  {
    bool busy = ui.st.link.llmBusy;
    bool show = !busy || ((ui.now / 200) & 1);
    uint16_t c = busy ? INFO : (ui.st.link.llmOk ? INFO : PANEL);
    if (show) {
      int cx = x - 6, cy = 9;
      g.drawLine(cx - 4, cy, cx + 4, cy, c);
      g.drawLine(cx, cy - 5, cx, cy + 5, c);
      g.drawLine(cx - 3, cy - 3, cx + 3, cy + 3, c);
      g.drawLine(cx - 3, cy + 3, cx + 3, cy - 3, c);
    }
    x -= 16;
  }

  /* alert dot */
  if (ui.st.alertActive && ((ui.now / 250) & 1)) {
    g.fillCircle(x - 5, 9, 4, CRIT);
  }
}

void footer(UiCtx &ui, const char *action) {
  LGFX_Sprite &g = ui.g;
  g.drawFastHLine(0, NOCT_FOOTER_TOP, NOCT_W, ORANGE_DIM);
  g.setFont(&F_TEXT);
  g.setTextSize(1);
  int y = NOCT_FOOTER_TOP + 6;
  textAt(g, 5, y, "1x", ORANGE);
  textAt(g, 20, y, "далее", DIM);
  textAt(g, 62, y, "2x", ORANGE);
  textAt(g, 77, y, "меню", DIM);
  textAt(g, 112, y, "3x", ORANGE);
  textAt(g, 127, y, "домой", DIM);
  if (action && action[0]) {
    textRight(g, NOCT_W - 5, y, action, ORANGE);
  }
}

void sparkline(LGFX_Sprite &g, int x, int y, int w, int h,
               const RollingGraph &gr, uint16_t color, int maxFloor) {
  g.drawRect(x, y, w, h, PANEL);
  if (gr.count < 2) return;
  int m = gr.maxVal(maxFloor);
  int n = gr.count;
  int px = -1, py = -1;
  for (int i = 0; i < n; i++) {
    int vx = x + 1 + (w - 3) * i / (n - 1);
    int vy = y + h - 2 - (h - 4) * gr.at(i) / m;
    if (px >= 0) g.drawLine(px, py, vx, vy, color);
    px = vx;
    py = vy;
  }
  if (px >= 0) g.fillCircle(px, py, 1, TEXT);
}

void valueTile(LGFX_Sprite &g, int x, int y, int w, int h, const char *label,
               const char *value, const char *unit, uint16_t accent) {
  panel(g, x, y, w, h, label);
  g.setFont(&F_BIG);
  g.setTextSize(1);
  int vw = g.textWidth(value);
  textAt(g, x + 8, y + (h - 24) / 2 + 2, value, accent);
  if (unit && unit[0]) {
    g.setFont(&F_TEXT);
    g.setTextSize(2);
    textAt(g, x + 10 + vw, y + h / 2 + 2, unit, DIM);
    g.setTextSize(1);
  }
}

void labelBar(LGFX_Sprite &g, int x, int y, int w, const char *label, int pct,
              const char *valueText, uint16_t color) {
  g.setFont(&F_TEXT);
  g.setTextSize(1);
  textAt(g, x, y, label, DIM);
  g.setFont(&F_VALUE);
  textRight(g, x + w, y - 2, valueText, TEXT);
  hBar(g, x, y + 10, w, 9, pct, color);
}

void speechBubble(UiCtx &ui, int x, int y, int w, int h) {
  LGFX_Sprite &g = ui.g;
  g.fillRoundRect(x, y, w, h, 6, PANEL);
  g.drawRoundRect(x, y, w, h, 6, ORANGE);
  /* tail toward the wolf (left) */
  g.fillTriangle(x + 2, y + h - 8, x - 8, y + h + 4, x + 14, y + h - 2, PANEL);
  g.drawLine(x + 2, y + h - 8, x - 8, y + h + 4, ORANGE);
  g.drawLine(x - 8, y + h + 4, x + 14, y + h - 2, ORANGE);

  g.setFont(&F_TEXT);
  g.setTextSize(2);
  if (ui.brain.thinking()) {
    int dots = 1 + (int)((ui.now / 350) % 3);
    char buf[8] = {0};
    for (int i = 0; i < dots; i++) buf[i] = '.';
    textAt(g, x + 10, y + h / 2 - 8, buf, INFO);
    g.setTextSize(1);
    return;
  }

  /* typewriter reveal, UTF-8 aware, word-wrapped into the box */
  const String &p = ui.brain.phrase();
  int reveal = ui.brain.revealChars(ui.now);
  int shown = 0;
  int lineY = y + 7;
  int cx = x + 9;
  const int lineH = 19;
  String word;
  for (size_t i = 0; i <= (size_t)p.length() && lineY < y + h - 10;) {
    bool end = i == (size_t)p.length();
    char c = end ? ' ' : p[i];
    if (c == ' ' || end) {
      int ww = g.textWidth(word.c_str());
      if (cx + ww > x + w - 8) {
        cx = x + 8;
        lineY += lineH;
        if (lineY >= y + h - 10) break;
      }
      textAt(g, cx, lineY, word.c_str(), TEXT);
      cx += ww + g.textWidth(" ");
      word = "";
      i++;
      shown++;
      if (shown >= reveal) break;
      continue;
    }
    /* consume one UTF-8 char */
    int bytes = (c & 0x80) == 0 ? 1 : (c & 0xE0) == 0xC0 ? 2 : 3;
    for (int b = 0; b < bytes && i < (size_t)p.length(); b++) word += p[i++];
    shown++;
    if (shown >= reveal) {
      /* draw the partial word too */
      int ww = g.textWidth(word.c_str());
      if (cx + ww > x + w - 8) {
        cx = x + 8;
        lineY += lineH;
      }
      if (lineY < y + h - 10) textAt(g, cx, lineY, word.c_str(), TEXT);
      break;
    }
  }
  g.setTextSize(1);
}

void xbmScaled(LGFX_Sprite &g, int x, int y, const unsigned char *bits, int w,
               int h, int scale, uint16_t color) {
  int bytesPerRow = (w + 7) / 8;
  for (int yy = 0; yy < h; yy++) {
    for (int xx = 0; xx < w; xx++) {
      if (bits[yy * bytesPerRow + (xx >> 3)] & (1 << (xx & 7))) {
        g.fillRect(x + xx * scale, y + yy * scale, scale, scale, color);
      }
    }
  }
}

void weatherIcon(LGFX_Sprite &g, int cx, int cy, int r, int wmo,
                 unsigned long now) {
  auto cloud = [&](int dx, int dy, uint16_t c) {
    g.fillCircle(cx - r / 2 + dx, cy + dy, r / 2, c);
    g.fillCircle(cx + dx, cy - r / 4 + dy, r / 2 + 2, c);
    g.fillCircle(cx + r / 2 + dx, cy + dy, r / 2, c);
    g.fillRect(cx - r / 2 + dx, cy + dy, r, r / 2, c);
  };
  if (wmo == 0) { /* clear */
    g.fillCircle(cx, cy, r / 2 + 2, WARN);
    for (int i = 0; i < 8; i++) {
      float a = i * PI / 4 + (now % 8000) * PI / 8000.0f / 4;
      g.drawLine(cx + cosf(a) * (r / 2 + 5), cy + sinf(a) * (r / 2 + 5),
                 cx + cosf(a) * (r - 1), cy + sinf(a) * (r - 1), WARN);
    }
  } else if (wmo <= 3) { /* partly cloudy */
    g.fillCircle(cx + r / 3, cy - r / 3, r / 2, WARN);
    cloud(0, r / 5, DIM);
  } else if (wmo >= 45 && wmo <= 48) { /* fog */
    for (int i = -2; i <= 2; i++)
      g.drawFastHLine(cx - r + ((i & 1) ? 4 : 0), cy + i * (r / 3), 2 * r - 6,
                      DIM);
  } else if ((wmo >= 51 && wmo <= 67) || (wmo >= 80 && wmo <= 82)) { /* rain */
    cloud(0, -r / 4, DIM);
    for (int i = -1; i <= 1; i++) {
      int ph = (now / 120 + i * 3) % 10;
      g.drawLine(cx + i * (r / 2), cy + r / 3 + ph, cx + i * (r / 2) - 2,
                 cy + r / 3 + ph + 5, INFO);
    }
  } else if ((wmo >= 71 && wmo <= 77) || wmo == 85 || wmo == 86) { /* snow */
    cloud(0, -r / 4, DIM);
    for (int i = -1; i <= 1; i++) {
      int ph = (now / 250 + i * 4) % 12;
      int sx = cx + i * (r / 2), sy = cy + r / 3 + ph;
      g.drawLine(sx - 2, sy, sx + 2, sy, TEXT);
      g.drawLine(sx, sy - 2, sx, sy + 2, TEXT);
    }
  } else if (wmo >= 95) { /* storm */
    cloud(0, -r / 4, DIM);
    bool on = (now / 300) & 1;
    if (on) {
      g.fillTriangle(cx, cy, cx - r / 3, cy + r / 2, cx + 2, cy + r / 4, WARN);
      g.fillTriangle(cx + 2, cy + r / 4, cx + r / 3, cy + r / 2, cx - 2,
                     cy + r, WARN);
    }
  } else {
    cloud(0, 0, DIM);
  }
}

} // namespace widgets
