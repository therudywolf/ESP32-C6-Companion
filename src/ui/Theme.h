/*
 * Nocturne C6 — "Flipper Den" design system: palette, fonts, draw helpers.
 * Rule: orange is chrome (frames, headers, selection, wolf); accent colors
 * carry state only (GOOD/warn/crit/info). Dithered fills give the Flipper
 * texture without extra colors.
 */
#ifndef NOCT_THEME_H
#define NOCT_THEME_H

#include "ui/Display.h"

class SdStore; /* global scope: declaring it inside the namespace would
                  make a distinct theme::SdStore that never matches */

namespace theme {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* Palette is RUNTIME now (extern globals, defined in Theme.cpp): the user
 * can switch presets or set a custom chrome color from the menu or the
 * companion app. Defaults = Cyberpunk 2077 preset. Draw code reads these
 * as plain variables; default args below bind them at each call. */
extern uint16_t BG;
extern uint16_t ORANGE;     /* chrome: frames, headers, selection */
extern uint16_t ORANGE_DIM; /* inactive chrome */
extern uint16_t TEXT;       /* primary text */
extern uint16_t DIM;        /* secondary text */
extern uint16_t PANEL;      /* panel fill */
extern uint16_t GOOD;       /* ok state */
extern uint16_t WARN;       /* warning state */
extern uint16_t CRIT;       /* alarm (blinks) */
extern uint16_t INFO;       /* data / net / LLM */
extern uint16_t ACCENT;     /* secondary accent */

/* Theme control. 12 presets (see kPresets in Theme.cpp). setChrome/setAccent
 * override individual hues on top of the active preset. */
static const int THEME_PRESETS = 22;
/* Themes are also FILES: 1.thm .. 8.thm under /themes on the card append to
 * the built-in
 * presets, so a palette can be shared, edited on a laptop and dropped in
 * without a rebuild — and costs no flash. Cycle over presetTotal(), never over
 * THEME_PRESETS, or the card ones are unreachable. */
static const int CARD_THEMES_MAX = 8;
int presetTotal();
int cardThemeCount();
int loadCardThemes(::SdStore *sd);
void applyPreset(int idx);
void setChrome(uint8_t r, uint8_t g, uint8_t b);
void setAccent(uint8_t r, uint8_t g, uint8_t b);
const char *presetName(int idx);
extern int currentPreset;

/* Full hand-tuned palette. Roles 0..9: BG, chrome, text, dim, panel, good,
 * warn, crit, info, accent. setColorRole sets one; getPalette reads all 10
 * (for persistence). A custom palette overrides the preset until a preset is
 * re-selected. */
static const int COLOR_ROLES = 10;
extern const char *roleName(int role);
void setColorRole(int role, uint8_t r, uint8_t g, uint8_t b);
void getPalette(uint16_t out[COLOR_ROLES]);
void applyPalette(const uint16_t pal[COLOR_ROLES]);

/* Background controls, independent of the colour preset.
 *  bgStyle: 0 = solid (clean), 1 = scanlines + sheen, 2 = dot grid.
 *  bgLight: false = dark theme bg, true = light/white background. */
extern int bgStyle;
extern bool bgLight;
void setBgStyle(int s);     /* 0..2 */
/* Greyscale the live palette. For screenshot review: hue hides a pixel of
 * drift, and the animated backdrop makes two captures impossible to compare.
 * Reversible — it re-derives from the active preset rather than replacing
 * it, so nothing is lost when it goes off. */
void setMono(bool on);
bool monoOn();

/* Per-panel tone correction: a gain per CHANNEL and a black point.
 *
 * One knob per channel rather than one for red, because which channel is
 * wrong is a property of the panel in front of the owner, not something that
 * can be decided here. The black point lifts or crushes the floor.
 *
 * (What follows is why this exists at all.)
 *
 * WHY THIS EXISTS. On this glass the test card — whose swatches are written
 * as literal rgb() values, bypassing the palette entirely — shows the grey
 * ramp as BLUE and the yellow patch as GREEN, while pure red, green and blue
 * are each correct on their own. Red is present but swamped whenever it is
 * mixed. That is a property of the panel, not of the framebuffer: the sprite
 * was decoded pixel by pixel and its channels differ by 4 counts, which is
 * RGB565 rounding.
 *
 * It matters far beyond the review mode: WARN is yellow and TEXT is white on
 * every preset, so both are wrong on this hardware until red is lifted.
 * Applied to the palette only — the test card stays uncorrected on purpose,
 * so there is always one place showing what the panel really does. */
void setTone(int gainR, int gainG, int gainB, int black);
void getTone(int *gainR, int *gainG, int *gainB, int *black);
void setBgLight(bool light);/* re-applies the active preset in light/dark */
static const int BG_STYLES = 3;
const char *bgStyleName(int s);

/* Frame clock for animations — set once per frame in SceneManager::draw, read
 * by draw helpers so they animate without threading `now` through every call. */
extern unsigned long nowMs;

/* Reactive backdrop state, set once per frame: reactLevel 0..100 (PC busy-ness,
 * = max(cpu,gpu) load) makes the background livelier; reactAlert tints it red. */
extern int reactLevel;
extern bool reactAlert;
/* WMO weather code from the payload. The board already knows it is raining
 * outside; letting the backdrop know costs nothing and is the sort of thing
 * people actually notice. 0 = clear / unknown. */
extern int weatherCode;

/* Per-element UI composition: a bitmask of which optional widget classes are
 * shown across scenes. Set once per frame from Settings. */
enum {
  UI_GRAPHS = 0, /* sparklines / history curves */
  UI_TRENDS,     /* ▲▼ trend carets */
  UI_STRIPS,     /* secondary bottom-band info lines */
  UI_PAWS,       /* decorative paw prints */
  UI_WOLFOVL,    /* wolf speech overlay on non-DEN scenes */
  UI_ELEM_COUNT
};
extern uint16_t uiElements;
inline bool uiOn(int bit) { return (uiElements >> bit) & 1u; }
/* Linear-interpolate two RGB565 colors (t = 0..255). */
uint16_t lerp565(uint16_t a, uint16_t b, int t);

/* Subtle animated cyber backdrop (faint scanlines + a sweeping sheen) drawn
 * behind scene content in the band y0..y1. Cheap; reads through empty areas. */
void backdrop(LGFX_Sprite &g, int y0, int y1);

/* Fonts (lgfx wrappers over U8g2 font data — Cyrillic capable where needed;
 * defined in Theme.cpp). Chunky 2x integer scaling = the Flipper aesthetic. */
extern const lgfx::U8g2font F_SMALL;  /* 5x8 cyrillic: labels, hints */
extern const lgfx::U8g2font F_TEXT;   /* haxrcorp4089 cyrillic: small text */
extern const lgfx::U8g2font F_MED;    /* 10x20 cyrillic: PRIMARY scene text */
extern const lgfx::U8g2font F_VALUE;  /* helvB10: bold values (latin) */
extern const lgfx::U8g2font F_BIG;    /* logisoso24: big numbers */
extern const lgfx::U8g2font F_HUGE;   /* logisoso32: hero numbers */

/* Pick accent by percent-of-limit (load/usage bars). */
inline uint16_t pctColor(int pct) {
  if (pct >= 90) return CRIT;
  if (pct >= 75) return WARN;
  return GOOD;
}
/* Pick accent for a temperature given warn/crit thresholds. */
inline uint16_t tempColor(int t, int warn = 70, int crit = 85) {
  if (t >= crit) return CRIT;
  if (t >= warn) return WARN;
  return GOOD;
}

/* 1px checkerboard fill — reads as a 50% tint, the Flipper texture trick. */
void ditherRect(LGFX_Sprite &g, int x, int y, int w, int h, uint16_t color);

/* Flipper-style panel: thin frame, cut corner, optional title tab. */
void panel(LGFX_Sprite &g, int x, int y, int w, int h,
           const char *title = nullptr, uint16_t color = ORANGE_DIM,
           uint16_t titleColor = ORANGE);

/* ── the second grammar: surfaces instead of outlines ────────────────────
 *
 * panel() draws information as LINES ON EMPTINESS - a contour with the label
 * punched through its top edge. That is the oscilloscope look and it is fine,
 * but it has one real weakness here: with 22 palettes and a colour editor, a
 * contour on a LIGHT background either vanishes or cuts, and there is no
 * single line colour that works for both. A filled surface works for both,
 * because the separation is carried by TONE rather than by a stroke.
 */

/* The raised surface, and the label colour that survives on it. Both derived
 * from the palette when it changes, not stored in it.
 *
 * Derived on purpose: a palette FIELD would have to be filled in for all 22
 * presets, for the eight that live on the card, AND for whatever colour the
 * owner mixes by hand in the editor - and that last one is exactly the case a
 * hardcoded field gets wrong.
 *
 * TWO values, not one, because the requirements pull opposite ways: the tile
 * has to separate from the background (lift up) while the label has to stay
 * readable on the tile (lift down). A numeric sweep across all 22 presets
 * found NO single lift that satisfies both using the global DIM - five
 * palettes had no workable value at any lift. So the lift answers visibility
 * and SURFACE_DIM answers readability, each solved against its own bar. */
extern uint16_t SURFACE;     /* material tile fill */
extern uint16_t SURFACE_DIM; /* label ON a tile — never plain DIM */

/* The rectangle a tile leaves for its contents. Returned rather than assumed,
 * because the label now lives INSIDE the surface and eats the first rows -
 * a screen that positions its text from the tile's own x/y would draw it
 * straight through the label. */
struct Rect {
  int x, y, w, h;
};

/* Material-style tile: filled, rounded, label inside, no outline at all.
 * Returns the content rectangle. */
Rect panelM(LGFX_Sprite &g, int x, int y, int w, int h,
            const char *title = nullptr, uint16_t titleColor = 0);

/* Everything on a material screen sits on a 4 px grid. Small screen, so 4
 * rather than Material's 8: at 172 px tall an 8 px rhythm spends a quarter of
 * the height on air. */
/* Split by axis, and both still on the grid. A uniform 8 costs 16 px of every
 * tile's height, and on a 172 px screen carrying four tiles that is a whole
 * row of text spent on air - which is exactly how the forecast strip ended up
 * with its second line printed through the descenders of its first. */
static const int MPADX = 8;  /* left and right inside a tile */
static const int MPADY = 4;  /* top and bottom */
static const int MGAP = 4;   /* between tiles */

/* Horizontal stat bar: frame + dithered fill + solid tip. */
void hBar(LGFX_Sprite &g, int x, int y, int w, int h, int pct, uint16_t color);

/* Vertical bar (fans etc.). */
void vBar(LGFX_Sprite &g, int x, int y, int w, int h, int pct, uint16_t color);

/* ── ink metrics ─────────────────────────────────────────────────────────
 *
 * A font has THREE heights and they are all different:
 *
 *   the name        "logisoso24"  — what the composer reads
 *   the line box     35 px        — what fontHeight() returns and what the
 *                                   renderer actually writes into
 *   the ink          24 px        — what the eye sees
 *
 * Every screen in this firmware was composed against the first one, so tiles
 * were sized 24 px for a glyph that needs 35 and the values were clipped by
 * eleven. Measured on the board with the `fontcard` command; the rule
 * ink_top = line_box - ascent - descent holds on all six.
 *
 * Centre by the INK or the digits sit visibly high; check fits by the LINE
 * BOX or the renderer clips what the eye cannot yet see. */
/* WHICH FONTS CAN SPELL RUSSIAN. Only three of the six:
 *
 *   F_SMALL  5x8_t_cyrillic          8 px   yes
 *   F_TEXT   haxrcorp4089_t_cyrillic 11 px  yes
 *   F_MED    10x20_t_cyrillic        20 px  yes
 *   F_VALUE  helvB10_tr              15 px  NO — Latin subset
 *   F_BIG    logisoso24_tr           35 px  NO
 *   F_HUGE   logisoso32_tr           47 px  NO
 *
 * The "_tr" faces draw a hollow box for every Cyrillic codepoint, silently.
 * So a Russian VALUE has exactly one size that is both legible at a metre
 * and spellable: F_MED. Digits and Latin units may use anything.
 *
 * This is why units get split from their numbers all over these screens —
 * "270" in F_VALUE beside "МГц" in F_TEXT — rather than being formatted into
 * one string. */
struct Ink {
  int top;       /* leading rows above the ink */
  int height;    /* a digit or a capital */
  int box;       /* the full line box — what fontHeight() returns */
  bool latin;    /* true = "_tr" subset, cannot spell Cyrillic */
};
extern const Ink INK_SMALL, INK_TEXT, INK_VALUE, INK_MED, INK_BIG, INK_HUGE;

/* Cursor y that centres this font's ink in the band [top, top+h). */
int inkY(const Ink &k, int top, int h, int size = 1);
/* Last row the ink will occupy, for a fits-check that matches the eye. */
int inkBottom(const Ink &k, int y, int size = 1);
/* Last row the RENDERER will touch — the line box, which is what clips. */
int boxBottom(const Ink &k, int y, int size = 1);

/* ── layout lint ─────────────────────────────────────────────────────────
 *
 * Built only into the `nocturne-c6-lint` environment. Every text helper
 * checks what it is about to draw against the rectangle it is supposed to
 * stay inside, and reports over serial when it does not.
 *
 * WHY A TOOL AND NOT AN EYE. The bug that motivated this is invisible by
 * inspection: a font called "logisoso24" occupies 35 pixels, so a tile sized
 * from the name clips the value by eleven and nobody notices until they look
 * at a photograph. Every screen in this firmware was composed against the
 * NAME. Finding the rest by reading code means re-deriving the same
 * arithmetic seventeen times and being wrong somewhere; the board can just
 * measure it, and it measures the string that is actually on screen with the
 * data that is actually there.
 *
 * The rect is set by panel()/panelM(), so a screen gets the check for free
 * the moment it uses either. */
void lintClip(int x, int y, int w, int h);

/* ── the second check: elements against each other ───────────────────────
 *
 * lintClip answers "does this text fit its tile". Every fault the owner
 * listed is the OTHER question — does this text hit that line, does this
 * arrow hit that number — and no amount of the first check answers it. So
 * everything drawn registers the rectangle it occupies, and at the end of
 * the frame the set is tested pairwise.
 *
 * `kind` separates what may legitimately share space from what may not:
 * a bar's fill sits inside its own frame by design, and reporting that
 * would bury the real collisions. */
enum LintKind {
  LK_TEXT,  /* a string */
  LK_FRAME, /* a tile border or rule — thin, and nothing should cross it */
  LK_ART,   /* bars, sparklines, icons, arrows */
  LK_FILL,  /* a background wipe: it ERASES whatever it covers */
};
void lintRect(int kind, int x, int y, int w, int h, const char *what);
/* Same, but tagged as belonging to one tile's chrome — see LRect::own. */
void lintRectOwned(int kind, int x, int y, int w, int h, const char *what,
                   int own);
/* The owner id panel()/panelM() give a tile at (x, y). A scene that draws its
 * own chrome onto a tile's frame on purpose — a counter sitting on the border
 * the way a label tab does — tags it with this and the check stays quiet. */
int lintOwner(int x, int y);
/* Test the frame's registered rectangles and report collisions. */
void lintFrameEnd();
void lintClear();
/* Also reports the current SCENE, so a violation can be found without
 * guessing which screen was up. */
void lintScene(const char *name);
/* Forget what has already been reported, so revisiting a screen reports it
 * again rather than staying silent because the first visit exhausted it. */
void lintReset();

/* Text helpers (current font/size respected). */
void textAt(LGFX_Sprite &g, int x, int y, const char *s, uint16_t color);
void textRight(LGFX_Sprite &g, int xRight, int y, const char *s,
               uint16_t color);
void textCenter(LGFX_Sprite &g, int xCenter, int y, const char *s,
                uint16_t color);

/* Copy whole UTF-8 codepoints from s into out while the rendered width at the
 * current font stays <= maxW (so multi-byte Cyrillic is never cut mid-glyph,
 * and the text never overruns its column). out is always NUL-terminated.
 * Returns out for chaining. */
const char *clipW(LGFX_Sprite &g, const char *s, char *out, size_t cap,
                  int maxW);

} // namespace theme

#endif
