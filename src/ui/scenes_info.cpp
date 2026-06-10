/* Nocturne C6 — info scenes: MEDIA, WEATHER, CLAUDE, FOREST, SERVICES,
 * EVENTS, SYSINFO. */
#include <WiFi.h>

#include "core/config.h"
#include "ui/Scenes.h"

using namespace theme;
using namespace widgets;

namespace scenes {

static uint16_t stColor(const char *st) {
  if (strcmp(st, "up") == 0) return GOOD;
  if (strcmp(st, "warn") == 0) return WARN;
  return CRIT;
}

/* ── MEDIA: cassette deck ────────────────────────────────────────────── */

static void reel(LGFX_Sprite &g, int cx, int cy, int r, float angle,
                 uint16_t c) {
  g.drawCircle(cx, cy, r, c);
  g.drawCircle(cx, cy, r - 6, ORANGE_DIM);
  for (int i = 0; i < 3; i++) {
    float a = angle + i * (2 * PI / 3);
    g.drawLine(cx + cosf(a) * 4, cy + sinf(a) * 4, cx + cosf(a) * (r - 7),
               cy + sinf(a) * (r - 7), c);
  }
  g.fillCircle(cx, cy, 3, c);
}

void drawMedia(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  MediaData &m = ui.st.media;
  bool playing = m.isPlaying;

  /* cassette body */
  panel(g, 30, 28, 260, 84);
  g.drawRoundRect(54, 40, 212, 60, 4, ORANGE_DIM);
  float angle = playing ? (ui.now % 3600) * 2 * PI / 3600.0f : 0.7f;
  reel(g, 100, 70, 20, angle, playing ? ORANGE : DIM);
  reel(g, 220, 70, 20, angle * 1.13f, playing ? ORANGE : DIM);
  /* tape between reels */
  g.drawFastHLine(118, 86, 84, DIM);
  g.drawFastHLine(118, 88, 84, ORANGE_DIM);
  /* window */
  g.drawRect(135, 56, 50, 18, DIM);

  /* status LED + text */
  const char *stTxt = playing            ? "PLAYING"
                      : m.isIdle         ? "IDLE"
                                         : "PAUSED";
  uint16_t stc = playing ? GOOD : WARN;
  if (playing && ((ui.now / 500) & 1)) g.fillCircle(44, 36, 3, stc);
  g.setFont(&F_SMALL);
  textAt(g, 52, 32, stTxt, stc);

  /* artist — track */
  g.setFont(&F_TEXT);
  g.setTextSize(2);
  String t = m.track.length() ? m.track : String("---");
  /* marquee when long */
  int tw = g.textWidth(t.c_str());
  int x = 8;
  if (tw > NOCT_W - 16) {
    int span = tw + 60;
    x = 8 - (int)((ui.now / 40) % span);
  }
  textAt(g, x, 116, t.c_str(), TEXT);
  g.setTextSize(1);
  textCenter(g, NOCT_W / 2, 138, m.artist.c_str(), ORANGE);
}

/* ── WEATHER ─────────────────────────────────────────────────────────── */

void drawWeather(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  WeatherData &w = ui.st.weather;
  if (!ui.st.weatherReceived) {
    g.setFont(&F_TEXT);
    textCenter(g, NOCT_W / 2, 80, "нет данных о погоде", DIM);
    return;
  }
  char v[32];

  weatherIcon(g, 50, 60, 26, w.wmoCode, ui.now);
  g.setFont(&F_HUGE);
  snprintf(v, sizeof(v), "%+d", w.temp);
  int vw = g.textWidth(v);
  textAt(g, 96, 38, v, TEXT);
  g.setFont(&F_TEXT);
  g.setTextSize(2);
  textAt(g, 100 + vw, 52, "C", DIM);
  g.setTextSize(1);
  textAt(g, 98, 78, w.desc.c_str(), ORANGE);

  /* 5-day forecast */
  static const char *dayNames[] = {"сег", "+1", "+2", "+3", "+4"};
  for (int i = 0; i < w.wfDays && i < 5; i++) {
    int x = 8 + i * 62;
    panel(g, x, 96, 56, 52);
    g.setFont(&F_SMALL);
    textCenter(g, x + 28, 100, dayNames[i], DIM);
    weatherIcon(g, x + 28, 118, 9, w.wfCode[i], ui.now);
    g.setFont(&F_TEXT);
    snprintf(v, sizeof(v), "%d..%d", w.wfMin[i], w.wfMax[i]);
    textCenter(g, x + 28, 134, v, TEXT);
  }
}

/* ── CLAUDE ──────────────────────────────────────────────────────────── */

void drawClaude(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  ClaudeData &c = ui.st.claude;
  char v[40];

  if (!c.available) {
    g.setFont(&F_TEXT);
    textCenter(g, NOCT_W / 2, 80, "Claude: нет данных", DIM);
    return;
  }

  /* 5h window gauge */
  panel(g, 4, 30, 200, 54, "ОКНО 5Ч");
  int win = c.windowPct < 0 ? 0 : c.windowPct;
  g.setFont(&F_VALUE);
  snprintf(v, sizeof(v), c.windowPct < 0 ? "n/a" : "%d%%", win);
  textAt(g, 14, 42, v, pctColor(win));
  hBar(g, 70, 44, 124, 14, win, pctColor(win));
  g.setFont(&F_SMALL);
  if (c.resetsInMin >= 0) {
    snprintf(v, sizeof(v), "сброс через %dч %02dм", c.resetsInMin / 60,
             c.resetsInMin % 60);
    textAt(g, 14, 68, v, DIM);
  }

  /* weekly gauge */
  panel(g, 4, 94, 200, 54, "НЕДЕЛЯ");
  int wk = c.weeklyPct < 0 ? 0 : c.weeklyPct;
  g.setFont(&F_VALUE);
  snprintf(v, sizeof(v), c.weeklyPct < 0 ? "n/a" : "%d%%", wk);
  textAt(g, 14, 106, v, pctColor(wk));
  hBar(g, 70, 108, 124, 14, wk, pctColor(wk));
  g.setFont(&F_SMALL);
  if (c.weeklyResetMin >= 0) {
    snprintf(v, sizeof(v), "сброс через %dд %dч", c.weeklyResetMin / 1440,
             (c.weeklyResetMin % 1440) / 60);
    textAt(g, 14, 132, v, DIM);
  }

  /* right column: plan + today numbers */
  panel(g, 212, 30, 104, 118, "СЕГОДНЯ");
  g.setFont(&F_TEXT);
  if (c.plan.length()) {
    String p = c.plan;
    p.toUpperCase();
    g.fillRoundRect(222, 38, g.textWidth(p.c_str()) + 10, 14, 3, ORANGE);
    textAt(g, 227, 41, p.c_str(), BG);
  }
  if (c.todayTokens >= 1000000)
    snprintf(v, sizeof(v), "%.1fM ток", c.todayTokens / 1e6);
  else
    snprintf(v, sizeof(v), "%ldK ток", c.todayTokens / 1000);
  textAt(g, 222, 62, v, TEXT);
  snprintf(v, sizeof(v), "%d сообщ", c.todayMsgs);
  textAt(g, 222, 78, v, DIM);
  snprintf(v, sizeof(v), "%d тулз", c.todayTools);
  textAt(g, 222, 94, v, DIM);
  if (c.stale) textAt(g, 222, 116, "данные устарели", WARN);
}

/* ── FOREST ──────────────────────────────────────────────────────────── */

void drawForest(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  ForestData &f = ui.st.forest;
  char v[24];
  if (f.count == 0) {
    g.setFont(&F_TEXT);
    textCenter(g, NOCT_W / 2, 80, "нет данных по нодам", DIM);
    return;
  }
  int shown = f.count > ForestData::kMaxNodes ? ForestData::kMaxNodes
                                              : f.count;
  int cols = shown > 3 ? 2 : 1;
  int rows = (shown + cols - 1) / cols;
  int cw = cols == 1 ? 312 : 154;
  int chh = rows > 2 ? 40 : (rows == 2 ? 60 : 120);
  for (int i = 0; i < shown; i++) {
    ForestNode &n = f.nodes[i];
    int x = 4 + (i % cols) * (cw + 4);
    int y = 28 + (i / cols) * (chh + 2);
    panel(g, x, y, cw, chh);
    uint16_t c = stColor(n.status);
    bool down = strcmp(n.status, "down") == 0;
    if (!down || ((ui.now / 400) & 1)) g.fillCircle(x + 10, y + 10, 4, c);
    g.setFont(&F_TEXT);
    textAt(g, x + 20, y + 5, n.name, TEXT);
    if (down) {
      textAt(g, x + 20, y + 20, "OFFLINE", CRIT);
      continue;
    }
    g.setFont(&F_SMALL);
    struct {
      const char *l;
      int val;
    } bars[] = {{"CPU", n.cpu}, {"RAM", n.ram}, {"HDD", n.disk}};
    for (int b = 0; b < 3; b++) {
      int bx = x + 8 + b * ((cw - 16) / 3);
      int val = bars[b].val < 0 ? 0 : bars[b].val;
      snprintf(v, sizeof(v), "%s %d%%", bars[b].l, val);
      textAt(g, bx, y + 20, v, DIM);
      hBar(g, bx, y + 29, (cw - 24) / 3 - 6, 7, val, pctColor(val));
    }
  }
}

/* ── SERVICES ────────────────────────────────────────────────────────── */

void drawServices(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  ServiceData &s = ui.st.services;
  char v[24];
  if (s.count == 0) {
    g.setFont(&F_TEXT);
    textCenter(g, NOCT_W / 2, 80, "нет данных о сервисах", DIM);
    return;
  }
  int shown = s.count > ServiceData::kMaxServices ? ServiceData::kMaxServices
                                                  : s.count;
  for (int i = 0; i < shown; i++) {
    ServiceEntry &e = s.list[i];
    int y = 28 + i * 15;
    uint16_t c = stColor(e.status);
    g.fillCircle(12, y + 4, 3, c);
    g.setFont(&F_TEXT);
    textAt(g, 24, y, e.name, TEXT);
    if (e.ms >= 0) {
      snprintf(v, sizeof(v), "%d ms", e.ms);
      textRight(g, 200, y, v, e.ms > 500 ? WARN : DIM);
    }
    g.drawFastHLine(8, y + 11, 200, PANEL);
  }

  /* docker summary */
  panel(g, 216, 28, 100, 60, "DOCKER");
  g.setFont(&F_VALUE);
  if (s.dockUp >= 0) {
    snprintf(v, sizeof(v), "%d/%d", s.dockUp, s.dockTotal);
    textAt(g, 226, 42, v,
           s.dockUp == s.dockTotal ? GOOD : WARN);
  } else {
    textAt(g, 226, 42, "n/a", DIM);
  }
  g.setFont(&F_SMALL);
  textAt(g, 226, 66, "контейнеры", DIM);

  panel(g, 216, 94, 100, 40, "ИТОГО");
  g.setFont(&F_VALUE);
  snprintf(v, sizeof(v), "%d/%d", s.up, s.count);
  textAt(g, 226, 104, v, s.up == s.count ? GOOD : CRIT);
}

/* ── EVENTS ──────────────────────────────────────────────────────────── */

void drawEvents(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  EventsData &e = ui.st.events;
  char v[32];

  if (e.count == 0) {
    g.setFont(&F_TEXT);
    g.setTextSize(2);
    textCenter(g, NOCT_W / 2, 70, "ТИШИНА В ЛЕСУ", GOOD);
    g.setTextSize(1);
    textCenter(g, NOCT_W / 2, 100, "активных алертов нет", DIM);
    return;
  }

  bool warnSev = strcmp(e.severity, "warning") == 0;
  uint16_t sc = warnSev ? WARN : CRIT;
  /* banner */
  g.fillRect(4, 26, 312, 34, PANEL);
  g.drawRect(4, 26, 312, 34, sc);
  if ((ui.now / 400) & 1) g.fillTriangle(16, 52, 28, 52, 22, 32, sc);
  g.setFont(&F_TEXT);
  g.setTextSize(2);
  textAt(g, 38, 32, e.top, sc);
  g.setTextSize(1);
  snprintf(v, sizeof(v), "%s  всего: %d", e.severity, e.count);
  textAt(g, 8, 66, v, DIM);

  /* human text, wrapped */
  g.setFont(&F_TEXT);
  String txt = e.text;
  int y = 80, x = 8;
  String word;
  for (size_t i = 0; i <= (size_t)txt.length(); i++) {
    char ch = i < (size_t)txt.length() ? txt[i] : ' ';
    if (ch == ' ') {
      int ww = g.textWidth(word.c_str());
      if (x + ww > 312) {
        x = 8;
        y += 13;
        if (y > 104) break;
      }
      textAt(g, x, y, word.c_str(), TEXT);
      x += ww + 5;
      word = "";
    } else {
      word += ch;
    }
  }

  /* firing list */
  int ly = 118;
  g.setFont(&F_SMALL);
  for (int i = 0; i < EventsData::kMaxList && e.list[i][0]; i++) {
    g.fillCircle(12, ly + 3, 2, sc);
    textAt(g, 20, ly, e.list[i], DIM);
    ly += 10;
  }
}

/* ── SYSINFO (overlay from menu) ─────────────────────────────────────── */

void drawSysInfo(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  char v[48];
  panel(g, 8, 26, 304, 122, "СИСТЕМА");
  g.setFont(&F_TEXT);
  int y = 36;
  snprintf(v, sizeof(v), "Nocturne C6 v%s", NOCT_VERSION);
  textAt(g, 18, y, v, ORANGE);
  y += 15;
  snprintf(v, sizeof(v), "heap: %u KB (min %u)",
           (unsigned)(ESP.getFreeHeap() / 1024),
           (unsigned)(ESP.getMinFreeHeap() / 1024));
  textAt(g, 18, y, v, TEXT);
  y += 15;
  unsigned long up = ui.now / 1000;
  snprintf(v, sizeof(v), "uptime: %luч %02luм %02luс", up / 3600,
           (up % 3600) / 60, up % 60);
  textAt(g, 18, y, v, TEXT);
  y += 15;
  snprintf(v, sizeof(v), "WiFi: %s  %d dBm", ui.st.link.ssid,
           ui.st.link.rssi);
  textAt(g, 18, y, v, TEXT);
  y += 15;
  snprintf(v, sizeof(v), "IP: %s", WiFi.localIP().toString().c_str());
  textAt(g, 18, y, v, TEXT);
  y += 15;
  snprintf(v, sizeof(v), "SD: %s   LLM: %s", ui.st.link.sdOk ? "GOOD" : "—",
           ui.st.link.llmOk ? "GOOD" : "—");
  textAt(g, 18, y, v, TEXT);
  y += 15;
  snprintf(v, sizeof(v), "возраст волка: %lu дней",
           (unsigned long)ui.pet.ageDays());
  textAt(g, 18, y, v, DIM);
}

} // namespace scenes
