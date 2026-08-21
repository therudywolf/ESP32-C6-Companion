/*
 * Nocturne C6 — what the wolf and its owner have actually done together.
 *
 * The tamagotchi has always been a snapshot: three bars and a mood. Everything
 * that HAPPENED — every meal, every lap driven, every night it fainted while
 * you were away — vanished the moment it scrolled off the screen. These are
 * simple counters, but they are the difference between a pet and a history
 * with a pet in it.
 *
 * NVS namespace "wolfstat", separate from settings so a factory reset cannot
 * erase a month of living with the thing. Writes are coalesced: a counter that
 * ticks on every track change must not spend a flash erase cycle each time.
 */
#ifndef NOCT_ACHIEVEMENTS_H
#define NOCT_ACHIEVEMENTS_H

#include <Arduino.h>

class Achievements {
public:
  enum Id {
    ACH_FEED = 0, /* meals served */
    ACH_PLAY,     /* games played */
    ACH_PET,      /* times stroked */
    ACH_TALK,     /* times asked to speak */
    ACH_DAYS,     /* oldest the wolf has ever been (a pet-day is a real hour) */
    ACH_FAINT,    /* times it ran out of everything and had to be revived */
    ACH_LAP,      /* Forza laps completed */
    ACH_TRACK,    /* tracks heard */
    ACH_SHOT,     /* screenshots taken */
    ACH_JOURNAL,  /* nights it wrote its own diary entry */
    ACH_COUNT
  };

  void begin();
  void bump(Id id, uint32_t by = 1);
  /* For a high-water mark (age) rather than a running total. */
  void raise(Id id, uint32_t value);
  uint32_t get(Id id) const { return v_[id]; }
  /* Persist if anything changed and enough time has passed. Call from loop. */
  void tick(unsigned long now);

  static const char *name(Id id);
  /* Next milestone above the current value, 0 when the top one is reached. */
  static uint32_t nextTier(Id id, uint32_t have);
  /* How many milestones this counter has passed — the "level". */
  static int level(Id id, uint32_t have);

private:
  void save();
  uint32_t v_[ACH_COUNT] = {0};
  bool dirty_ = false;
  unsigned long lastSave_ = 0;
};

#endif
