#include "net/ForzaManager.h"

#include <math.h>

static float rdF(const uint8_t *p, int off) {
  float f;
  memcpy(&f, p + off, 4);
  return f;
}
static int32_t rdI32(const uint8_t *p, int off) {
  int32_t v;
  memcpy(&v, p + off, 4);
  return v;
}
static uint16_t rdU16(const uint8_t *p, int off) {
  uint16_t v;
  memcpy(&v, p + off, 2);
  return v;
}

void ForzaManager::tick(unsigned long now, bool wifiUp) {
  if (!wifiUp) {
    if (bound_) {
      udp_.stop();
      bound_ = false;
    }
    return;
  }
  if (!bound_) {
    bound_ = udp_.begin(NOCT_FORZA_UDP_PORT);
    if (bound_) Serial.println("[FORZA] UDP listening on 5300");
    return;
  }
  int len;
  while ((len = udp_.parsePacket()) > 0) {
    if (len > (int)sizeof(buf_)) {
      udp_.clear();
      continue;
    }
    int n = udp_.read(buf_, sizeof(buf_));
    if (n >= NOCT_FORZA_MIN_PACKET) {
      uint8_t prevPos = st_.racePos;   /* snapshot before parse overwrites */
      float prevBest = st_.bestLap;
      parse(buf_, n);
      st_.lastPacketMs = now;
      /* dynamics: detect a place change and a new personal-best lap */
      if (st_.racePos > 0 && prevPos > 0 && st_.racePos != prevPos) {
        st_.posGain = (int)prevPos - (int)st_.racePos; /* +N = gained places */
        st_.posChangeMs = now;
      }
      if (st_.bestLap > 0.01f && prevBest > 0.01f &&
          st_.bestLap < prevBest - 0.005f)
        st_.bestLapMs = now;

      /* reset session bests on a fresh race */
      if (st_.raceOn && !lastRaceOn_) {
        st_.topSpeed = st_.maxG = 0;
        st_.peakHp = 0;
      }
      lastRaceOn_ = st_.raceOn;

      /* G-force + drift detection (sideways, grip loss, moving) */
      st_.gforce = sqrtf(st_.accelLat * st_.accelLat +
                         st_.accelLong * st_.accelLong) /
                   9.80665f;
      float rearSlip = (st_.slip[2] + st_.slip[3]) * 0.5f;
      bool sideways = fabsf(st_.latG()) > 0.55f && rearSlip > 0.55f &&
                      st_.speedKmh > 25.0f;
      if (sideways) {
        if (!st_.drifting) {
          st_.drifting = true;
          st_.driftMs = now;
          st_.driftPeakG = 0;
        }
        if (st_.gforce > st_.driftPeakG) st_.driftPeakG = st_.gforce;
        st_.driftHoldMs = now;
      } else if (st_.drifting && now - st_.driftHoldMs > 700) {
        st_.drifting = false; /* hysteresis: keep drift state 0.7 s after */
      }

      /* session bests */
      if (st_.speedKmh > st_.topSpeed) st_.topSpeed = st_.speedKmh;
      if (st_.gforce > st_.maxG) st_.maxG = st_.gforce;
      int hp = (int)(st_.powerW / 745.7f);
      if (hp > st_.peakHp) st_.peakHp = hp;
    }
  }
}

void ForzaManager::parse(const uint8_t *p, int len) {
  st_.raceOn = rdI32(p, 0) > 0;
  if (!st_.raceOn) return;

  /* sled block — format-independent */
  st_.maxRpm = rdF(p, 8);
  st_.idleRpm = rdF(p, 12);
  st_.rpm = rdF(p, 16);
  st_.accelLat = rdF(p, 20);
  st_.accelLong = rdF(p, 28);
  for (int i = 0; i < 4; i++) {
    float s = rdF(p, 180 + i * 4);
    st_.slip[i] = s < 0 ? 0 : (s > 50 ? 50 : s);
  }
  if (st_.maxRpm < 1) st_.maxRpm = 1;

  /* dash block — Horizon formats carry a 12-byte insert at 232 */
  bool horizon = (len >= 320 && len <= 326);
  if (!horizon && len != 311 && len != 331 && !warnedLen_) {
    warnedLen_ = true;
    Serial.printf("[FORZA] unknown packet len %d — parsing as %s\n", len,
                  len > 311 ? "horizon" : "motorsport");
    horizon = len > 311 && len != 331;
  }
  int d = horizon ? 12 : 0;

  st_.speedKmh = rdF(p, 244 + d) * 3.6f;
  st_.powerW = rdF(p, 248 + d);
  /* tyre temps FL/FR/RL/RR @256..268 (FM sends real values, FH ~ ambient) */
  for (int i = 0; i < 4; i++) st_.tireTemp[i] = rdF(p, 256 + i * 4 + d);
  st_.boostPsi = rdF(p, 272 + d);
  st_.distanceM = rdF(p, 280 + d);
  float fuel = rdF(p, 276 + d);
  st_.fuel = fuel < 0 ? 0 : (fuel > 1 ? 1 : fuel);
  /* lap times (seconds): BestLap@284 LastLap@288 CurrentLap@292 */
  st_.bestLap = rdF(p, 284 + d);
  st_.lastLap = rdF(p, 288 + d);
  st_.curLap = rdF(p, 292 + d);
  st_.lap = rdU16(p, 300 + d);
  st_.racePos = p[302 + d];
  st_.throttle = p[303 + d];
  st_.brake = p[304 + d];
  st_.gear = p[307 + d];
}
