/*
 * Nocturne C6 — АНАЛИЗ: what several windows say once they are allowed to
 * disagree.
 *
 *   y26..y72   the five pressure windows, side by side
 *   y78..y168  what the board made of them
 *
 * The point of the top strip is the SHAPE across it, not any single number.
 * Five columns falling steadily is a low settling in; five columns where the
 * short ones have turned while the long ones still point down is a trough
 * that has already passed. Neither reading exists on a screen that shows one
 * window, which is why they are drawn adjacent and to a common scale rather
 * than each in its own tile.
 *
 * A window with no history yet is drawn as a dash, never as zero. "Nothing
 * that old on the card" and "no change" are different answers and a shared
 * glyph for both would make the strip lie in the one direction that matters.
 */
#include <stdlib.h>

#include "core/ClimateAnalysis.h"
#include "core/config.h"
#include "ui/Scenes.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace scenes {

using namespace theme;
using namespace widgets;

/* One window column: label on top, signed change under it, and a bar whose
 * height carries the magnitude so the row reads as a shape before it reads as
 * numbers. */
static void drawWindow(LGFX_Sprite &g, int cx, const char *label, int d10,
                       bool ok, int maxAbs10) {
  /* y=35, not 30. panel() punches its title tab through the frame from
   * y-5 to y+6, so for a tile starting at 26 everything above 32 is the
   * title's. The window labels were landing inside it - "1 ч" sat on top of
   * "ДАВЛЕНИЕ ПО ОКНАМ" and read as a rendering fault. */
  g.setFont(&F_SMALL);
  textCenter(g, cx, 35, label, DIM);

  if (!ok) {
    /* An empty window is a fact worth its own word. A bare dash reads as a
     * drawing bug, and this screen shows one at 1 h most of the time: the
     * sensor speaks roughly hourly, so an hour-old sample often does not
     * exist. */
    g.setFont(&F_SMALL);
    textCenter(g, cx, 50, "нет", DIM);
    return;
  }
  char v[12];
  int whole = d10 / 10, frac = d10 % 10;
  if (frac < 0) frac = -frac;
  snprintf(v, sizeof(v), "%s%d.%d", d10 > 0 ? "+" : (d10 < 0 ? "-" : ""),
           abs(whole), frac);
  /* Falling is the direction worth noticing, so it wears the warn colour; a
   * rise is good news and stays neutral rather than shouting. */
  uint16_t c = d10 <= -16 ? WARN : (d10 >= 16 ? GOOD : TEXT);
  /* F_MED, not F_TEXT: this number IS the panel. It was drawn in the same
   * face as its own unit label, so the eye had nothing to land on and the
   * whole row read as small print. */
  g.setFont(&F_MED);
  textCenter(g, cx, 45, v, c);
  g.setFont(&F_TEXT);

  /* The bar grows from a common midline, so up and down are told apart by
   * direction rather than by reading the sign. */
  const int mid = 74, maxH = 8;
  if (maxAbs10 < 5) maxAbs10 = 5;
  int h = (int)((long)abs(d10) * maxH / maxAbs10);
  if (h > maxH) h = maxH;
  g.drawFastHLine(cx - 12, mid, 24, lerp565(PANEL, DIM, 110));
  if (h > 0) {
    if (d10 < 0) g.fillRect(cx - 5, mid + 1, 10, h, c);
    else g.fillRect(cx - 5, mid - h, 10, h, c);
  }
}

void drawAnalysis(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  const AppState &st = ui.st;
  char v[80];

  /* ── the windows ──────────────────────────────────────────────────────── */
  panel(g, 4, 26, 312, 54, "ДАВЛЕНИЕ ПО ОКНАМ, гПа");
  {
    const analysis::Windows &w = st.zbWin;
    struct Col {
      const char *label;
      int d10;
      bool ok;
    } cols[5] = {
        {"1 ч", w.dP10_1h, w.okP1},   {"3 ч", w.dP10_3h, w.okP3},
        {"6 ч", w.dP10_6h, w.okP6},   {"12 ч", w.dP10_12h, w.okP12},
        {"24 ч", w.dP10_24h, w.okP24},
    };
    /* One scale for all five, so the bars are comparable to each other. A
     * per-column scale would make a 0.3 hPa wobble look like a storm. */
    int maxAbs = 5;
    for (int i = 0; i < 5; i++)
      if (cols[i].ok && abs(cols[i].d10) > maxAbs) maxAbs = abs(cols[i].d10);
    for (int i = 0; i < 5; i++)
      drawWindow(g, 36 + i * 62, cols[i].label, cols[i].d10, cols[i].ok,
                 maxAbs);
  }

  /* ── the findings ─────────────────────────────────────────────────────── */
  panel(g, 4, 86, 312, 82, "ЧТО ЭТО ЗНАЧИТ");
  {
    if (st.zbFindCount <= 0) {
      g.setFont(&F_MED);
      textAt(g, 14, 100, "Ничего примечательного", DIM);
      g.setFont(&F_TEXT);
      textAt(g, 14, 124, "все окна в пределах спокойного хода", DIM);
      /* Even a quiet verdict should show its evidence, or the screen reads as
       * broken rather than as calm. */
      if (st.zbDewPoint10 != -9999) {
        snprintf(v, sizeof(v), "точка росы %d.%d C", st.zbDewPoint10 / 10,
                 abs(st.zbDewPoint10 % 10));
        textAt(g, 14, 142, v, DIM);
      }
      if (st.zbPressPct >= 0) {
        snprintf(v, sizeof(v), "по архиву: выше %d%% показаний", st.zbPressPct);
        textAt(g, 14, 156, v, DIM);
      }
      return;
    }

    /* Two findings fit legibly; a third would need a font nobody can read
     * across a room, and the rest are on the web panel anyway. */
    int shown = st.zbFindCount > 2 ? 2 : st.zbFindCount;
    int y = 96;
    for (int i = 0; i < shown; i++) {
      const analysis::Finding &f = st.zbFind[i];
      uint16_t c = f.severity >= 2 ? CRIT : (f.severity == 1 ? WARN : GOOD);
      g.fillRect(12, y + 3, 3, 26, c);
      g.setFont(&F_MED);
      textAt(g, 22, y, f.title, c);
      g.setFont(&F_TEXT);
      textWrap(g, f.detail, 22, y + 20, 286, 13, 1, DIM);
      y += 38;
    }
    if (st.zbFindCount > shown) {
      /* Top right, on the frame line beside the panel label - the bottom of
       * the tile belongs to the second finding's detail, and putting the
       * counter there landed it on top of the words. The frame break at the
       * top is the one piece of chrome with room to spare. */
      g.setFont(&F_SMALL);
      snprintf(v, sizeof(v), " еще %d ", st.zbFindCount - shown);
      int w = g.textWidth(v);
      g.fillRect(308 - w, 76, w, 9, BG);
      textRight(g, 308, 76, v, DIM);
    }
  }
}

} // namespace scenes
