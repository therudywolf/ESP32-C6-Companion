#include "pet/Achievements.h"

#include <Preferences.h>

namespace {
/* Short NVS keys: the namespace is small and these are written for years. */
const char *kKey[Achievements::ACH_COUNT] = {"fd", "pl", "pt", "tk", "dy",
                                             "fn", "lp", "tr", "sh", "jr"};
const char *kName[Achievements::ACH_COUNT] = {
    "Накормлен",   "Наигрался",  "Обласкан",  "Разговоры", "Прожито дней",
    "Обмороков",   "Кругов",     "Треков",    "Снимков",   "Записей"};

/* Milestones per counter, ascending, 0-terminated. Chosen so the first one
 * lands within a day or two of ordinary use and the last one is a genuine
 * "you have lived with this thing" marker. */
const uint32_t kTiers[Achievements::ACH_COUNT][5] = {
    {10, 50, 200, 1000, 0},  /* feed */
    {10, 50, 200, 1000, 0},  /* play */
    {10, 50, 200, 1000, 0},  /* pet  */
    {5, 25, 100, 500, 0},    /* talk */
    {1, 7, 30, 365, 0},      /* days */
    {1, 5, 20, 100, 0},      /* faints — surviving them is the achievement */
    {10, 100, 500, 2000, 0}, /* laps */
    {25, 250, 1000, 5000, 0},/* tracks */
    {1, 10, 50, 200, 0},     /* shots */
    {1, 7, 30, 365, 0},      /* journal entries */
};
} // namespace

void Achievements::begin() {
  Preferences p;
  p.begin("wolfstat", true);
  for (int i = 0; i < ACH_COUNT; i++) v_[i] = p.getUInt(kKey[i], 0);
  p.end();
  Serial.printf("[ACH] fed %lu, played %lu, days %lu, laps %lu, tracks %lu\n",
                (unsigned long)v_[ACH_FEED], (unsigned long)v_[ACH_PLAY],
                (unsigned long)v_[ACH_DAYS], (unsigned long)v_[ACH_LAP],
                (unsigned long)v_[ACH_TRACK]);
}

void Achievements::bump(Id id, uint32_t by) {
  if (id < 0 || id >= ACH_COUNT || by == 0) return;
  int before = level(id, v_[id]);
  v_[id] += by;
  dirty_ = true;
  int after = level(id, v_[id]);
  if (after > before)
    Serial.printf("[ACH] %s -> %lu (level %d)\n", kName[id],
                  (unsigned long)v_[id], after);
}

void Achievements::raise(Id id, uint32_t value) {
  if (id < 0 || id >= ACH_COUNT || value <= v_[id]) return;
  int before = level(id, v_[id]);
  v_[id] = value;
  dirty_ = true;
  if (level(id, v_[id]) > before)
    Serial.printf("[ACH] %s -> %lu\n", kName[id], (unsigned long)v_[id]);
}

void Achievements::tick(unsigned long now) {
  /* Five minutes, same cadence the pet itself uses. Counters like "tracks
   * heard" would otherwise erase a flash page every few minutes forever. */
  if (!dirty_ || now - lastSave_ < 300000UL) return;
  lastSave_ = now;
  save();
}

void Achievements::save() {
  Preferences p;
  p.begin("wolfstat", false);
  for (int i = 0; i < ACH_COUNT; i++) p.putUInt(kKey[i], v_[i]);
  p.end();
  dirty_ = false;
}

const char *Achievements::name(Id id) {
  return (id >= 0 && id < ACH_COUNT) ? kName[id] : "?";
}

uint32_t Achievements::nextTier(Id id, uint32_t have) {
  if (id < 0 || id >= ACH_COUNT) return 0;
  for (int t = 0; t < 4 && kTiers[id][t]; t++)
    if (have < kTiers[id][t]) return kTiers[id][t];
  return 0; /* topped out */
}

int Achievements::level(Id id, uint32_t have) {
  if (id < 0 || id >= ACH_COUNT) return 0;
  int n = 0;
  for (int t = 0; t < 4 && kTiers[id][t]; t++)
    if (have >= kTiers[id][t]) n++;
  return n;
}
