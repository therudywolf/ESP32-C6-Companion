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
  bool dirty_ = false; /* unsaved stat change — skips the periodic NVS write
                          while nothing changed (e.g. the pet is fainted) */
};

#endif
