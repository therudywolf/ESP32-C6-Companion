/*
 * Nocturne C6 — climate alerts on the ForestHome sensor.
 *
 * The board watches one room. When it leaves the band the owner set, the wolf
 * says so, the screen toasts, and the LED goes to the alert colour — the same
 * three channels a hardware alert already uses, because a second notification
 * grammar would just be one more thing to learn.
 *
 * Three rules shape this, all learned from the sensor rather than assumed:
 *
 *  - HYSTERESIS. A room sitting exactly on the threshold would otherwise
 *    announce itself every report forever. An alert clears only after the
 *    reading comes back INSIDE the band by a margin.
 *  - EDGES, not levels. Fire once on entry and once on return, never on the
 *    plain fact of being out of range — the pet already has enough to say.
 *  - STALE IS NOT SAFE. A sensor that has gone quiet must not clear an alert:
 *    "no reading" and "reading is fine" are different, and only one of them
 *    means the room is warm.
 */
#ifndef NOCT_CLIMATE_ALERT_H
#define NOCT_CLIMATE_ALERT_H

#include <Arduino.h>

#include "core/Types.h"

class PetBrain;
class SceneManager;

class ClimateAlert {
public:
  /* Called every loop; cheap, and does nothing unless a reading changed. */
  void tick(unsigned long now, AppState &st, PetBrain &brain,
            SceneManager &ui);
  /* True while any bound is currently breached — drives the LED and lets the
   * ДОМ screen colour the offending number. */
  bool active() const { return tempHigh_ || tempLow_ || humHigh_ || humLow_; }
  bool tempAlert() const { return tempHigh_ || tempLow_; }
  bool humAlert() const { return humHigh_ || humLow_; }
  bool battAlert() const { return battLow_; }

private:
  /* One degree / three percent of margin before an alert clears. Below that a
   * room drifting across the line would flap. */
  static const int kTempHystX10 = 10;
  static const int kHumHyst = 3;

  bool tempHigh_ = false, tempLow_ = false;
  bool humHigh_ = false, humLow_ = false;
  bool battLow_ = false;
  int lastTemp10_ = -32768;
  int lastHum_ = -1;
  unsigned long battNagUntil_ = 0;
};

#endif
