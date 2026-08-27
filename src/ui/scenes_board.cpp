/*
 * Nocturne C6 — ПЛАТА C6: what the board knows about itself.
 *
 * Everything here was previously visible only in the serial log or buried in a
 * menu overlay, which means nobody ever saw it — the board watches a PC all day
 * and had no screen for its own vitals.
 *
 * Four tiles in a 2x2 over y26..y150, footer line at y156:
 *   die temperature, with the 32-second trend and the peak since boot
 *   heap, free against the lowest ever seen, with the largest block
 *   loop duty cycle and the frame rate actually achieved
 *   uptime, restart reason and counters
 *
 * Two deliberate choices about honesty:
 *  - heap shows MIN free, not just free. Free heap right now says nothing; the
 *    low-water mark is what decides whether the next TLS handshake fits.
 *  - "load" is the share of the frame period the render loop actually spent
 *    working. An Arduino sketch has no scheduler accounting to ask, so this is
 *    the only figure that can be measured rather than invented, and it is
 *    labelled as what it is: цикл, not CPU.
 */
#include "core/config.h"
#include "ui/Scenes.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace scenes {

using namespace theme;
using namespace widgets;

/* A small sparkline inside a tile: same shape as the ДОМ trends, minus the
 * labels, because a tile has no room for a second scale. */
static void miniTrend(LGFX_Sprite &g, int x, int y, int w, int h,
                      const RollingGraph &gr, int floorSpan, uint16_t ink) {
  if (gr.count < 2) return;
  int lo = gr.at(0), hi = gr.at(0);
  for (int i = 1; i < gr.count; i++) {
    int v = gr.at(i);
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  if (hi - lo < floorSpan) { /* a steady value should look steady */
    int mid = (hi + lo) / 2;
    lo = mid - floorSpan / 2;
    hi = mid + floorSpan / 2;
  }
  for (int i = 1; i < gr.count; i++) {
    int x0 = x + (i - 1) * w / (gr.count - 1);
    int x1 = x + i * w / (gr.count - 1);
    int y0 = y + h - (gr.at(i - 1) - lo) * h / (hi - lo);
    int y1 = y + h - (gr.at(i) - lo) * h / (hi - lo);
    g.drawLine(x0, y0, x1, y1, ink);
  }
}

/* A horizontal fill bar with its own colour ramp. */
static void bar(LGFX_Sprite &g, int x, int y, int w, int pct, uint16_t c) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  g.drawRect(x, y, w, 7, ORANGE_DIM);
  int fill = (w - 2) * pct / 100;
  if (fill < 1 && pct > 0) fill = 1;
  g.fillRect(x + 1, y + 1, fill, 5, c);
}

void drawBoard(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  const AppState &st = ui.st;
  char v[48];

  const int cw = 150, ch = 60;
  const int x0 = 6, x1 = 164, y0 = 26, y1 = 90;

  /* ── die temperature ─────────────────────────────────────────────────── */
  panel(g, x0, y0, cw, ch, "ТЕМПЕРАТУРА");
  {
    /* The same thresholds the backlight guard uses, so the screen and the
     * board agree about what "warm" means. */
    uint16_t c = st.boardTemp >= NOCT_BOARD_HOT_C    ? CRIT
                 : st.boardTemp >= NOCT_BOARD_WARM_C ? WARN
                                                     : GOOD;
    /* Placed from the font's OWN height rather than a guessed offset: F_BIG's
     * ink is taller than it looks, and every stacked label in the first draft
     * of this screen landed inside the digits above it. */
    g.setFont(&F_BIG);
    snprintf(v, sizeof(v), "%.0f", st.boardTemp);
    const int heroY = y0 + 14;
    textAt(g, x0 + 8, heroY, v, c);
    int wNum = g.textWidth(v);
    int below = heroY + g.fontHeight() - 4;
    g.setFont(&F_TEXT);
    textAt(g, x0 + 10 + wNum, heroY + 12, "C", DIM);
    g.setFont(&F_SMALL);
    snprintf(v, sizeof(v), "пик %.0f", st.boardTempMax);
    textAt(g, x0 + 8, below, v, DIM);
    miniTrend(g, x0 + 82, y0 + 18, 60, 26, ui.gr.boardTemp, 20, c);
  }

  /* ── heap ────────────────────────────────────────────────────────────── */
  panel(g, x1, y0, cw, ch, "ПАМЯТЬ");
  {
    /* Percentages against the free heap the board boots with. Not the chip's
     * 512 KB: most of that is gone to the framebuffer and the radio stacks
     * before the first frame, and a bar against a number never available
     * would flatter the reading. */
    const int kBaseKb = 170;
    int pct = st.heapFreeKb * 100 / kBaseKb;
    uint16_t c = st.heapMinKb < 25 ? CRIT : st.heapMinKb < 45 ? WARN : GOOD;
    g.setFont(&F_BIG);
    snprintf(v, sizeof(v), "%d", st.heapFreeKb);
    const int heroY = y0 + 14;
    textAt(g, x1 + 8, heroY, v, c);
    int wNum = g.textWidth(v);
    int below = heroY + g.fontHeight() - 4;
    g.setFont(&F_TEXT);
    textAt(g, x1 + 10 + wNum, heroY + 12, "КБ", DIM);
    /* The bar rides beside the number, not under it: stacked under a F_BIG
     * hero there is no room for both a bar and the line that explains it. */
    bar(g, x1 + 84, heroY + 14, cw - 92, pct, c);
    /* The low-water mark decides whether the next TLS handshake fits, so it
     * gets the line rather than the current free figure. */
    /* The comment above says this number decides whether the next handshake
     * fits — which makes it a reading, not a footnote, and it was set in the
     * smallest face on the device. F_VALUE puts it back above the threshold
     * an eye resolves from a metre. */
    g.setFont(&F_TEXT);
    textAt(g, x1 + 8, below + 4, "мин", DIM);
    g.setFont(&F_VALUE);
    snprintf(v, sizeof(v), "%d", st.heapMinKb);
    textAt(g, x1 + 32, below, v, st.heapMinKb < 45 ? WARN : TEXT);
    g.setFont(&F_TEXT);
    textAt(g, x1 + 66, below + 4, "блок", DIM);
    g.setFont(&F_VALUE);
    snprintf(v, sizeof(v), "%d", st.heapLargestKb);
    textAt(g, x1 + 96, below, v, DIM);
  }

  /* ── loop duty cycle ─────────────────────────────────────────────────── */
  panel(g, x0, y1, cw, ch, "ЦИКЛ");
  {
    uint16_t c = st.boardLoad >= 85 ? CRIT : st.boardLoad >= 60 ? WARN : GOOD;
    g.setFont(&F_BIG);
    snprintf(v, sizeof(v), "%d%%", st.boardLoad);
    const int heroY = y1 + 14;
    textAt(g, x0 + 8, heroY, v, c);
    int below = heroY + g.fontHeight() - 4;
    g.setFont(&F_SMALL);
    /* Frames actually rendered against the target: a duty cycle alone cannot
     * tell a busy loop from a stalled one. */
    snprintf(v, sizeof(v), "%d к/с из %d", st.boardFps, 1000 / NOCT_FRAME_MS);
    textAt(g, x0 + 8, below, v,
           st.boardFps < (1000 / NOCT_FRAME_MS) - 6 ? WARN : DIM);
    miniTrend(g, x0 + 82, y1 + 18, 60, 26, ui.gr.boardLoad, 10, c);
  }

  /* ── uptime and restarts ─────────────────────────────────────────────── */
  panel(g, x1, y1, cw, ch, "В РАБОТЕ");
  {
    unsigned long up = st.uptimeSec;
    /* F_BIG like the other three heroes of this 2x2 grid — it was the only
     * one in F_MED, and the same role in two sizes reads as a hierarchy that
     * is not there.
     *
     * But F_BIG is logisoso24_tr, a LATIN subset: "0ч 02м" set in it draws
     * two hollow boxes where the units belong. So the digits get F_BIG and
     * the Russian units get F_TEXT beside them, sharing one baseline. */
    unsigned long n1, n2;
    const char *u1, *u2;
    if (up >= 86400UL) {
      n1 = up / 86400UL;
      u1 = "д";
      n2 = (up % 86400UL) / 3600UL;
      u2 = "ч";
    } else {
      n1 = up / 3600UL;
      u1 = "ч";
      n2 = (up % 3600UL) / 60UL;
      u2 = "м";
    }
    g.setFont(&F_BIG);
    int uy = inkY(INK_BIG, y1 + 8, 30);
    int ub = uy + INK_BIG.top + INK_BIG.height - INK_TEXT.top - INK_TEXT.height;
    snprintf(v, sizeof(v), "%lu", n1);
    textAt(g, x1 + 8, uy, v, TEXT);
    int w1 = g.textWidth(v);
    g.setFont(&F_TEXT);
    textAt(g, x1 + 10 + w1, ub, u1, DIM);
    int wu = g.textWidth(u1);
    g.setFont(&F_BIG);
    snprintf(v, sizeof(v), "%02lu", n2);
    textAt(g, x1 + 16 + w1 + wu, uy, v, TEXT);
    int w2 = g.textWidth(v);
    g.setFont(&F_TEXT);
    textAt(g, x1 + 18 + w1 + wu + w2, ub, u2, DIM);
    g.setFont(&F_BIG);
    g.setFont(&F_TEXT);
    const BootInfo &b = st.boot;
    /* A self-heal on a device nobody is watching is invisible without this. */
    snprintf(v, sizeof(v), "рестарт: %s", b.reasonText);
    textAt(g, x1 + 8, y1 + 38, v, b.lastWasFault ? WARN : DIM);
    /* The file's own header says this screen exists so a silent self-heal
     * becomes a number. A number nobody can read from where the board sits
     * does not do that. */
    snprintf(v, sizeof(v), "пусков %lu, сбоев %lu", (unsigned long)b.bootCount,
             (unsigned long)b.faultCount);
    textAt(g, x1 + 8, y1 + 50, v, b.faultCount > 0 ? WARN : DIM);
  }

  /* ── footer: the rest of the identity, one line ──────────────────────── */
  g.setFont(&F_SMALL);
  g.drawFastHLine(8, 152, NOCT_W - 16, ORANGE_DIM);
  snprintf(v, sizeof(v), "v%s   %d МГц   wifi %d dBm", NOCT_VERSION, st.cpuMhz,
           st.link.rssi);
  textAt(g, 8, 158, v, DIM);
  snprintf(v, sizeof(v), "SD %s   zigbee %s", st.link.sdOk ? "есть" : "нет",
           st.link.zbUp ? "кан.25" : "выкл");
  textRight(g, NOCT_W - 8, 158, v, DIM);
}

} // namespace scenes
