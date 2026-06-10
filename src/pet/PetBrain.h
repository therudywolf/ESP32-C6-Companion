/*
 * Nocturne C6 — PetBrain: the wolf's voice. Sits BETWEEN WolfPet (untouched
 * deterministic tamagotchi) and LlmClient (async LM Studio call). Watches
 * pet stats + PC telemetry for trigger edges, shapes a compact Russian
 * prompt, hides latency behind a "thinking" bubble, falls back to cached
 * phrases when offline. Keeps a small diary on SD so the wolf remembers.
 */
#ifndef NOCT_PET_BRAIN_H
#define NOCT_PET_BRAIN_H

#include <Arduino.h>

#include "core/Types.h"
#include "net/LlmClient.h"
#include "pet/PhraseCache.h"
#include "pet/WolfPet.h"
#include "storage/SdStore.h"

class PetBrain {
public:
  void begin(WolfPet *pet, LlmClient *llm, PhraseCache *cache, SdStore *sd);
  void onAction(int action); /* ACT_FEED / ACT_PLAY / ACT_TALK from the UI */
  void tick(unsigned long now, AppState &st);
  /* remote: make the wolf say a literal line right now (companion app). */
  void sayNow(const String &text) { show(text, millis()); }

  /* Rendering state */
  bool bubbleVisible(unsigned long now) const;
  bool thinking() const { return thinking_; }
  const String &phrase() const { return phrase_; }
  int revealChars(unsigned long now) const; /* typewriter, UTF-8 aware */
  bool talkingAnim(unsigned long now) const;

private:
  void trigger(const char *bucket, const char *eventRu, unsigned long now,
               AppState &st, bool urgent, bool forceLlm = false);
  bool pcIdle(const AppState &st) const;
  void show(const String &p, unsigned long now);
  String buildContext(const char *eventRu, AppState &st);
  void diary(const char *ev);

  WolfPet *pet_ = nullptr;
  LlmClient *llm_ = nullptr;
  PhraseCache *cache_ = nullptr;
  SdStore *sd_ = nullptr;

  String phrase_;
  unsigned long speechStart_ = 0;
  unsigned long speechHold_ = 0;
  bool thinking_ = false;
  char pendingBucket_[16] = {0};
  unsigned long lastSpeech_ = 0;
  unsigned long nextIdleChatter_ = 0;
  int actionPending_ = -1;
  unsigned long actionPendingAt_ = 0;
  bool bootGreetPending_ = true;

  /* edge detection */
  bool lastAlert_ = false;
  bool lastSleeping_ = false;
  bool lastAlive_ = true;
  bool lowHungerFired_ = false;
  bool lowEnergyFired_ = false;
  bool lowHappyFired_ = false;
  bool claudeFired_ = false;
  char lastEventTop_[21] = {0};
  String lastTrack_;
  int lastWmo_ = -999;
  bool seenFirstPayload_ = false;

  /* PC activity watchers */
  unsigned long gpuHighSince_ = 0;
  unsigned long cpuHighSince_ = 0;
  bool gamingFired_ = false;
  bool cpuWorkFired_ = false;
  int lastPidle_ = -1;
  bool awayFired_ = false;
  bool lastForzaLive_ = false;
};

#endif
