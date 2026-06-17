/*
 * Nocturne C6 — shared UI widgets: status bar, sparklines, speech bubble,
 * value tiles, icon-ish primitives. All draw into the framebuffer sprite.
 */
#ifndef NOCT_WIDGETS_H
#define NOCT_WIDGETS_H

#include "core/Graphs.h"
#include "core/HourHistory.h"
#include "core/Types.h"
#include "net/ForzaManager.h"
#include "pet/PetBrain.h"
#include "pet/WolfPet.h"
#include "ui/Theme.h"

struct UiCtx {
  LGFX_Sprite &g;
  AppState &st;
  Graphs &gr;
  WolfPet &pet;
  PetBrain &brain;
  unsigned long now;
  const ForzaState *forza = nullptr;
  bool forzaLive = false; /* packets within the timeout window */
  const Histories *hist = nullptr; /* on-device hour history */
  const uint16_t *cover = nullptr; /* album cover RGB565 (CoverClient), or null */
};

namespace widgets {

/* Top status bar: scene title + wifi/tcp/sd/llm/clock + scene-position ticker
 * built into the separator line. */
void statusBar(UiCtx &ui, const char *title, int scene = -1,
               int sceneCount = 0);

/* Sparkline from a rolling graph (line + tip dot). */
void sparkline(LGFX_Sprite &g, int x, int y, int w, int h,
               const RollingGraph &gr, uint16_t color, int maxFloor = 100);

/* Big value tile: label, huge value + unit, accent color. */
void valueTile(LGFX_Sprite &g, int x, int y, int w, int h, const char *label,
               const char *value, const char *unit, uint16_t accent);

/* Labelled horizontal bar with value text on the right. */
void labelBar(LGFX_Sprite &g, int x, int y, int w, const char *label,
              int pct, const char *valueText, uint16_t color);

/* Wolf speech bubble with typewriter reveal / thinking dots. */
void speechBubble(UiCtx &ui, int x, int y, int w, int h);

/* XBM drawn scaled (chunky pixels). */
void xbmScaled(LGFX_Sprite &g, int x, int y, const unsigned char *bits,
               int w, int h, int scale, uint16_t color);

/* Small weather glyph by WMO code (centered in box). */
void weatherIcon(LGFX_Sprite &g, int cx, int cy, int r, int wmo,
                 unsigned long now);

/* kb/s humanizer: "1.2M" / "850K". */
void fmtRate(char *out, size_t cap, int kbs);

/* Tiny 8px trend caret from a rolling graph: up (rising, amber) / down
 * (falling, green) / flat (dim dash). Blinks red/cyan when the recent change
 * is a spike, so big jumps catch the eye. Draws within an 8x8 box at (x,y). */
void trendArrow(LGFX_Sprite &g, int x, int y, const RollingGraph &gr,
                int back = 8, int deadband = 2);

/* Little wolf paw print (pad + 4 toe beans) centred at (cx,cy), ~8x10. */
void pawPrint(LGFX_Sprite &g, int cx, int cy, uint16_t color);

/* Word-wrap text into a box at the current font, breaking overlong words by
 * characters (UTF-8 aware). Returns the next y after the last line drawn.
 * Stops at maxLines / box bottom. */
int textWrap(LGFX_Sprite &g, const char *s, int x, int y, int w, int lineH,
             int maxLines, uint16_t color);

} // namespace widgets

#endif
