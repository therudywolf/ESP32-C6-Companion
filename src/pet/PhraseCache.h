/*
 * Nocturne C6 — tiered phrase store for the wolf's speech.
 * Tier 1: SD /wolf/cache/<bucket>.jsonl — every successful LLM phrase is
 *         cached; offline we pick a random one from the matching bucket.
 * Tier 2: flash fallback table below — first boot / no SD.
 * The deterministic WolfPet::statusText() always renders regardless —
 * speech is decoration, stats are truth.
 */
#ifndef NOCT_PHRASE_CACHE_H
#define NOCT_PHRASE_CACHE_H

#include <Arduino.h>

#include "storage/SdStore.h"

class PhraseCache {
public:
  void begin(SdStore *sd) { sd_ = sd; }

  /* Random phrase for a bucket: SD cache first, flash table otherwise. */
  String pick(const char *bucket);

  /* Remember a successful LLM phrase (queued SD append; ~50 lines cap). */
  void remember(const char *bucket, const String &phrase);

private:
  String pickFromSd(const char *bucket);
  SdStore *sd_ = nullptr;
};

#endif
