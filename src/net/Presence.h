/*
 * Nocturne C6 — presence: what to do when somebody walks up to the desk.
 *
 * ── WHAT THE HARDWARE CAN AND CANNOT TELL YOU ────────────────────────────
 *
 * A PIR reports MOTION IN ITS CONE. It does not measure distance, and the
 * RTCGQ11LM's cone is 170 degrees out to seven metres — which is most of a
 * room. "Somebody came close to the PC" is therefore not something the sensor
 * knows; it is something the INSTALLATION knows, and only if the sensor is
 * aimed so that its cone covers the desk and little else.
 *
 * That is a physical fact this file cannot fix in software, so it does not
 * pretend to. What it does instead is refuse to act on a signal it has no
 * business trusting: nothing fires unless motion is FRESH, and the caller
 * chooses which sensor counts.
 *
 * The second hardware fact: the device has a 60-second retrigger lockout. It
 * cannot report twice inside a minute, so nothing here can react faster than
 * that, and any design that needs to is the wrong design for this sensor.
 *
 * ── WHY THE COOLDOWN IS MUCH LONGER THAN THE SENSOR'S ────────────────────
 *
 * Waking a PC is intrusive and, unlike a screen dimming, it cannot be undone
 * by waiting. Somebody crossing the room three times while carrying laundry
 * must not send three magic packets. So the ACTION has its own cooldown, far
 * longer than the sensor's own, and it is measured from when the action fired
 * rather than from the last motion — otherwise continuous presence keeps
 * pushing the window out and it never settles.
 *
 * ── AND WHY IT IS OFF UNTIL ASKED ────────────────────────────────────────
 *
 * A default that wakes the owner's computer because somebody walked past is
 * not a default, it is a surprise. Everything here stays inert until the
 * setting is on and a MAC has been configured.
 */
#ifndef NOCT_PRESENCE_H
#define NOCT_PRESENCE_H

#include <Arduino.h>

#include "core/Types.h"

class TelemetryClient;
class CardConfig;
class SceneManager;

class Presence {
public:
  /* What the board did about it, so the caller can say so honestly rather
   * than claiming the PC woke up — which is never knowable from here. */
  enum Action {
    ACT_NONE = 0,
    ACT_WOL,     /* magic packet sent; whether anything woke is unknowable */
    ACT_WAKE_PC, /* asked the server to wake the display */
    ACT_LOCAL,   /* only the board's own screen came up */
  };

  void begin(const CardConfig *cfg) { cfg_ = cfg; }

  /* Call once per loop. `motionAgeSec` is the freshest motion across the
   * sensors the owner nominated, -1 when there is none. */
  Action tick(unsigned long now, AppState &st, TelemetryClient &tcp,
              SceneManager &ui);

  unsigned long lastFired() const { return lastFire_; }
  Action lastAction() const { return lastAct_; }

private:
  const CardConfig *cfg_ = nullptr;
  unsigned long lastFire_ = 0;
  int lastSeenAge_ = -1;
  Action lastAct_ = ACT_NONE;
};

#endif
