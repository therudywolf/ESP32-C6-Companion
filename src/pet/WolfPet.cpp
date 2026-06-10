#include "pet/WolfPet.h"

#include <Preferences.h>

#include "core/config.h"

void WolfPet::begin() {
  Preferences p;
  p.begin("wolfpet", true);
  hunger_ = p.getInt("h", 70);
  happy_ = p.getInt("j", 70);
  energy_ = p.getInt("e", 70);
  ageDays_ = p.getUInt("age", 0);
  alive_ = p.getBool("alive", true);
  sleeping_ = p.getBool("slp", false);
  p.end();
  clampAll();
  lastDecayMs_ = millis();
  lastSaveMs_ = millis();
}

void WolfPet::clampAll() {
  if (hunger_ < 0) hunger_ = 0;
  if (hunger_ > 100) hunger_ = 100;
  if (happy_ < 0) happy_ = 0;
  if (happy_ > 100) happy_ = 100;
  if (energy_ < 0) energy_ = 0;
  if (energy_ > 100) energy_ = 100;
}

void WolfPet::save() {
  Preferences p;
  p.begin("wolfpet", false);
  p.putInt("h", hunger_);
  p.putInt("j", happy_);
  p.putInt("e", energy_);
  p.putUInt("age", ageDays_);
  p.putBool("alive", alive_);
  p.putBool("slp", sleeping_);
  p.end();
}

void WolfPet::tick(unsigned long now) {
  while (now - lastDecayMs_ >= NOCT_WOLF_DECAY_INTERVAL_MS) {
    lastDecayMs_ += NOCT_WOLF_DECAY_INTERVAL_MS;
    if (!alive_) continue;
    ageAccumMs_ += NOCT_WOLF_DECAY_INTERVAL_MS;
    if (ageAccumMs_ >= NOCT_WOLF_DAY_MS) {
      ageAccumMs_ -= NOCT_WOLF_DAY_MS;
      ageDays_++;
    }
    if (sleeping_) {
      energy_ += 6;
      hunger_ -= 1;
      if (energy_ >= 96) {
        sleeping_ = false;
        happy_ += 3; /* woke up refreshed */
      }
    } else {
      hunger_ -= 2;
      happy_ -= 1;
      energy_ -= 1;
      if (energy_ <= 18) sleeping_ = true; /* falls asleep on its own */
    }
    clampAll();
    if (hunger_ == 0 && happy_ == 0 && energy_ == 0) {
      alive_ = false;
      save();
    }
  }
  if (now - lastSaveMs_ >= NOCT_WOLF_SAVE_INTERVAL_MS) {
    lastSaveMs_ = now;
    save();
  }
}

void WolfPet::doAction(int action) {
  if (!alive_) { /* any care revives a fainted pet */
    alive_ = true;
    sleeping_ = false;
    hunger_ = happy_ = energy_ = 40;
    clampAll();
    save();
    return;
  }
  switch (action) {
  case ACT_FEED:
    hunger_ += 30;
    happy_ += 5;
    break;
  case ACT_PLAY:
    happy_ += 25;
    energy_ -= 10;
    hunger_ -= 5;
    sleeping_ = false; /* playing wakes it */
    break;
  case ACT_TALK:
    /* no stat change — speech is PetBrain's job */
    break;
  default:
    break;
  }
  clampAll();
  save();
}

int WolfPet::mood() const {
  if (!alive_) return 0;
  int avg = (hunger_ + happy_ + energy_) / 3;
  if (avg < 30) return 0;
  if (avg < 65) return 1;
  return 2;
}

const char *WolfPet::statusText() const {
  if (!alive_) return "без сил! покорми";
  if (sleeping_) return "Zzz... спит";
  if (hunger_ < 25) return "голодный!";
  if (energy_ < 25) return "сонный...";
  if (happy_ < 25) return "скучает...";
  return mood() == 2 ? "доволен!" : "норм";
}
