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

/* UTF-8 word-wrap with hard char-break for overlong words. */
int textWrap(LGFX_Sprite &g, const char *s, int x, int y, int w, int lineH,
             int maxLines, uint16_t color) {
  String line;
  String word;
  int lines = 0;
  auto flush = [&](bool force) {
    if (line.length() == 0 && !force) return;
    textAt(g, x, y, line.c_str(), color);
    y += lineH;
    lines++;
    line = "";
  };
  size_t i = 0, n = strlen(s);
  while (i <= n && lines < maxLines) {
    bool end = (i == n);
    char c = end ? ' ' : s[i];
    if (c == ' ' || end) {
      /* word complete: does it fit on the current line? */
      String trial = line.length() ? line + " " + word : word;
      if (g.textWidth(trial.c_str()) <= w) {
        line = trial;
      } else if (line.length()) {
        flush(false);
        line = word;
        if (g.textWidth(line.c_str()) > w) line = ""; /* fall to char-break */
      }
      if (line.length() == 0 && g.textWidth(word.c_str()) > w) {
        /* word alone too wide: break it character by character */
        String acc;
        for (size_t k = 0; k < (size_t)word.length() && lines < maxLines;) {
          int bytes = (word[k] & 0x80) == 0      ? 1
                      : (word[k] & 0xE0) == 0xC0 ? 2
                                                 : 3;
          String ch = word.substring(k, k + bytes);
          if (g.textWidth((acc + ch).c_str()) > w && acc.length()) {
            line = acc;
            flush(false);
            acc = ch;
          } else {
            acc += ch;
          }
          k += bytes;
        }
        line = acc;
      }
      word = "";
      i++;
      if (end) break;
      continue;
    }
    int bytes = (c & 0x80) == 0 ? 1 : (c & 0xE0) == 0xC0 ? 2 : 3;
    word += String(s).substring(i, i + bytes);
    i += bytes;
  }
  if (lines < maxLines) flush(false);
  return y;
}

void statusBar(UiCtx &ui, const char *title, int scene, int sceneCount) {
  LGFX_Sprite &g = ui.g;
  g.fillRect(0, 0, NOCT_W, NOCT_STATUS_H, BG);
  /* the separator line doubles as a scene-position ticker (replaces the old
   * footer): dim full width + a bright segment for the current scene */
  g.drawFastHLine(0, NOCT_STATUS_H - 1, NOCT_W, ORANGE_DIM);
  if (scene >= 0 && sceneCount > 0) {
    int seg = NOCT_W / sceneCount;
    g.fillRect(scene * seg, NOCT_STATUS_H - 2, seg, 2, ORANGE);
  }

  g.setFont(&F_MED);
  g.setTextSize(1);
  /* 12px corner inset: the physical panel corners are rounded */
  textAt(g, 12, 0, title, ORANGE);

  int x = NOCT_W - 12;

  /* clock from server payload */
  if (ui.st.pcClock[0]) {
    g.setFont(&F_MED);
    int w = g.textWidth(ui.st.pcClock);
    textAt(g, x - w, 0, ui.st.pcClock, TEXT);
    x -= w + 8;
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

void footer(UiCtx &ui, const char *action, int scene, int sceneCount) {
  LGFX_Sprite &g = ui.g;
  g.drawFastHLine(0, NOCT_FOOTER_TOP, NOCT_W, ORANGE_DIM);
  /* ring position ticker on the separator line itself */
  if (scene >= 0 && sceneCount > 0) {
    int seg = NOCT_W / sceneCount;
    g.drawFastHLine(scene * seg, NOCT_FOOTER_TOP, seg, ORANGE);
    g.drawFastHLine(scene * seg, NOCT_FOOTER_TOP + 1, seg, ORANGE);
  }
  g.setFont(&F_TEXT);
  g.setTextSize(1);
  int y = NOCT_FOOTER_TOP + 6;
  textAt(g, 14, y, "1x", ORANGE);
  textAt(g, 29, y, "далее", DIM);
  textAt(g, 71, y, "2x", ORANGE);
  textAt(g, 86, y, "меню", DIM);
  textAt(g, 121, y, "3x", ORANGE);
  textAt(g, 136, y, "домой", DIM);
  if (action && action[0]) {
    textRight(g, NOCT_W - 14, y, action, ORANGE);
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
  if (px >= 0) { /* pulsing tip beacon */
    int pr = 1 + (int)((sinf(theme::nowMs / 280.0f) + 1) * 1.2f);
    g.fillCircle(px, py, pr, lerp565(color, TEXT, 200));
    g.drawCircle(px, py, pr + 1, color);
  }
}

void valueTile(LGFX_Sprite &g, int x, int y, int w, int h, const char *label,
               const char *value, const char *unit, uint16_t accent) {
  panel(g, x, y, w, h, label);
  g.setFont(&F_BIG);
  g.setTextSize(1);
  int vw = g.textWidth(value);
  textAt(g, x + 8, y + (h - 24) / 2 + 2, value, accent);
  if (unit && unit[0]) {
    g.setFont(&F_MED);
    g.setTextSize(1);
    textAt(g, x + 10 + vw, y + h / 2 + 2, unit, DIM);
    g.setTextSize(1);
  }
}

void labelBar(LGFX_Sprite &g, int x, int y, int w, const char *label, int pct,
              const char *valueText, uint16_t color) {
  g.setFont(&F_MED);
  g.setTextSize(1);
  textAt(g, x, y, label, DIM);
  g.setTextSize(1);
  g.setFont(&F_VALUE);
  textRight(g, x + w, y + 3, valueText, TEXT);
  hBar(g, x, y + 16, w, 9, pct, color);
}

void speechBubble(UiCtx &ui, int x, int y, int w, int h) {
  LGFX_Sprite &g = ui.g;
  g.fillRoundRect(x, y, w, h, 6, PANEL);
  g.drawRoundRect(x, y, w, h, 6, ORANGE);
  /* tail toward the wolf (left) */
  g.fillTriangle(x + 2, y + h - 8, x - 8, y + h + 4, x + 14, y + h - 2, PANEL);
  g.drawLine(x + 2, y + h - 8, x - 8, y + h + 4, ORANGE);
  g.drawLine(x - 8, y + h + 4, x + 14, y + h - 2, ORANGE);

  g.setFont(&F_MED);
  g.setTextSize(1);
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
  int lineY = y + 6;
  int cx = x + 9;
  const int lineH = 21;
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
  /* gentle horizontal drift so every cloud "breathes" */
  int drift = (int)(sinf(now / 900.0f) * (r / 6.0f));
  auto cloud = [&](int dx, int dy, uint16_t c) {
    dx += drift;
    g.fillCircle(cx - r / 2 + dx, cy + dy, r / 2, c);
    g.fillCircle(cx + dx, cy - r / 4 + dy, r / 2 + 2, c);
    g.fillCircle(cx + r / 2 + dx, cy + dy, r / 2, c);
    g.fillRect(cx - r / 2 + dx, cy + dy, r, r / 2, c);
  };
  /* pulsing sun disc shared by clear + partly-cloudy */
  auto sun = [&](int sx, int sy, int rad, bool rays) {
    int pulse = (int)(sinf(now / 500.0f) * (rad / 6.0f + 1));
    g.fillCircle(sx, sy, rad + pulse, WARN);
    g.fillCircle(sx, sy, rad + pulse - 2, lerp565(WARN, TEXT, 90));
    if (rays)
      for (int i = 0; i < 8; i++) {
        float a = i * PI / 4 + (now % 8000) * PI / 8000.0f * 2;
        int r0 = rad + 3 + pulse, r1 = rad + 7 + pulse;
        g.drawLine(sx + cosf(a) * r0, sy + sinf(a) * r0, sx + cosf(a) * r1,
                   sy + sinf(a) * r1, WARN);
      }
  };

  if (wmo == 0) { /* clear — pulsing sun with rotating rays */
    sun(cx, cy, r / 2, true);
  } else if (wmo <= 3) { /* partly cloudy — sun peeking, cloud drifting */
    sun(cx + r / 3, cy - r / 3, r / 3, true);
    cloud(0, r / 5, DIM);
  } else if (wmo >= 45 && wmo <= 48) { /* fog — scrolling bands */
    for (int i = -2; i <= 2; i++) {
      int off = (int)(sinf(now / 600.0f + i) * 4);
      g.drawFastHLine(cx - r + 3 + off, cy + i * (r / 3), 2 * r - 6,
                      lerp565(DIM, TEXT, 60));
    }
  } else if ((wmo >= 51 && wmo <= 67) || (wmo >= 80 && wmo <= 82)) { /* rain */
    cloud(0, -r / 4, DIM);
    for (int i = -1; i <= 1; i++) {
      int ph = (now / 110 + i * 4) % 12;
      int rx = cx + i * (r / 2) + drift;
      g.drawLine(rx, cy + r / 4 + ph, rx - 2, cy + r / 4 + ph + 5, INFO);
    }
  } else if ((wmo >= 71 && wmo <= 77) || wmo == 85 || wmo == 86) { /* snow */
    cloud(0, -r / 4, DIM);
    for (int i = -1; i <= 1; i++) {
      int ph = (now / 220 + i * 5) % 14;
      int sx = cx + i * (r / 2) + drift + (int)(sinf(now / 300.0f + i) * 3);
      int sy = cy + r / 4 + ph;
      g.drawLine(sx - 2, sy, sx + 2, sy, TEXT);
      g.drawLine(sx, sy - 2, sx, sy + 2, TEXT);
    }
  } else if (wmo >= 95) { /* storm — flashing bolt + flicker cloud */
    bool on = (now / 250) & 1;
    cloud(0, -r / 4, on ? lerp565(DIM, WARN, 60) : DIM);
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
