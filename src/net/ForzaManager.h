/*
 * Nocturne C6 — Forza telemetry listener, ported from Nocturne OS.
 * UDP "Data Out" on port 5300. Auto-detects format by packet length:
 *   FM7 = 311 B, FM8 = 331 B (dash starts at 232),
 *   FH4/FH5 = 323/324 B (12-byte CarCategory block shifts dash to 244).
 * Unknown lengths >= 311 are parsed as Horizon and logged once (FH6 watch).
 * Fire-and-forget from the game; connected = packet within 3 s.
 */
#ifndef NOCT_FORZA_MANAGER_H
#define NOCT_FORZA_MANAGER_H

#include <WiFiUdp.h>

#include "core/config.h"

struct ForzaState {
  bool raceOn = false;
  float rpm = 0, maxRpm = 1, idleRpm = 0;
  float speedKmh = 0;
  int gear = 11; /* 0 = R, 11 = N, 1..10 */
  float slip[4] = {0, 0, 0, 0}; /* FL FR RL RR combined slip */
  uint8_t throttle = 0, brake = 0;
  float fuel = 0; /* 0..1 */
  uint16_t lap = 0;
  uint8_t racePos = 0;
  float accelLat = 0, accelLong = 0;
  float powerW = 0;   /* engine output, watts */
  float boostPsi = 0; /* turbo boost, PSI */
  float bestLap = 0, lastLap = 0, curLap = 0; /* seconds (0 = none) */
  float tireTemp[4] = {0, 0, 0, 0};  /* FL FR RL RR, degrees (FM only) */
  float distanceM = 0;               /* total distance travelled, metres */
  float gforce = 0;                  /* |accel| in g, computed by the manager */
  unsigned long lastPacketMs = 0;
  /* dynamics: stamped when the value changes so the HUD can animate it */
  int posGain = 0;                 /* +N gained / -N lost places on last change */
  unsigned long posChangeMs = 0;   /* when racePos last changed */
  unsigned long bestLapMs = 0;     /* when a new personal best was set */
  /* drift detection (sideways + grip loss + moving), with hysteresis */
  bool drifting = false;
  unsigned long driftMs = 0;       /* start of the current drift */
  unsigned long driftHoldMs = 0;   /* last sideways sample */
  float driftPeakG = 0;            /* peak lateral g of the current drift */
  /* session bests (reset on each race start) */
  float topSpeed = 0, maxG = 0;
  int peakHp = 0;

  float latG() const { return accelLat / 9.80665f; }
  float longG() const { return accelLong / 9.80665f; }

  /* 0..1 of usable rev range (idle..max). */
  float rpmPct() const {
    float span = maxRpm - idleRpm;
    if (span < 1) return 0;
    float p = (rpm - idleRpm) / span;
    return p < 0 ? 0 : (p > 1 ? 1 : p);
  }
};

class ForzaManager {
public:
  void tick(unsigned long now, bool wifiUp);
  bool connected(unsigned long now) const {
    return st_.lastPacketMs &&
           (now - st_.lastPacketMs) < NOCT_FORZA_TIMEOUT_MS;
  }
  const ForzaState &state() const { return st_; }

private:
  void parse(const uint8_t *p, int len);

  WiFiUDP udp_;
  bool bound_ = false;
  ForzaState st_;
  uint8_t buf_[400];
  bool warnedLen_ = false;
  bool lastRaceOn_ = false; /* rising edge resets the session bests */
};

#endif
