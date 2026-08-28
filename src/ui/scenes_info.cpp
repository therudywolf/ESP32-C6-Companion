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

  char et[12], tt[12];
  unsigned ps = (unsigned)(pos < 0 ? 0 : pos), ds = (unsigned)(dur > 0 ? dur : 0);
  snprintf(et, sizeof(et), "%u:%02u", ps / 60, ps % 60);
  if (dur > 0) snprintf(tt, sizeof(tt), "%u:%02u", ds / 60, ds % 60);
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
      lintRect(LK_FRAME, cx, cy + shown, cw, 1, "линейка");
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
    /* Opened at the cursor and as tall as the line: at 48 with 20 rows it
     * ended four rows early and shaved the descenders off, so Cyrillic "р",
     * "у" and "д" lost their tails and read as different letters. */
    g.setClipRect(rx, 52, rw, INK_MED.box);
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
  lintRect(LK_FRAME, 118, 86, 84, 1, "линейка");
  g.drawFastHLine(118, 86, 84, DIM);
  lintRect(LK_FRAME, 118, 88, 84, 1, "линейка");
  g.drawFastHLine(118, 88, 84, ORANGE_DIM);
  /* window */
  g.drawRect(135, 56, 50, 18, DIM);

  /* status LED + text */
  const char *stTxt = playing            ? "PLAYING"
                      : m.isIdle         ? "IDLE"
                                         : "PAUSED";
  uint16_t stc = playing ? GOOD : WARN;
  /* In the card's LABEL band, above the cassette, not on its corner. The
   * ink used to end on row 40 and the cassette's own outline starts there —
   * "Паузед налазит на кассету", exactly one row of it. */
  if (playing && ((ui.now / 500) & 1)) g.fillCircle(44, 33, 3, stc);
  g.setFont(&F_TEXT);
  textAt(g, 52, 29, stTxt, stc);

  /* track (seamless scrolling marquee) + artist */
  g.setFont(&F_MED);
  g.setTextSize(1);
  String t = m.track.length() ? m.track : String("--- нет трека ---");
  int tw = g.textWidth(t.c_str());
  if (tw > NOCT_W - 16) {
    int span = tw + 50; /* gap between loop copies */
    int off = (int)((ui.now / 35) % span);
    /* The window has to contain the LINE, not a guess at it. F_MED writes 20
     * rows from the cursor, so an 18-row window opened ten rows above the
     * cursor showed the top five rows of the capitals and nothing else — a
     * long title scrolled past as a band of letter-tops. */
    g.setClipRect(4, 118, NOCT_W - 8, INK_MED.box);
    textAt(g, 8 - off, 118, t.c_str(), TEXT);
    textAt(g, 8 - off + span, 118, t.c_str(), TEXT); /* second copy = seamless */
    g.clearClipRect();
  } else {
    textCenter(g, NOCT_W / 2, 118, t.c_str(), TEXT);
  }
  /* clipW, not substring(0, 24): the argument is a BYTE count and every
   * Cyrillic letter costs two, so a Russian name lost half of what a Latin
   * one kept — and a cut landing mid-codepoint left a broken byte the font
   * drew as a box. clipW measures rendered width and cuts on codepoints. */
  char artist[64];
  clipW(g, m.artist.c_str(), artist, sizeof(artist), NOCT_W - 8);
  textCenter(g, NOCT_W / 2, 138, artist, ORANGE);

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

  /* The hero gets a CARD, like every other screen's hero.
   *
   * It used to float on the background while the day tiles below it were
   * cards — one screen in two idioms, which is what "всё в целом как-то не
   * верно скомпоновано" is describing. The card also fixes the padding the
   * owner could not place: the tiles now run to the last usable row instead
   * of stopping eight pixels short of it for no reason. */
  {
    Rect c = panelM(g, 4, 26, 312, 78, "за окном");
    weatherIcon(g, c.x + 22, c.y + 32, 17, w.wmoCode, ui.now);
    g.setFont(&F_HUGE);
    g.setTextSize(2);
    snprintf(v, sizeof(v), "%+d", w.temp);
    /* Ink-anchored: F_HUGE at double size writes a 94 px line for 64 px of
     * digits, so a cursor placed by eye puts a third of the glyph outside
     * whatever it was aimed at. */
    const int ty = inkY(INK_HUGE, c.y, c.h, 2);
    textAt(g, c.x + 44, ty, v, TEXT);
    const int heroEnd = c.x + 44 + g.textWidth(v) + 10;
    g.setTextSize(1);

    /* The right column starts where the hero ENDS, measured: a two-digit
     * sub-zero reading is wider than "+5", and a fixed column collided. */
    const int dx = heroEnd > 176 ? heroEnd : 176;
    const int bw = c.x + c.w - dx;
    g.setFont(&F_TEXT);
    snprintf(v, sizeof(v), "осадки %d%%", w.precip);
    textAt(g, dx, c.y + 2, v, w.precip >= 50 ? INFO : DIM);
    g.setFont(&F_MED);
    String d = wmoRu(w.wmoCode);
    textWrap(g, d.c_str(), dx, c.y + 20, bw, 19, 2, ORANGE);
  }

  /* Forecast, CENTRED so it isn't lopsided when fewer than 5 days arrive.
   * Indoor sensors, when a coordinator is feeding them, take the last tile in
   * the same row: "за окном" and "дома" belong side by side, and reusing the
   * forecast tile means no new layout and no scene-id surgery (adding a scene
   * would shift FORZA's index and desync the web panel). */
  static const char *dayNames[] = {"сег", "+1", "+2", "+3", "+4"};
  int indoor = ui.st.zb.count > 0 ? 1 : 0;
  int nd = w.wfDays < 5 ? w.wfDays : 5;
  if (indoor && nd > 4) nd = 4; /* give the last slot to the room */
  if (nd > 0 || indoor) {
    int tiles = nd + indoor;
    int totalW = tiles * 62 - 6; /* 56-wide tiles, 6 px gaps */
    int x0 = (NOCT_W - totalW) / 2;
    for (int i = 0; i < nd; i++) {
      int x = x0 + i * 62;
      panel(g, x, 108, 56, 62, dayNames[i]);
      weatherIcon(g, x + 28, 132, 11, w.wfCode[i], ui.now);
      g.setFont(&F_MED);
      snprintf(v, sizeof(v), "%d", w.wfMax[i]);
      textCenter(g, x + 28, 144, v, WARN); /* hi */
      g.setFont(&F_TEXT);
      snprintf(v, sizeof(v), "%d", w.wfMin[i]);
      textCenter(g, x + 28, 160, v, INFO); /* lo, чернила 162..168 */
    }

    if (indoor) {
      const ZbSensor &z = ui.st.zb.list[0];
      int x = x0 + nd * 62;
      /* A battery sensor that has gone quiet still has a last reading, and
       * showing it as if it were current is the same lie the "no signal"
       * handling exists to prevent. Over an hour old -> everything dims. */
      bool stale = z.ageSec >= 0 && z.ageSec > 3600;
      char tab[18];
      /* The tile is 56 px wide; measure the label in the font it will actually
       * be drawn in, or "ForestHome" silently becomes "ForestHom" — which
       * reads as a typo rather than as a truncation. */
      g.setFont(&F_TEXT);
      clipW(g, z.name[0] ? z.name : "дома", tab, sizeof(tab), 48);
      panel(g, x, 108, 56, 62, tab, stale ? DIM : ORANGE_DIM,
            stale ? DIM : ORANGE);
      g.setFont(&F_MED);
      if (z.temp10 != -32768) {
        snprintf(v, sizeof(v), "%d", (z.temp10 + (z.temp10 < 0 ? -5 : 5)) / 10);
        textCenter(g, x + 28, 122, v, stale ? DIM : WARN);
      } else {
        textCenter(g, x + 28, 122, "--", DIM);
      }
      g.setFont(&F_TEXT);
      if (z.humidity >= 0) {
        snprintf(v, sizeof(v), "%d%%", z.humidity);
        textCenter(g, x + 28, 142, v, stale ? DIM : INFO);
      }
      /* battery as a tiny bar: the number matters far less than "is it dying" */
      if (z.battery >= 0) {
        int bw2 = 34, bx = x + 11, by = 158;
        g.drawRect(bx, by, bw2, 7, stale ? DIM : ORANGE_DIM);
        g.fillRect(bx + bw2, by + 2, 2, 3, stale ? DIM : ORANGE_DIM);
        int fill = (bw2 - 2) * z.battery / 100;
        if (fill > 0)
          g.fillRect(bx + 1, by + 1, fill, 5,
                     z.battery < 20 ? CRIT : (stale ? DIM : GOOD));
      } else if (stale) {
        textCenter(g, x + 28, 156, "молчит", DIM);
      }
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
  panel(g, 4, 28, 200, 64, "окно 5ч");
  int win = c.windowPct < 0 ? 0 : c.windowPct;
  g.setFont(&F_BIG);
  snprintf(v, sizeof(v), c.windowPct < 0 ? "n/a" : "%d%%", win);
  /* "n/a" in DIM, not pctColor(0) — a green "n/a" reads as a healthy 0%. */
  textAt(g, 14, 38, v, c.windowPct < 0 ? DIM : pctColor(win));
  hBar(g, 96, 44, 98, 20, win, pctColor(win));
  g.setFont(&F_TEXT);
  if (c.resetsInMin >= 0) {
    snprintf(v, sizeof(v), "сброс через %dч %02dм", c.resetsInMin / 60,
             c.resetsInMin % 60);
    textAt(g, 14, 74, v, DIM);
  }

  /* weekly gauge */
  panel(g, 4, 100, 200, 64, "неделя");
  int wk = c.weeklyPct < 0 ? 0 : c.weeklyPct;
  g.setFont(&F_BIG);
  snprintf(v, sizeof(v), c.weeklyPct < 0 ? "n/a" : "%d%%", wk);
  textAt(g, 14, 110, v, c.weeklyPct < 0 ? DIM : pctColor(wk));
  hBar(g, 96, 116, 98, 20, wk, pctColor(wk));
  g.setFont(&F_TEXT);
  if (c.weeklyResetMin >= 0) {
    snprintf(v, sizeof(v), "сброс через %dд %dч", c.weeklyResetMin / 1440,
             (c.weeklyResetMin % 1440) / 60);
    textAt(g, 14, 146, v, DIM);
  }

  /* right column: plan + today numbers */
  panel(g, 212, 28, 104, 136, "сегодня");
  /* Content starts BELOW the label band. The plan badge is a filled rounded
   * rect, so drawing it at the card's top did not merely overlap the word
   * "сегодня" — it erased it. */
  g.setFont(&F_MED);
  g.setTextSize(1);
  if (c.plan.length()) {
    String p = c.plan;
    p.toUpperCase();
    /* The plan may carry the rate-limit multiplier ("max 5x" / "max 20x") — a
     * bare "MAX" cannot tell a 5x from a 20x, and those are very different
     * ceilings. Uppercasing the whole string turns it into "MAX 5X", so put the
     * multiplier's x back down where it reads as a multiplier. */
    for (unsigned int i = 1; i < p.length(); i++)
      if (p[i] == 'X' && p[i - 1] >= '0' && p[i - 1] <= '9') p.setCharAt(i, 'x');
    g.fillRoundRect(220, 42, g.textWidth(p.c_str()) + 12, 20, 3, ORANGE);
    textAt(g, 226, 44, p.c_str(), BG);
  }
  if (c.todayTokens >= 1000000)
    snprintf(v, sizeof(v), "%.1fM", c.todayTokens / 1e6);
  else
    snprintf(v, sizeof(v), "%ldK", c.todayTokens / 1000);
  textAt(g, 222, 66, v, TEXT);
  g.setFont(&F_TEXT);
  textAt(g, 222, 86, "токенов", DIM);
  g.setFont(&F_MED);
  snprintf(v, sizeof(v), "%d", c.todayMsgs);
  textAt(g, 222, 98, v, TEXT);
  g.setFont(&F_TEXT);
  textAt(g, 222, 118, "сообщений", DIM);
  /* The third of today's three numbers. The card had thirty empty rows under
   * the second one — "снизу пусто, некрасиво" — and the tool count was
   * already in the payload, arriving and going nowhere. */
  g.setFont(&F_MED);
  snprintf(v, sizeof(v), "%d", c.todayTools);
  textAt(g, 222, 130, v, TEXT);
  g.setFont(&F_TEXT);
  textAt(g, 222, 150, "вызовов", DIM);
  /* Same grammar as the status bar: a pip, not a word. */
  if (c.stale) g.fillCircle(308, 34, 3, WARN);
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
  /* Fill the band 26..170 across `rows`, and no cap.
   *
   * The cap at 122 was written when this screen showed two nodes: with three
   * it left each card 48 px and the band 22 px short. It also meant a single
   * OFFLINE node drew a 122 px card with one word in it — the owner's "снизу
   * пусто" on a screen where the emptiness was structural. */
  int chh = (144 - (rows - 1) * 4) / rows;
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
      /* -1 means "the producer could not measure this" (SCHEMA.md), NOT zero.
       * Printing it as "0%" claimed a healthy reading for a metric we never got
       * — which is exactly how a missing Prometheus series read as a calm green
       * "RAM 0%". Show a dash instead; the bar stays an empty outline (hBar with
       * 0 draws no fill), so unknown never masquerades as measured. */
      bool unknown = bars[b].val < 0;
      int val = unknown ? 0 : bars[b].val;
      if (roomy) {
        /* F_VALUE, not F_TEXT. This is the reading the screen exists for,
         * and eleven-pixel type subtends 3.9 arc-minutes at the metre this
         * board is read from — under the 5' an eye resolves at all. The same
         * percentage-with-a-bar is set in F_BIG on CLAUDE, so it was also
         * three and a half times smaller than its own twin one screen over.
         *
         * The label stays small: it is a label, and it never changes. */
        g.setFont(&F_TEXT);
        textAt(g, bx, y + 22, bars[b].l, DIM);
        g.setFont(&F_VALUE);
        if (unknown)
          snprintf(v, sizeof(v), "--");
        else
          snprintf(v, sizeof(v), "%d%%", val);
        textAt(g, bx + 26, y + 20, v, unknown ? DIM : TEXT);
        hBar(g, bx, y + 38, (cw - 24) / 3 - 6, 9, val, pctColor(val));
      } else {
        hBar(g, bx, y + 26, (cw - 24) / 3 - 6, 9, val, pctColor(val));
        /* no room for a label here (4+ nodes), so mark "unknown" with a dash
         * across the empty bar — otherwise it is identical to a real 0%. */
        if (unknown) {
          int bw = (cw - 24) / 3 - 6;
          lintRect(LK_FRAME, bx + bw / 2 - 2, y + 30, 5, 1, "линейка");
  g.drawFastHLine(bx + bw / 2 - 2, y + 30, 5, DIM);
        }
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
  /* The container card is gone — "Докер... тут не нужен" — and the list
   * takes the 100 px it was using. Seven services fit at a legible pitch
   * where six were crowded into two thirds of the width. */
  Rect lc = panelM(g, 4, 26, 312, 144, "сервисы");
  int shown = s.count > 7 ? 7 : s.count;
  int pitch = 19;
  for (int i = 0; i < shown; i++) {
    ServiceEntry &e = s.list[i];
    int y = lc.y + 2 + i * pitch;
    uint16_t c = stColor(e.status);
    g.fillCircle(lc.x + 5, y + 8, 5, c);
    int nameRight = lc.x + lc.w - 8;
    if (e.ms >= 0) {
      /* F_VALUE: the latency is the only NUMBER on this screen, and it was
       * set two steps smaller than the service name beside it — the name is
       * the label and the number is the reading, so the sizes were the wrong
       * way round. */
      g.setFont(&F_VALUE);
      g.setTextSize(1);
      snprintf(v, sizeof(v), "%dms", e.ms);
      int mw = g.textWidth(v);
      textAt(g, lc.x + lc.w - 8 - mw, inkY(INK_VALUE, y, 18), v,
             e.ms > 500 ? WARN : TEXT);
      nameRight = lc.x + lc.w - 16 - mw;
    }
    /* name clipped by display width so multi-byte Cyrillic never cuts mid-glyph
     * (was "%.11s" → "Игровой сервер" became broken "Игров") */
    char nm[40];
    g.setFont(&F_MED);
    g.setTextSize(1);
    clipW(g, e.name, nm, sizeof(nm), nameRight - lc.x - 20);
    textAt(g, lc.x + 16, y, nm, TEXT);
  }
  /* The tally where the label is, so it costs no row of its own. */
  g.setFont(&F_TEXT);
  g.setTextSize(1);
  snprintf(v, sizeof(v), "%d из %d на связи", s.up, s.count);
  textRight(g, lc.x + lc.w, 28, v, s.up == s.count ? GOOD : WARN);
}

/* ── EVENTS ──────────────────────────────────────────────────────────── */

void drawEvents(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  EventsData &e = ui.st.events;
  char v[32];

  if (e.count == 0) {
    /* Quiet is a state, not an absence. The screen used to answer it with two
     * centred lines and two thirds of nothing — "пустовато тут" — so it now
     * says what quiet is being claimed ON: which watcher is reporting, and
     * whether the link that carries it is up. A green screen from a dead feed
     * looks exactly like a green screen from a healthy one, and this is the
     * difference. */
    Rect c = panelM(g, 4, 26, 312, 60, "алерты");
    g.setFont(&F_BIG);
    g.setTextSize(1);
    textAt(g, c.x, inkY(INK_BIG, c.y, c.h), "тихо в лесу", GOOD);

    Rect d = panelM(g, 4, 90, 312, 80, "кто смотрит");
    g.setFont(&F_MED);
    g.setTextSize(1);
    textAt(g, d.x, d.y + 2, "Alertmanager", TEXT);
    textRight(g, d.x + d.w, d.y + 2,
              ui.st.link.tcpConnected ? "на связи" : "нет связи",
              ui.st.link.tcpConnected ? GOOD : CRIT);
    g.setFont(&F_TEXT);
    textAt(g, d.x, d.y + 24,
           "активных срабатываний нет — ни warning, ни critical", DIM);
    if (ui.st.claude.available && ui.st.claude.weeklyPct >= 0) {
      char q[40];
      snprintf(q, sizeof(q), "лимит Claude: %d%% за неделю",
               ui.st.claude.weeklyPct);
      textAt(g, d.x, d.y + 40, q, DIM);
    }
    return;
  }

  bool warnSev = strcmp(e.severity, "warning") == 0;
  uint16_t sc = warnSev ? WARN : CRIT;

  /* The alert itself, in its own card, with the severity and the count on the
   * same line as the title rather than on a line of their own. */
  {
    Rect c = panelM(g, 4, 26, 312, 56, "срабатывание");
    if ((ui.now / 400) & 1)
      g.fillTriangle(c.x, c.y + 18, c.x + 12, c.y + 18, c.x + 6, c.y + 2, sc);
    g.setFont(&F_MED);
    g.setTextSize(1);
    char nm[40];
    clipW(g, e.top, nm, sizeof(nm), c.w - 90);
    textAt(g, c.x + 18, c.y + 2, nm, sc);
    g.setFont(&F_TEXT);
    snprintf(v, sizeof(v), "%s x%d", e.severity, e.count);
    textRight(g, c.x + c.w, c.y + 6, v, DIM);
    /* The human sentence, at F_MED. It was the whole point of the screen and
     * it was set in the same face as the timestamps beside it. */
    g.setFont(&F_MED);
    textWrap(g, e.text, c.x, c.y + 22, c.w, 18, 1, TEXT);
  }

  /* Everything else that is firing. */
  {
    Rect c2 = panelM(g, 4, 86, 312, 84,
                     e.count > 1 ? "ещё горит" : "подробности");
    g.setFont(&F_MED);
    g.setTextSize(1);
    textWrap(g, e.text, c2.x, c2.y + 2, c2.w, 18, 2, DIM);
    int shown = 0;
    g.setFont(&F_TEXT);
    for (int i = 1; i < e.count && i < EventsData::kMaxList; i++) {
      if (!e.list[i][0]) continue;
      snprintf(v, sizeof(v), "+ %s", e.list[i]);
      textAt(g, c2.x, c2.y + 40 + shown * 14, v, DIM);
      shown++;
      if (shown >= 2) break;
    }
  }
}

/* ── HISTORY — last hour mini-graphs ─────────────────────────────────── */

static void hourGraph(LGFX_Sprite &g, int x, int y, int w, int h,
                      const char *title, const HourGraph &hg,
                      const char *unit, uint16_t color, int floorMax) {
  panel(g, x, y, w, h, title);
  /* Current value, big, top-right. F_VALUE at double size is a 30 px line for
   * 22 px of ink, so placing the cursor at y+4 and reserving 22 rows put its
   * lower third inside the graph frame below — on all four tiles at once. */
  char v[12];
  snprintf(v, sizeof(v), "%d%s", hg.now(), unit);
  g.setFont(&F_VALUE);
  g.setTextSize(2);
  const int headH = 26;
  textRight(g, x + w - 6, inkY(INK_VALUE, y + 2, headH, 2), v, color);
  g.setTextSize(1);

  int gx = x + 4, gy = y + headH + 2, gw = w - 8, gh = h - headH - 6;
  g.drawRect(gx, gy, gw, gh, PANEL);
  if (hg.count < 2) {
    g.setFont(&F_TEXT);
    textCenter(g, x + w / 2, y + h / 2, "сбор данных...", DIM);
    return;
  }
  int mx = hg.maxVal(floorMax), mn = hg.minVal();
  if (mx <= mn) mx = mn + 1;
  /* Curve first, ticks after.
   *
   * The ticks used to be drawn first and the filled area painted straight
   * over them — the bottom one was legible only where the curve happened to
   * be low. Order is the whole fix; nothing moves. */
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

  /* min/max ticks, inside the frame. The lower one sat at gy+gh-8 with an
   * 11 px line, so its last rows fell outside the graph field entirely. */
  g.setFont(&F_TEXT);
  char t[12];
  snprintf(t, sizeof(t), "%d", mx);
  textAt(g, gx + 2, gy + 1, t, DIM);
  snprintf(t, sizeof(t), "%d", mn);
  textAt(g, gx + 2, gy + gh - 1 - INK_TEXT.box, t, DIM);
}

void drawHistory(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  if (!ui.hist) return;
  /* LONG press on this scene swaps the scale (see SceneManager::handleInput).
   * Mode 2 is not in RAM at all — it is the card's daily rows, loaded once by
   * main when the view is opened. */
  const GraphSet &s = (ui.histMode == 2 && ui.archive) ? *ui.archive
                      : ui.histMode == 1               ? ui.hist->day
                                                       : ui.hist->hour;
  int span = s.ct.count;
  /* graphs fill the freed band; footer label stays inside the screen */
  bool arc = (ui.histMode == 2);
  hourGraph(g, 4, 26, 154, 60, arc ? "CPU C /дн" : "CPU C", s.ct, "", INFO, 60);
  hourGraph(g, 162, 26, 154, 60, arc ? "GPU C /дн" : "GPU C", s.gt, "", GOOD, 60);
  hourGraph(g, 4, 90, 154, 58, "CPU %", s.cl, "", WARN, 100);
  hourGraph(g, 162, 90, 154, 58, "RAM %", s.ram, "", ACCENT, 100);
  g.setFont(&F_TEXT);
  char buf[80]; /* 2 B/char: the longer footer below is 53 B of UTF-8 */
  if (ui.histMode == 2)
    snprintf(buf, sizeof(buf), "архив с карты: %d дн (долго - час)", span);
  else if (ui.histMode == 1)
    snprintf(buf, sizeof(buf), "история за %d ч (долго - архив)",
             span < 24 ? span : 24);
  else
    snprintf(buf, sizeof(buf), "история за %d мин (долго - сутки)",
             span < 60 ? span : 60);
  textCenter(g, NOCT_W / 2, 156, buf, DIM); /* y156..169 inside */
}

/* ── ACHIEVEMENTS (overlay from menu) ────────────────────────────────── */

/* Ten counters in two columns, each with how far it is to the next milestone.
 * The point is not gamification — it is that a month of living with the thing
 * leaves a trace you can look at. */
void drawAchievements(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  if (!ui.ach) return;
  panel(g, 8, 26, 304, 122, "достижения");
  g.setFont(&F_TEXT);
  g.setTextSize(1);
  const int rows = Achievements::ACH_COUNT / 2; /* 5 rows, 2 columns */
  for (int i = 0; i < Achievements::ACH_COUNT; i++) {
    Achievements::Id id = (Achievements::Id)i;
    int col = i / rows, r = i % rows;
    int x = 16 + col * 150, y = 36 + r * 21;
    uint32_t have = ui.ach->get(id);
    uint32_t next = Achievements::nextTier(id, have);
    int lvl = Achievements::level(id, have);

    /* level pips: filled for each milestone passed */
    for (int p = 0; p < 4; p++) {
      uint16_t c = p < lvl ? ORANGE : PANEL;
      g.fillRect(x + 118 + p * 6, y + 1, 4, 4, c);
    }
    char lbl[40];
    snprintf(lbl, sizeof(lbl), "%s", Achievements::name(id));
    textAt(g, x, y, lbl, lvl > 0 ? TEXT : DIM);
    char val[24];
    if (next)
      snprintf(val, sizeof(val), "%lu/%lu", (unsigned long)have,
               (unsigned long)next);
    else
      snprintf(val, sizeof(val), "%lu MAX", (unsigned long)have);
    textRight(g, x + 112, y, val, next ? (lvl > 0 ? ORANGE : DIM) : GOOD);
    /* progress toward the next milestone */
    if (next) {
      uint32_t prev = 0;
      for (int p = 0; p < lvl; p++) prev = Achievements::nextTier(id, prev);
      int span = (int)(next - prev), got = (int)(have - prev);
      int w = span > 0 ? 112 * got / span : 0;
      if (w > 0) lintRect(LK_FRAME, x, y + 13, w, 1, "линейка");
  g.drawFastHLine(x, y + 13, w, ORANGE_DIM);
    }
  }
  g.setFont(&F_TEXT);
  char foot[64];
  snprintf(foot, sizeof(foot), "%s, %lu дн - любая кнопка закрывает",
           ui.pet.stageName(), (unsigned long)ui.pet.ageDays());
  textCenter(g, NOCT_W / 2, 152, foot, DIM);
}

/* ── SYSINFO (overlay from menu) ─────────────────────────────────────── */

void drawSysInfo(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  char v[64];
  panel(g, 8, 26, 304, 122, "система");
  g.setFont(&F_TEXT);
  const int rowH = 14; /* 8 rows have to fit the 122 px panel */
  int y = 34;
  snprintf(v, sizeof(v), "Nocturne C6 v%s", NOCT_VERSION);
  textAt(g, 18, y, v, ORANGE);
  y += rowH;
  snprintf(v, sizeof(v), "heap: %u KB (min %u)   плата: %.0fC (пик %.0f)",
           (unsigned)(ESP.getFreeHeap() / 1024),
           (unsigned)(ESP.getMinFreeHeap() / 1024), ui.st.boardTemp,
           ui.st.boardTempMax);
  /* the board's own temperature turns amber once the backlight guard engages */
  textAt(g, 18, y, v,
         ui.st.boardTemp >= NOCT_BOARD_WARM_C ? WARN : TEXT);
  y += rowH;
  unsigned long up = ui.now / 1000;
  snprintf(v, sizeof(v), "uptime: %luч %02luм %02luс", up / 3600,
           (up % 3600) / 60, up % 60);
  textAt(g, 18, y, v, TEXT);
  y += rowH;
  /* Why we last restarted. The watchdog panics and reboots on a wedged render
   * loop, so without this line a self-heal — or a reboot loop — is invisible. */
  {
    const BootInfo &b = ui.st.boot;
    snprintf(v, sizeof(v), "рестарт: %s  (пусков %lu, сбоев %lu)", b.reasonText,
             (unsigned long)b.bootCount, (unsigned long)b.faultCount);
    textAt(g, 18, y, v, b.faultCount > 0 ? WARN : TEXT);
  }
  y += rowH;
  snprintf(v, sizeof(v), "WiFi: %s  %d dBm", ui.st.link.ssid,
           ui.st.link.rssi);
  textAt(g, 18, y, v, TEXT);
  y += rowH;
  snprintf(v, sizeof(v), "IP: %s   zigbee: %d", WiFi.localIP().toString().c_str(),
           ui.st.zb.count);
  textAt(g, 18, y, v, TEXT);
  y += rowH;
  snprintf(v, sizeof(v), "SD: %s   LLM: %s", ui.st.link.sdOk ? "GOOD" : "-",
           ui.st.link.llmOk ? "GOOD" : "-");
  textAt(g, 18, y, v, TEXT);
  y += rowH;
  snprintf(v, sizeof(v), "возраст волка: %lu дней",
           (unsigned long)ui.pet.ageDays());
  textAt(g, 18, y, v, DIM);
}

} // namespace scenes
