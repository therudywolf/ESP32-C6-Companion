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
  /* console: one draw from a bucket with the live context, not spoken */
  String samplePhrase(const char *bucket, AppState &st) {
    return cache_->pick(bucket, makeCtx(st));
  }
  /* Something the board worked out on its own (a baseline drift from the SD
   * archive). Queued rather than spoken directly, so the wolf phrases it in
   * character instead of reciting a number. */
  void notice(const String &eventRu) { pendingNotice_ = eventRu; }
  /* Ask the model, once a day, to write the day up in the wolf's own voice and
   * file it on the card. Tagged so the answer is filed, not spoken. */
  void writeJournal(const String &dayDate, const String &summary);
  bool journalBusy() const { return journalPending_; }
  /* Drained by main for the achievement counters: onAction and the journal
   * both happen in here, and both are things worth counting. */
  int takeActionEvent() {
    int a = actionEvent_;
    actionEvent_ = -1;
    return a;
  }
  bool takeJournalWritten() {
    bool w = journalWritten_;
    journalWritten_ = false;
    return w;
  }
  /* the scene the owner is currently looking at (nullptr = DEN/Forza/none);
   * lets idle chatter occasionally comment on what's on screen. */
  void setViewScene(const char *name) { viewSceneName_ = name; }

  /* Rendering state */
  bool bubbleVisible(unsigned long now) const;
  /* 0..1 slide envelope for the speech overlay (ease in / hold / ease out). */
  float speechEnvelope(unsigned long now) const;
  bool thinking() const { return thinking_; }
  const String &phrase() const { return phrase_; }
  int revealChars(unsigned long now) const; /* typewriter, UTF-8 aware */
  bool talkingAnim(unsigned long now) const;
  /* last action for the DEN reaction burst: 0 feed,1 play,2 pet,3 talk,-1 none */
  int reactionKind() const { return reactionKind_; }
  unsigned long reactionAt() const { return reactionAt_; }

  /* emotional tone of the latest utterance, consumed once by main to tint the
   * LED (-1 = nothing new). Derived from the trigger bucket, no extra model out. */
  enum { TONE_NEUTRAL = 0, TONE_HAPPY, TONE_TENSE, TONE_LOW };
  int takeSpeechTone() {
    int t = speechTone_;
    speechTone_ = -1;
    return t;
  }

private:
  void trigger(const char *bucket, const char *eventRu, unsigned long now,
               AppState &st, bool urgent, bool forceLlm = false,
               bool solicited = false);
  void show(const String &p, unsigned long now, int tone = TONE_NEUTRAL);
  static int toneForBucket(const char *bucket);
  String buildContext(const char *eventRu, AppState &st);
  /* What the wolf can see, for the cache path. The LLM path gets the same
   * facts as prose in buildContext(); this is the structured twin. */
  PhraseCtx makeCtx(AppState &st);
  /* The most specific idle bucket the moment supports ("idle.scene.CPU",
   * "idle.weather.rain", "idle.night"…). The cache falls back up the path. */
  String idleBucket(AppState &st, const PhraseCtx &c);
  static int clockHour();
  void diary(const char *ev);

  WolfPet *pet_ = nullptr;
  LlmClient *llm_ = nullptr;
  PhraseCache *cache_ = nullptr;
  SdStore *sd_ = nullptr;

  String phrase_;
  unsigned long speechStart_ = 0;
  unsigned long speechHold_ = 0;
  bool thinking_ = false;
  char pendingBucket_[32] = {0}; /* "idle.scene.СЕРВИСЫ" is 25 bytes */
  char appBuf_[32] = {0};        /* top process, ".exe" stripped, for {app} */
  unsigned long lastSpeech_ = 0;
  unsigned long nextIdleChatter_ = 0;
  int actionPending_ = -1;
  unsigned long actionPendingAt_ = 0;
  bool bootGreetPending_ = true;
  int lastHour_ = -1;              /* wall-clock hour, for time-of-day remarks */
  bool nightScolded_ = false;      /* one "go to bed" per night, not per hour */
  bool morningGreeted_ = false;
  String pendingNotice_;           /* an archive finding waiting to be voiced */
  String journalDate_;             /* day the in-flight journal entry covers */
  String lastJournal_;             /* newest entry, folded into every prompt */
  bool journalPending_ = false;
  bool journalWritten_ = false;
  int actionEvent_ = -1;
  static const int kTagSpeech = 0, kTagJournal = 1;
  int reactionKind_ = -1;          /* DEN particle burst kind */
  unsigned long reactionAt_ = 0;   /* when the last action fired */
  int speechTone_ = -1;            /* tone of the latest utterance (main → LED) */
  const char *viewSceneName_ = nullptr; /* scene the owner is viewing */

  /* edge detection */
  bool lastAlert_ = false;
  bool lastSleeping_ = false;
  bool lastAlive_ = true;
  bool lowHungerFired_ = false;
  bool lowEnergyFired_ = false;
  bool lowHappyFired_ = false;
  bool claudeFired_ = false;
  String lastAlertSig_; /* severity|top of the last commented server event */
  String lastTrack_;
  int lastWmo_ = -999;
  bool seenFirstPayload_ = false;
  String lastApp_;          /* foreground app (top CPU process) */
  unsigned long lastAppAt_ = 0;

  /* PC activity watchers */
  unsigned long gpuHighSince_ = 0;
  unsigned long cpuHighSince_ = 0;
  bool gamingFired_ = false;
  bool cpuWorkFired_ = false;
  int lastPidle_ = -1;
  bool awayFired_ = false;
  bool lastForzaLive_ = false;

  /* LLM failure backoff. The LM Studio endpoints live on the owner's PC (LAN +
   * public DDNS fallback). With the PC off, both are dead and each call burns
   * ~connect-timeout walking them before falling back — so after 2 consecutive
   * failures we suppress LLM calls for a window and go STRAIGHT to the instant
   * phrase cache (no "thinking" stall). The window expiry lets one probe retry,
   * so it self-heals the moment the PC/LLM is reachable again. This keeps the
   * hotspot scenario alive (PC on, telemetry unreachable, public LLM works)
   * unlike a blunt tcpConnected gate. */
  int llmFailStreak_ = 0;
  unsigned long llmSuppressUntil_ = 0;
  static const unsigned long kLlmSuppressMs = 300000UL; /* 5 min */
};

#endif
