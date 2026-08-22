/*
 * Nocturne C6 — Zigbee coordinator: the board IS the hub.
 *
 * Measured before committing to it: the coordinator stack is +380 KB of flash
 * and +20 KB of static RAM, and Espressif's own zigbee_zczr partition gives
 * 1.25 MB app slots that a hello-world coordinator already overflows. So this
 * build runs a single 3.62 MB app partition and has NO OTA — updates are by
 * cable. That was the deliberate trade; see partitions/nocturne_zb.csv.
 *
 * What it does: forms a network, lets a sensor join on request, binds both the
 * temperature (0x0402) and humidity (0x0405) measurement clusters, and turns
 * their reports into the same `zb` readings the ПОГОДА screen already draws —
 * so a sensor on the board and a sensor relayed by a server look identical to
 * the rest of the firmware.
 *
 * Battery sensors report every half hour or so and then go silent, which drives
 * two decisions: readings carry an AGE (a stale number drawn as current is the
 * lie "no signal" exists to prevent), and the last set is kept on the card so a
 * reboot does not blank the screen until the next report an hour later.
 */
#ifndef NOCT_ZB_HUB_H
#define NOCT_ZB_HUB_H

#include <Arduino.h>

#include "core/Types.h"

class SdStore;
class CardConfig;

class ZbHub {
public:
  /* Start the coordinator. `sd` may be null; names come from the card config.
   * Returns false when the build has no Zigbee (then everything else no-ops). */
  bool begin(SdStore *sd, const CardConfig *cfg);
  /* Copy fresh readings into st.zb and age them. Cheap; call every loop. */
  void tick(unsigned long now, AppState &st);

  /* Open the network so a sensor can join, for `seconds`. A coordinator that
   * is always joinable is a coordinator anyone can join. */
  void permitJoin(int seconds = 180);
  bool joining(unsigned long now) const { return (long)(joinUntil_ - now) > 0; }
  int deviceCount() const;
  bool running() const { return running_; }
  /* One-shot: a sensor reported for the first time (fresh pairing, or first
   * words after a reboot). main() turns it into a toast and a wolf remark. */
  bool takeNewSensor() {
    bool r = newSensor_;
    newSensor_ = false;
    return r;
  }
  /* Forget the network and every paired device — the Zigbee equivalent of the
   * factory reset in the menu. Reboots, because the stack cannot re-form a
   * network in place. */
  void factoryReset();

private:
  void save();
  void restore();

  SdStore *sd_ = nullptr;
  const CardConfig *cfg_ = nullptr;
  bool running_ = false;
  unsigned long joinUntil_ = 0;
  unsigned long lastSave_ = 0;
  unsigned long lastLog_ = 0;
  bool dirty_ = false;
  int knownCount_ = 0;
  bool newSensor_ = false;
};

#endif
