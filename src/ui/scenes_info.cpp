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

/* "M:SS  [====o─────]  M:SS" track timeline, drawn in the footer band.
 * The server sends a position snapshot (mpos) + length (mdur); we interpolate
 * locally while playing so the playhead glides instead of stepping each second.
 * dur<=0 (live stream / ad with no length) -> an indeterminate shimmer. */
static void mediaTimeline(LGFX_Sprite &g, const MediaData &m, bool playing,
                          unsigned long now, int y) {
  int dur = m.durSec;
  float pos = (float)m.posSec;
  /* interpolate only for a recent snapshot of a KNOWN-length track: a telemetry
   * stall then can't run the clock away (mirrors the server's 30 s bound), and an
   * unknown-length stream/ad just shows its frozen snapshot instead of climbing. */
  unsigned long age = now - m.posStamp;
  if (playing && m.posStamp && dur > 0 && age < 30000UL)
    pos += (float)age / 1000.0f;
  if (pos < 0) pos = 0;
  if (dur > 0 && pos > (float)dur) pos = (float)dur;

  char et[8], tt[8];
  snprintf(et, sizeof(et), "%d:%02d", (int)pos / 60, (int)pos % 60);
  if (dur > 0) snprintf(tt, sizeof(tt), "%d:%02d", dur / 60, dur % 60);
  else         snprintf(tt, sizeof(tt), "--:--");

  g.setFont(&F_TEXT);
  g.setTextSize(1);
  int etw = g.textWidth(et), ttw = g.textWidth(tt);
  int by = y + 5, bh = 5;            /* progress bar, just below the time labels */
  int bx = 10 + etw + 8;
  int bex = NOCT_W - 10 - ttw - 8;
  int bw = bex - bx;
  if (bw < 20) bw = 20;

  textAt(g, 10, y, et, playing ? TEXT : DIM);
  textAt(g, NOCT_W - 10 - ttw, y, tt, DIM);

  g.fillRoundRect(bx, by, bw, bh, 2, PANEL);
  g.drawRoundRect(bx, by, bw, bh, 2, ORANGE_DIM);

  if (dur > 0) {
    float frac = pos / (float)dur;
    if (frac > 1) frac = 1;
    int fw = (int)(bw * frac);
    if (fw > 1) g.fillRoundRect(bx, by, fw, bh, 2, ACCENT);
    int hx = bx + fw;                /* playhead — a tick kept inside the bar */
    if (hx > bx + bw - 1) hx = bx + bw - 1;
    g.drawFastVLine(hx, by, bh, playing ? TEXT : DIM);
  } else {
    int seg = bw / 4;               /* unknown length -> sliding highlight */
    int off = (int)((now / 6) % (unsigned long)(bw + seg)) - seg;
    g.setClipRect(bx, by, bw, bh);
    g.fillRoundRect(bx + off, by, seg, bh, 2, playing ? ACCENT : ORANGE_DIM);
    g.clearClipRect();
  }
}

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

  /* Spotify mode: real album cover + track/artist + a colour spectrum. */
  if (ui.cover) {
    const int cw = 96, cx = 10, cy = 26;
    /* track-change transition: the cover reveals top-down, its frame flashes,
     * and a "НОВЫЙ ТРЕК" badge fades in/out. */
    static String lastTrack;
    static unsigned long changeAt = 0;
    if (m.track != lastTrack) {
      lastTrack = m.track;
      changeAt = ui.now;
    }
    unsigned long age = changeAt ? ui.now - changeAt : 999999UL;
    int shown = age < 380 ? (int)((float)age / 380.0f * cw) : cw;

    bool sw = g.getSwapBytes();
    g.setSwapBytes(true); /* cover bytes are RGB565; match the sprite's order */
    if (shown >= cw) {
      g.pushImage(cx, cy, cw, cw, ui.cover);
    } else if (shown > 0) {
      g.setClipRect(cx, cy, cw, shown); /* reveal from the top down */
      g.pushImage(cx, cy, cw, cw, ui.cover);
      g.clearClipRect();
      g.drawFastHLine(cx, cy + shown, cw, ACCENT); /* sweeping reveal edge */
    }
    g.setSwapBytes(sw);
    uint16_t frameC =
        (age < 600 && ((ui.now / 120) & 1)) ? ACCENT : ORANGE; /* flash */
    g.drawRect(cx - 1, cy - 1, cw + 2, cw + 2, frameC);
    g.drawRect(cx - 2, cy - 2, cw + 4, cw + 4, ORANGE_DIM);

    int rx = cx + cw + 12, rw = NOCT_W - rx - 6; /* right column */
    const char *stTxt = playing ? "PLAYING" : (m.isIdle ? "IDLE" : "PAUSED");
    uint16_t stc = playing ? GOOD : WARN;
    g.setFont(&F_TEXT);
    if (!playing || ((ui.now / 500) & 1)) g.fillCircle(rx + 3, cy + 4, 3, stc);
    textAt(g, rx + 10, cy, stTxt, stc);

    g.setFont(&F_MED);
    g.setTextSize(1);
    String t = m.track.length() ? m.track : String("--- нет трека ---");
    int tw = g.textWidth(t.c_str());
    g.setClipRect(rx, 48, rw, 20);
    if (tw > rw) { /* scroll long titles seamlessly */
      int span = tw + 40, off = (int)((ui.now / 35) % span);
      textAt(g, rx - off, 52, t.c_str(), TEXT);
      textAt(g, rx - off + span, 52, t.c_str(), TEXT);
    } else {
      textAt(g, rx, 52, t.c_str(), TEXT);
    }
    g.clearClipRect();
    g.setClipRect(rx, 74, rw, 20); /* artist, scroll if long too */
    int aw = g.textWidth(m.artist.c_str());
    if (aw > rw) {
      int span = aw + 40, off = (int)((ui.now / 40) % span);
      textAt(g, rx - off, 76, m.artist.c_str(), ORANGE);
      textAt(g, rx - off + span, 76, m.artist.c_str(), ORANGE);
    } else {
      textAt(g, rx, 76, m.artist.c_str(), ORANGE);
    }
    g.clearClipRect();

    if (age < 1500) { /* "НОВЫЙ ТРЕК" badge, fades in then out */
      int a = age < 200 ? (int)(age * 255 / 200)
              : age > 1200 ? (int)((1500 - age) * 255 / 300)
                           : 255;
      a = a < 0 ? 0 : (a > 255 ? 255 : a);
      g.setFont(&F_TEXT);
      const char *nt = "НОВЫЙ ТРЕК";
      int bwl = g.textWidth(nt) + 14;
      g.fillRoundRect(rx, 98, bwl, 14, 4, lerp565(BG, PANEL, a));
      g.drawRoundRect(rx, 98, bwl, 14, 4, lerp565(BG, ACCENT, a));
      textAt(g, rx + 7, 101, nt, lerp565(BG, ACCENT, a));
    }

    /* real track timeline across the bottom (replaces the old fake spectrum) */
    mediaTimeline(g, m, playing, ui.now, 154);
    return;
  }

  /* cassette body (no cover / "all" media mode) */
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
  g.setFont(&F_TEXT);
  textAt(g, 52, 32, stTxt, stc);

  /* track (seamless scrolling marquee) + artist */
  g.setFont(&F_MED);
  g.setTextSize(1);
  String t = m.track.length() ? m.track : String("--- нет трека ---");
  int tw = g.textWidth(t.c_str());
  if (tw > NOCT_W - 16) {
    int span = tw + 50; /* gap between loop copies */
    int off = (int)((ui.now / 35) % span);
    g.setClipRect(4, 108, NOCT_W - 8, 18); /* keep it on its own line */
    textAt(g, 8 - off, 118, t.c_str(), TEXT);
    textAt(g, 8 - off + span, 118, t.c_str(), TEXT); /* second copy = seamless */
    g.clearClipRect();
  } else {
    textCenter(g, NOCT_W / 2, 118, t.c_str(), TEXT);
  }
  String a = m.artist;
  int aw = g.textWidth(a.c_str());
  if (aw > NOCT_W - 8) a = a.substring(0, 24);
  textCenter(g, NOCT_W / 2, 138, a.c_str(), ORANGE);

  /* track timeline across the freed bottom band (replaces the equaliser) */
  mediaTimeline(g, m, playing, ui.now, 154);
}

/* ── WEATHER ─────────────────────────────────────────────────────────── */

/* WMO -> Russian. Multi-word where long so it wraps on a WORD boundary
 * (the right column is narrow) instead of clipping or breaking mid-word. */
static const char *wmoRu(int wmo) {
  if (wmo == 0) return "ясно";
  if (wmo <= 2) return "малая облачность";
  if (wmo == 3) return "пасмурно";
  if (wmo >= 95) return "гроза";
  if (wmo >= 85) return "снег";
  if (wmo >= 80) return "ливень";
  if (wmo >= 71) return "снег";
  if (wmo >= 66) return "ледяной дождь";
  if (wmo == 65) return "ливень";
  if (wmo >= 61) return "дождь";
  if (wmo >= 51) return "морось";
  if (wmo >= 45) return "туман";
  return "облачно";
}

void drawWeather(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  WeatherData &w = ui.st.weather;
  if (!ui.st.weatherReceived) {
    g.setFont(&F_MED);
    textCenter(g, NOCT_W / 2, 80, "нет данных о погоде", DIM);
    return;
  }
  char v[32];

  /* header in FIXED columns so nothing drifts with the temperature's width:
   *   left: animated icon | big temperature + superscript degree
   *   right (fixed x): wrapped description
   * Temp is ink-anchored like the CPU/GPU hero tiles (inkH 64 -> ink y30..94). */
  weatherIcon(g, 28, 54, 18, w.wmoCode, ui.now);
  g.setFont(&F_HUGE);
  g.setTextSize(2);
  snprintf(v, sizeof(v), "%+d", w.temp);
  /* just the signed number (no degree symbol). Ink top ~y26: below the status
   * bar (y19) and above the forecast (y92). */
  textAt(g, 50, 26 - (g.fontHeight() - 64) / 2, v, TEXT);
  g.setTextSize(1);
  /* precipitation probability in the free top-right corner */
  g.setFont(&F_TEXT);
  snprintf(v, sizeof(v), "осадки %d%%", w.precip);
  textRight(g, NOCT_W - 6, 26, v, w.precip >= 50 ? INFO : DIM);
  /* description below the precip, in the right column */
  {
    const int dx = 168, bw = NOCT_W - dx - 6;
    g.setFont(&F_MED);
    String d = wmoRu(w.wmoCode);
    bool oneLine = g.textWidth(d.c_str()) <= bw;
    int y0 = oneLine ? 58 : 48;
    textWrap(g, d.c_str(), dx, y0, bw, 20, 2, ORANGE);
  }

  /* forecast — CENTRED so it isn't lopsided when fewer than 5 days arrive */
  static const char *dayNames[] = {"сег", "+1", "+2", "+3", "+4"};
  int nd = w.wfDays < 5 ? w.wfDays : 5;
  if (nd > 0) {
    int totalW = nd * 62 - 6; /* 56-wide tiles, 6 px gaps */
    int x0 = (NOCT_W - totalW) / 2;
    for (int i = 0; i < nd; i++) {
      int x = x0 + i * 62;
      panel(g, x, 92, 56, 72, dayNames[i]); /* day name in the panel tab */
      weatherIcon(g, x + 28, 116, 12, w.wfCode[i], ui.now);
      g.setFont(&F_MED);
      snprintf(v, sizeof(v), "%d", w.wfMax[i]);
      textCenter(g, x + 28, 132, v, WARN); /* hi */
      g.setFont(&F_TEXT);
      snprintf(v, sizeof(v), "%d", w.wfMin[i]);
      textCenter(g, x + 28, 152, v, INFO); /* lo, inside y164 */
    }
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

  /* 5h window gauge (grown to fill height) */
  panel(g, 4, 28, 200, 64, "ОКНО 5Ч");
  int win = c.windowPct < 0 ? 0 : c.windowPct;
  g.setFont(&F_BIG);
  snprintf(v, sizeof(v), c.windowPct < 0 ? "n/a" : "%d%%", win);
  textAt(g, 14, 38, v, pctColor(win));
  hBar(g, 96, 44, 98, 20, win, pctColor(win));
  g.setFont(&F_TEXT);
  if (c.resetsInMin >= 0) {
    snprintf(v, sizeof(v), "сброс через %dч %02dм", c.resetsInMin / 60,
             c.resetsInMin % 60);
    textAt(g, 14, 74, v, DIM);
  }

  /* weekly gauge */
  panel(g, 4, 100, 200, 64, "НЕДЕЛЯ");
  int wk = c.weeklyPct < 0 ? 0 : c.weeklyPct;
  g.setFont(&F_BIG);
  snprintf(v, sizeof(v), c.weeklyPct < 0 ? "n/a" : "%d%%", wk);
  textAt(g, 14, 110, v, pctColor(wk));
  hBar(g, 96, 116, 98, 20, wk, pctColor(wk));
  g.setFont(&F_TEXT);
  if (c.weeklyResetMin >= 0) {
    snprintf(v, sizeof(v), "сброс через %dд %dч", c.weeklyResetMin / 1440,
             (c.weeklyResetMin % 1440) / 60);
    textAt(g, 14, 146, v, DIM);
  }

  /* right column: plan + today numbers */
  panel(g, 212, 28, 104, 136, "СЕГОДНЯ");
  g.setFont(&F_MED);
  g.setTextSize(1);
  if (c.plan.length()) {
    String p = c.plan;
    p.toUpperCase();
    g.fillRoundRect(220, 36, g.textWidth(p.c_str()) + 12, 20, 3, ORANGE);
    textAt(g, 226, 38, p.c_str(), BG);
  }
  if (c.todayTokens >= 1000000)
    snprintf(v, sizeof(v), "%.1fM", c.todayTokens / 1e6);
  else
    snprintf(v, sizeof(v), "%ldK", c.todayTokens / 1000);
  textAt(g, 222, 60, v, TEXT);
  g.setFont(&F_TEXT);
  textAt(g, 222, 82, "токенов", DIM);
  g.setFont(&F_MED);
  snprintf(v, sizeof(v), "%d", c.todayMsgs);
  textAt(g, 222, 96, v, TEXT);
  g.setFont(&F_TEXT);
  textAt(g, 222, 118, "сообщений", DIM);
  if (c.stale) textAt(g, 222, 132, "устарело", WARN);
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
  /* fill the band y26..170 (144 px) across `rows`, capped for legibility */
  int chh = (144 - (rows - 1) * 4) / rows;
  if (chh > 122) chh = 122;
  for (int i = 0; i < shown; i++) {
    ForestNode &n = f.nodes[i];
    int x = 4 + (i % cols) * (cw + 4);
    int y = 26 + (i / cols) * (chh + 4);
    if (y + chh > 171) break; /* safety: never draw past the screen */
    panel(g, x, y, cw, chh);
    uint16_t c = stColor(n.status);
    bool down = strcmp(n.status, "down") == 0;
    if (!down || ((ui.now / 400) & 1)) g.fillCircle(x + 11, y + 11, 5, c);
    g.setFont(&F_MED);
    g.setTextSize(1);
    char nm[24];
    clipW(g, n.name, nm, sizeof(nm), cw - 28);
    textAt(g, x + 22, y + 4, nm, TEXT);
    if (down) {
      textAt(g, x + 22, y + 22, "OFFLINE", CRIT);
      g.setTextSize(1);
      continue;
    }
    g.setTextSize(1);
    struct {
      const char *l;
      int val;
    } bars[] = {{"CPU", n.cpu}, {"RAM", n.ram}, {"HDD", n.disk}};
    bool roomy = chh >= 50;
    for (int b = 0; b < 3; b++) {
      int bx = x + 8 + b * ((cw - 16) / 3);
      int val = bars[b].val < 0 ? 0 : bars[b].val;
      if (roomy) {
        g.setFont(&F_TEXT);
        snprintf(v, sizeof(v), "%s %d%%", bars[b].l, val);
        textAt(g, bx, y + 26, v, DIM);
        hBar(g, bx, y + 37, (cw - 24) / 3 - 6, 9, val, pctColor(val));
      } else {
        hBar(g, bx, y + 26, (cw - 24) / 3 - 6, 9, val, pctColor(val));
      }
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
  /* service list spread over the full height (rows of 23px, up to 6) */
  int shown = s.count > 6 ? 6 : s.count;
  int pitch = shown > 0 ? (142 / (shown < 6 ? 6 : shown)) : 23;
  if (pitch > 24) pitch = 24;
  for (int i = 0; i < shown; i++) {
    ServiceEntry &e = s.list[i];
    int y = 28 + i * pitch;
    uint16_t c = stColor(e.status);
    g.fillCircle(13, y + 8, 5, c);
    /* latency in a slim font, right-aligned — frees width for the name */
    int nameRight = 206;
    if (e.ms >= 0) {
      g.setFont(&F_TEXT);
      g.setTextSize(1);
      snprintf(v, sizeof(v), "%dms", e.ms);
      int mw = g.textWidth(v);
      textAt(g, 206 - mw, y + 5, v, e.ms > 500 ? WARN : DIM);
      nameRight = 206 - mw - 8;
    }
    /* name clipped by display width so multi-byte Cyrillic never cuts mid-glyph
     * (was "%.11s" → "Игровой сервер" became broken "Игров") */
    char nm[40];
    g.setFont(&F_MED);
    g.setTextSize(1);
    clipW(g, e.name, nm, sizeof(nm), nameRight - 26);
    textAt(g, 26, y, nm, TEXT);
    g.drawFastHLine(8, y + pitch - 2, 200, lerp565(BG, ORANGE_DIM, 120));
  }

  /* docker + totals on the right, grown to fill the height */
  panel(g, 216, 28, 100, 66, "DOCKER");
  g.setFont(&F_BIG);
  if (s.dockUp >= 0) {
    snprintf(v, sizeof(v), "%d/%d", s.dockUp, s.dockTotal);
    textCenter(g, 266, 44, v, s.dockUp == s.dockTotal ? GOOD : WARN);
  } else {
    textCenter(g, 266, 44, "n/a", DIM);
  }
  g.setFont(&F_TEXT);
  textCenter(g, 266, 80, "контейнеры", DIM);

  panel(g, 216, 100, 100, 64, "СЕРВИСЫ");
  g.setFont(&F_BIG);
  snprintf(v, sizeof(v), "%d/%d", s.up, s.count);
  textCenter(g, 266, 118, v, s.up == s.count ? GOOD : CRIT);
  g.setFont(&F_TEXT);
  textCenter(g, 266, 150, "онлайн", DIM);
}

/* ── EVENTS ──────────────────────────────────────────────────────────── */

void drawEvents(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  EventsData &e = ui.st.events;
  char v[32];

  if (e.count == 0) {
    g.setFont(&F_MED);
    g.setTextSize(1);
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
  g.setFont(&F_MED);
  g.setTextSize(1);
  textAt(g, 38, 32, e.top, sc);
  snprintf(v, sizeof(v), "%s  x%d", e.severity, e.count);
  textAt(g, 8, 64, v, DIM);

  /* human text — robust word+char wrap (long CVE ids etc.), up to 2 lines */
  g.setFont(&F_MED);
  textWrap(g, e.text, 8, 86, NOCT_W - 16, 21, 2, TEXT);

  /* other firing alerts fill the freed bottom band (up to 3 more lines) */
  if (uiOn(UI_STRIPS)) {
    g.setFont(&F_TEXT);
    int shown = 0;
    for (int i = 1; i < e.count && i < EventsData::kMaxList; i++) {
      if (!e.list[i][0]) continue;
      snprintf(v, sizeof(v), "+ %s", e.list[i]);
      textAt(g, 8, 134 + shown * 12, v, DIM);
      shown++;
    }
  }
}

/* ── HISTORY — last hour mini-graphs ─────────────────────────────────── */

static void hourGraph(LGFX_Sprite &g, int x, int y, int w, int h,
                      const char *title, const HourGraph &hg,
                      const char *unit, uint16_t color, int floorMax) {
  panel(g, x, y, w, h, title);
  /* current value, big, top-right */
  char v[12];
  snprintf(v, sizeof(v), "%d%s", hg.now(), unit);
  g.setFont(&F_VALUE);
  g.setTextSize(2);
  textRight(g, x + w - 6, y + 4, v, color);
  g.setTextSize(1);

  int gx = x + 4, gy = y + 22, gw = w - 8, gh = h - 26;
  g.drawRect(gx, gy, gw, gh, PANEL);
  if (hg.count < 2) {
    g.setFont(&F_TEXT);
    textCenter(g, x + w / 2, y + h / 2, "сбор данных...", DIM);
    return;
  }
  int mx = hg.maxVal(floorMax), mn = hg.minVal();
  if (mx <= mn) mx = mn + 1;
  /* min/max ticks */
  g.setFont(&F_TEXT);
  char t[8];
  snprintf(t, sizeof(t), "%d", mx);
  textAt(g, gx + 2, gy + 1, t, DIM);
  snprintf(t, sizeof(t), "%d", mn);
  textAt(g, gx + 2, gy + gh - 8, t, DIM);
  /* curve, filled under */
  int n = hg.count, px = -1, py = -1;
  for (int i = 0; i < n; i++) {
    int vx = gx + 1 + (gw - 3) * i / (n - 1);
    int vy = gy + gh - 2 - (gh - 4) * (hg.at(i) - mn) / (mx - mn);
    g.drawFastVLine(vx, vy, gy + gh - 2 - vy, lerp565(BG, color, 60));
    if (px >= 0) g.drawLine(px, py, vx, vy, color);
    px = vx;
    py = vy;
  }
  if (px >= 0) g.fillCircle(px, py, 2, TEXT);
}

void drawHistory(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  if (!ui.hist) return;
  const Histories &h = *ui.hist;
  int span = h.ct.count;
  /* graphs fill the freed band; footer label stays inside the screen */
  hourGraph(g, 4, 26, 154, 60, "CPU C", h.ct, "", INFO, 60);
  hourGraph(g, 162, 26, 154, 60, "GPU C", h.gt, "", GOOD, 60);
  hourGraph(g, 4, 90, 154, 58, "CPU %", h.cl, "", WARN, 100);
  hourGraph(g, 162, 90, 154, 58, "RAM %", h.ram, "", ACCENT, 100);
  g.setFont(&F_TEXT);
  char buf[40]; /* Cyrillic is 2 B/char: "история за 60 мин" is ~31 B */
  snprintf(buf, sizeof(buf), "история за %d мин", span < 60 ? span : 60);
  textCenter(g, NOCT_W / 2, 156, buf, DIM); /* y156..169 inside */
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
