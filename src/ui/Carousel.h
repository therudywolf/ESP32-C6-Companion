/*
 * Nocturne C6 — the carousel: what comes after what, and how often.
 *
 * Two things were wrong with rotating through SceneId order at a fixed
 * interval, and the owner named both:
 *
 *   "очередность карточек и их сортировка — не логичны, непонятны. Плюс можно
 *    сделать разные виды карусели. Где неважны параметры типа погоды,
 *    показывают каждый 5й круг, а важные по 2 раза за круг."
 *
 * ── Order is not the enum ─────────────────────────────────────────────────
 *
 * SceneId order is the order things were BUILT in. ДОМ, ПЛАТА C6 and АНАЛИЗ
 * are all appended at the end with a comment explaining that inserting them
 * where they belong would renumber every saved scnMask bit and every
 * `screen:N` the server has ever sent. That constraint is real and it stays;
 * what changes is that the ring no longer has to obey it.
 *
 * ORDER below reads as a sentence: the wolf, then the machine, then the
 * network the machine sits on, then what is happening on it, then the board
 * itself, then the room, then the sky, then the diversions. Each hop is to
 * something related to the last, which is the thing an arbitrary order costs
 * you — a glance that starts by working out what it is looking at.
 *
 * ── Frequency, because not everything deserves the same share ─────────────
 *
 * A forecast that updates every twenty minutes does not need a turn every
 * ninety seconds, and the summary screen is worth two. So each scene carries
 * a period in ROUNDS, and one of them (TWICE) is drawn twice per round,
 * spaced half a round apart rather than back to back — adjacent repeats read
 * as the carousel being stuck.
 *
 * A round is one full pass of the order table. Scenes with period 5 appear on
 * rounds 0, 5, 10; that is why the counter has to survive the pass rather
 * than being derived from the current scene.
 */
#ifndef NOCT_CAROUSEL_H
#define NOCT_CAROUSEL_H

#include <stdint.h>

#include "ui/SceneIds.h"

namespace carousel {

/* How often a scene takes its turn. Stored three bits at a time, so the
 * whole table for twenty scenes fits in two words of NVS. */
enum Freq : uint8_t {
  FQ_OFF = 0,   /* никогда */
  FQ_TWICE = 1, /* дважды за круг */
  FQ_EVERY = 2, /* каждый круг */
  FQ_HALF = 3,  /* через круг */
  FQ_THIRD = 4, /* каждый 3-й круг */
  FQ_FIFTH = 5, /* каждый 5-й круг */
  FQ_COUNT = 6
};

inline const char *freqName(uint8_t f) {
  switch (f) {
  case FQ_TWICE: return "2 раза";
  case FQ_EVERY: return "каждый";
  case FQ_HALF: return "через круг";
  case FQ_THIRD: return "каждый 3-й";
  case FQ_FIFTH: return "каждый 5-й";
  default: return "выкл";
  }
}

/* Rounds between showings. TWICE and EVERY both come up every round; they
 * differ in how many turns they get inside it. */
inline int periodOf(uint8_t f) {
  switch (f) {
  case FQ_TWICE:
  case FQ_EVERY: return 1;
  case FQ_HALF: return 2;
  case FQ_THIRD: return 3;
  case FQ_FIFTH: return 5;
  default: return 0; /* off */
  }
}

/* The ring, in reading order. FORZA is not here: it is a HUD that takes over
 * when telemetry arrives, and rotating onto an idle "жду телеметрию" is the
 * carousel showing a waiting room. */
static const uint8_t ORDER[] = {
    SCENE_DEN,      /* кто мы */
    SCENE_DASH,     /* ── машина ── */
    SCENE_CPU,   SCENE_GPU,      SCENE_RAM,     SCENE_DISKS,
    SCENE_FANS,  SCENE_MB,
    SCENE_NET,      /* ── сеть и то, что на ней ── */
    SCENE_FOREST, SCENE_SERVICES, SCENE_EVENTS, SCENE_HISTORY,
    SCENE_BOARD,    /* ── сама плата ── */
    SCENE_HOME,     /* ── комната ── */
    SCENE_ANALYSIS,
    SCENE_WEATHER,  /* ── улица ── */
    SCENE_MEDIA,    /* ── прочее ── */
    SCENE_CLAUDE,
};
static const int ORDER_N = (int)(sizeof(ORDER) / sizeof(ORDER[0]));

/* Named modes. The owner asked for "разные режимы", and a mode is exactly a
 * whole frequency table: which screens this hour is about.
 *
 * Index 0 is the custom table the web panel edits, so picking a preset and
 * then changing one screen does not silently drop back to the preset. */
struct Preset {
  const char *name;
  uint8_t freq[SCENE_COUNT];
};

/* Written out per scene rather than as a loop with exceptions: the whole
 * point of a mode is that you can read what it does. Index order is SceneId
 * order, NOT ring order. */
#define NOCT_CAR_ROW(den, dash, cpu, gpu, ram, dsk, fan, mb, net, med, wea,   \
                     cla, forr, srv, evt, his, hom, brd, ana, frz)            \
  {                                                                          \
    den, dash, cpu, gpu, ram, dsk, fan, mb, net, med, wea, cla, forr, srv,    \
        evt, his, hom, brd, ana, frz                                         \
  }

static const Preset PRESETS[] = {
    /* 0 — своя: seeded from «за компом», then edited from the web panel. */
    {"своя",
     NOCT_CAR_ROW(FQ_EVERY, FQ_TWICE, FQ_EVERY, FQ_EVERY, FQ_EVERY, FQ_HALF,
                  FQ_HALF, FQ_HALF, FQ_EVERY, FQ_EVERY, FQ_FIFTH, FQ_HALF,
                  FQ_HALF, FQ_HALF, FQ_EVERY, FQ_THIRD, FQ_EVERY, FQ_THIRD,
                  FQ_THIRD, FQ_OFF)},
    /* 1 — за компом: the machine and what it is doing, weather rarely. */
    {"за компом",
     NOCT_CAR_ROW(FQ_EVERY, FQ_TWICE, FQ_EVERY, FQ_EVERY, FQ_EVERY, FQ_HALF,
                  FQ_HALF, FQ_HALF, FQ_EVERY, FQ_EVERY, FQ_FIFTH, FQ_HALF,
                  FQ_HALF, FQ_HALF, FQ_EVERY, FQ_THIRD, FQ_EVERY, FQ_THIRD,
                  FQ_THIRD, FQ_OFF)},
    /* 2 — дом: the room, the sky and the wolf. The PC gets a summary. */
    {"дом",
     NOCT_CAR_ROW(FQ_TWICE, FQ_HALF, FQ_OFF, FQ_OFF, FQ_OFF, FQ_OFF, FQ_OFF,
                  FQ_OFF, FQ_THIRD, FQ_HALF, FQ_EVERY, FQ_THIRD, FQ_THIRD,
                  FQ_OFF, FQ_HALF, FQ_OFF, FQ_TWICE, FQ_HALF, FQ_EVERY,
                  FQ_OFF)},
    /* 3 — железо: temperatures and fans, nothing else. For a load test. */
    {"железо",
     NOCT_CAR_ROW(FQ_HALF, FQ_EVERY, FQ_TWICE, FQ_TWICE, FQ_EVERY, FQ_EVERY,
                  FQ_TWICE, FQ_EVERY, FQ_HALF, FQ_OFF, FQ_OFF, FQ_OFF, FQ_OFF,
                  FQ_OFF, FQ_HALF, FQ_HALF, FQ_OFF, FQ_EVERY, FQ_OFF,
                  FQ_OFF)},
    /* 4 — тихо: three screens, slowly. What sits on a desk at night. */
    {"тихо",
     NOCT_CAR_ROW(FQ_TWICE, FQ_EVERY, FQ_OFF, FQ_OFF, FQ_OFF, FQ_OFF, FQ_OFF,
                  FQ_OFF, FQ_OFF, FQ_OFF, FQ_HALF, FQ_OFF, FQ_OFF, FQ_OFF,
                  FQ_OFF, FQ_OFF, FQ_EVERY, FQ_OFF, FQ_OFF, FQ_OFF)},
    /* 5 — всё поровну: every enabled screen, one turn each. The old
     * behaviour, kept because it is the one that needs no explaining. */
    {"всё поровну",
     NOCT_CAR_ROW(FQ_EVERY, FQ_EVERY, FQ_EVERY, FQ_EVERY, FQ_EVERY, FQ_EVERY,
                  FQ_EVERY, FQ_EVERY, FQ_EVERY, FQ_EVERY, FQ_EVERY, FQ_EVERY,
                  FQ_EVERY, FQ_EVERY, FQ_EVERY, FQ_EVERY, FQ_EVERY, FQ_EVERY,
                  FQ_EVERY, FQ_OFF)},
};
#undef NOCT_CAR_ROW
static const int PRESET_N = (int)(sizeof(PRESETS) / sizeof(PRESETS[0]));

/* ── the pass ─────────────────────────────────────────────────────────────
 *
 * Builds the list of scenes due in `round` and returns the one at `slot`,
 * or -1 when the round is empty (everything masked off or the PC is dark).
 * Deliberately recomputed rather than cached: it is twenty comparisons once
 * every ten seconds, and a cache would need invalidating on six different
 * settings changes.
 *
 * `skip` lets the caller drop scenes that have nothing to show — a dark PC,
 * an unpaired sensor — without this file knowing why. */
typedef bool (*SkipFn)(int scene, void *ctx);

/* Room for one round: every scene once, plus a second place for each of the
 * twice-per-round ones. Twenty-five bytes of stack, rebuilt once every ten
 * seconds — which is cheaper than the invalidation a cache would need across
 * six different settings changes, and very much easier to be sure about. */
static const int ROUND_MAX = ORDER_N * 2;

/* Fill `out` with the scenes due in `round`, in the order they will show.
 * Returns how many. A TWICE scene is placed at its own position and again
 * half a round later: back to back it reads as the carousel being stuck. */
inline int buildRound(const uint8_t *freq, uint32_t mask, int round,
                      SkipFn skip, void *ctx, uint8_t *out) {
  int n = 0;
  int twice[ORDER_N];
  int twiceN = 0;
  for (int i = 0; i < ORDER_N && n < ROUND_MAX; i++) {
    int sc = ORDER[i];
    if (!(mask & (1u << sc))) continue;
    if (skip && skip(sc, ctx)) continue;
    int p = periodOf(freq[sc]);
    if (p <= 0 || (round % p) != 0) continue;
    if (freq[sc] == FQ_TWICE) twice[twiceN++] = n;
    out[n++] = (uint8_t)sc;
  }
  /* Insert the second copies back to front, so the positions recorded above
   * are still valid as each insertion shifts everything after it. */
  for (int k = twiceN - 1; k >= 0 && n < ROUND_MAX; k--) {
    int at = twice[k] + n / 2;
    if (at > n) at = n;
    for (int j = n; j > at; j--) out[j] = out[j - 1];
    out[at] = out[twice[k]];
    n++;
  }
  return n;
}

} // namespace carousel


#endif
