/*
 * Nocturne C6 — tiered phrase store for the wolf's speech.
 *
 * Tier 1: SD /wolf/cache/<bucket>.jsonl — every successful LLM phrase is
 *         cached; offline we pick one from the matching bucket.
 * Tier 2: the flash table in PhraseCache.cpp — first boot, no SD, or the
 *         mix-in that keeps the wolf from converging on a dozen learned lines.
 *
 * ── Why a bucket is now a PATH, and a phrase is now a TEMPLATE ───────────
 *
 * The old table had one "idle" bucket. The wolf could be looking at the CPU
 * screen at three in the morning while a track played and the room was
 * 27 degrees, and offline it would say "Тишина... проверь бэкапы" — the same
 * five lines regardless. The LLM path got all of that context in its prompt;
 * the cache path got the word "idle". So without the model the wolf was not a
 * companion, it was a screensaver with a vocabulary.
 *
 * Two changes fix that without a model:
 *
 *   1. Buckets nest. "idle.scene.cpu" is tried first, then "idle.scene",
 *      then "idle". PetBrain names the most specific thing it knows; the
 *      table answers at whatever depth it has lines for. Adding a scenario
 *      is adding lines, not code.
 *   2. Phrases carry {placeholders} — {track}, {app}, {gpu}, {temp}… — filled
 *      from the same state the LLM prompt is built from. A line that needs
 *      a field the context does not have is simply not eligible, so nothing
 *      ever reads "{track}" on screen.
 *
 * Plus a short memory of what was just said, so the same line never lands
 * twice in a row — the single most noticeable tell of canned speech.
 *
 * The deterministic WolfPet::statusText() always renders regardless —
 * speech is decoration, stats are truth.
 */
#ifndef NOCT_PHRASE_CACHE_H
#define NOCT_PHRASE_CACHE_H

#include <Arduino.h>

#include "storage/SdStore.h"

/* What the wolf can see right now. Empty string / negative = unknown, and a
 * phrase that mentions an unknown field is skipped rather than shown with a
 * hole in it. Filled by PetBrain from AppState + WolfPet every time it speaks. */
struct PhraseCtx {
  const char *track = "";   /* current media track, or "" */
  const char *artist = "";
  const char *app = "";     /* foreground / top-CPU program, or "" */
  const char *scene = "";   /* Russian scene title the owner is looking at */
  int gpu = -1;             /* GPU load %, -1 unknown */
  int cpu = -1;             /* CPU load % */
  int gt = -1;              /* GPU temperature C */
  int ct = -1;              /* CPU temperature C */
  int temp = -999;          /* outdoor temperature C, -999 unknown */
  int roomT10 = -32768;     /* room temperature, tenths */
  int roomRh = -1;          /* room humidity % */
  int hour = -1;            /* wall-clock hour, -1 if no NTP */
  int ageDays = 0;
  int stage = 1;            /* WolfPet::Stage */
  int mood = 1;             /* WolfPet::mood() */
  int claudeWk = -1;        /* Claude weekly % */
};

class PhraseCache {
public:
  void begin(SdStore *sd) { sd_ = sd; }

  /* A phrase for a bucket path ("idle.scene.cpu"), most specific first,
   * placeholders filled from ctx, never the same line twice in a row.
   * SD-learned lines and the flash table are MIXED, not tiered: with SD alone
   * a bucket that learned twelve lines in its first week would say only those
   * twelve forever. */
  String pick(const char *bucket, const PhraseCtx &ctx);

  /* Old signature, kept for the two call sites that have no context. */
  String pick(const char *bucket) { return pick(bucket, PhraseCtx()); }

  /* Remember a successful LLM phrase (queued SD append; rotating cap). */
  void remember(const char *bucket, const String &phrase);

  /* For the console: how many flash lines answer this exact bucket. */
  static int flashCount(const char *bucket);

private:
  String pickFromSd(const char *bucket);
  String pickFromFlash(const char *bucket, const PhraseCtx &ctx);
  static bool eligible(const char *tpl, const PhraseCtx &ctx);
  static String fill(const char *tpl, const PhraseCtx &ctx);
  bool saidRecently(const char *phrase, int depth = 4) const;
  void noteSaid(const char *phrase);

  SdStore *sd_ = nullptr;
  /* Hashes of the last few lines spoken, any bucket. Four is enough to stop
   * the back-to-back repeat without starving a small bucket. */
  uint32_t recent_[4] = {0, 0, 0, 0};
  int recentAt_ = 0;
};

#endif
