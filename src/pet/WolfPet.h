/*
 * Nocturne C6 — WolfPet: the tamagotchi core, ported VERBATIM from
 * Nocturne OS (#3). Stats decay over real time, persist in NVS "wolfpet"
 * (same keys — the pet survives the board migration if NVS is seeded).
 * The LLM personality is bolted on AROUND this class (PetBrain), never
 * inside: autonomous rest and deterministic decay stay untouched.
 * New: ACT_TALK — a third action that only asks the wolf to say something.
 */
#ifndef NOCT_WOLF_PET_H
#define NOCT_WOLF_PET_H

#include <Arduino.h>

#include "pet/PetKind.h"

class WolfPet {
public:
  enum Action { ACT_FEED = 0, ACT_PLAY, ACT_PET, ACT_TALK, ACT_COUNT };

  void begin();
  void tick(unsigned long now);
  void doAction(int action);

  int hunger() const { return hunger_; }
  int happy() const { return happy_; }
  int energy() const { return energy_; }
  uint32_t ageDays() const { return ageDays_; }
  bool isAlive() const { return alive_; }
  bool isSleeping() const { return sleeping_; }
  int mood() const; /* 0 = sad/fainted, 1 = ok, 2 = happy */
  /* Quiet hours: the pet turns in at a much higher energy than the daytime
   * threshold, so it sleeps when the owner does instead of at 3 a.m. by
   * chance. The decay model itself is untouched; only the point at which
   * "tired" becomes "asleep" moves. */
  void setNightRest(bool on) { nightRest_ = on; }
  bool nightRest() const { return nightRest_; }
  /* Life stage from age. A pet-day is a real hour, so a pup lasts a couple of
   * days of wall time and an elder is a month of living with the thing. The
   * wolf GROWS: that is the difference between a mascot and a companion, and
   * it costs nothing because ageDays was already being counted. */
  enum Stage { STAGE_PUP = 0, STAGE_ADULT, STAGE_ELDER };
  int stage() const {
    if (ageDays_ < 2) return STAGE_PUP;
    if (ageDays_ < 30) return STAGE_ADULT;
    return STAGE_ELDER;
  }
  /* "щенок / волк / седой" for a wolf, "котёнок / кот / старый кот" for a
   * cat: the species owns the words (PetKind). */
  const char *stageName() const { return petkind::stageName(stage()); }
  const char *statusText() const; /* deterministic — always true, RU */

private:
  void save();
  void clampAll();

  int hunger_ = 70, happy_ = 70, energy_ = 70;
  uint32_t ageDays_ = 0;
  bool alive_ = true;
  bool sleeping_ = false;
  unsigned long lastDecayMs_ = 0;
  unsigned long lastSaveMs_ = 0;
  uint32_t ageAccumMs_ = 0;
  bool nightRest_ = false;
  bool dirty_ = false; /* unsaved stat change — skips the periodic NVS write
                          while nothing changed (e.g. the pet is fainted) */
};

#endif
