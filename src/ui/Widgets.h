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
#include "pet/Achievements.h"
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
  /* ИСТОРИЯ scale: 0 = last hour, 1 = last 24 h, 2 = the SD archive by day. */
  int histMode = 0;
  const GraphSet *archive = nullptr; /* daily series, only in mode 2 */
  int archiveDays = 0;
  const Achievements *ach = nullptr;
  /* ДОМ trend window: 0 = the live 32 reports in RAM, 1 = today from the card,
   * 2 = the week. `climate` is only non-null for 1 and 2. */
  int homeMode = 0;
  const struct ClimateSeriesView *climate = nullptr;
  /* ОСМОТР: hold every moving thing still so two captures of one screen can
   * be diffed pixel for pixel. Reaches the scenes, not just the manager,
   * because the animation that actually breaks a diff is the wolf blinking. */
  bool review = false;
};

/* A loaded climate series, flattened so the UI does not include the storage
 * layer. INT_MIN / -1 mark buckets no reading fell into: a gap in the record is
 * a fact, and drawing through it would invent readings the sensor never sent. */
struct ClimateSeriesView {
  const int *temp10;
  const int *hum;
  int cols;
  int filled;
  int rows;
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
/* Same input, but read as MB/s. A disk that does 400 000 KB/s prints five
 * digits nobody parses as a speed; 390 does. */
void fmtRateMb(char *out, size_t cap, int kbs);

/* Tiny 8px trend caret from a rolling graph: up (rising, amber) / down
 * (falling, green) / flat (dim dash). Blinks red/cyan when the recent change
 * is a spike, so big jumps catch the eye. Draws within an 8x8 box at (x,y). */
void trendArrow(LGFX_Sprite &g, int x, int y, const RollingGraph &gr,
                int back = 8, int deadband = 2);

/* Barometric direction marker: a triangle pointing the way the needle is
 * moving, doubled when the change is a sharp one. `dir` is -1/0/+1. Drawn
 * rather than typed because the font has no arrow glyphs. Returns the width
 * consumed, so callers can place text after it without guessing. */
int baroArrow(LGFX_Sprite &g, int x, int y, int dir, bool sharp, uint16_t c);

/* Little wolf paw print (pad + 4 toe beans) centred at (cx,cy), ~8x10. */
void pawPrint(LGFX_Sprite &g, int cx, int cy, uint16_t color);

/* Word-wrap text into a box at the current font, breaking overlong words by
 * characters (UTF-8 aware). Returns the next y after the last line drawn.
 * Stops at maxLines / box bottom. */
int textWrap(LGFX_Sprite &g, const char *s, int x, int y, int w, int lineH,
             int maxLines, uint16_t color);

/* Like textWrap, but renders inline COLOUR emoji from the emoji atlas: emoji
 * codepoints are blitted as 18x18 bitmaps, other text drawn with the current
 * font. Word-wrapped, UTF-8 aware (incl. 4-byte). Returns lines drawn. */
int drawEmojiText(LGFX_Sprite &g, const char *s, int x, int y, int w, int lineH,
                  int maxLines, uint16_t color);

} // namespace widgets

#endif
