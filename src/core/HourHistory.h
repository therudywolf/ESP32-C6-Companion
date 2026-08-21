/*
 * Nocturne C6 — long-window history for the on-device graphs.
 *  hour view: 60 samples, one per minute (average of the payloads that minute)
 *  day  view: 24 samples, one per hour  (average of that hour's minutes)
 * ~1.2 KB for five metrics in both scales — cheap.
 *
 * The series survive a reboot: an always-mounted device that heals itself with
 * a watchdog reset should not also wipe the graph you were looking at. The
 * snapshot goes to /logs/hist.bin and is only adopted if it is fresh enough
 * (see kMaxGap*), so a long outage starts clean instead of drawing a lie.
 */
#ifndef NOCT_HOUR_HISTORY_H
#define NOCT_HOUR_HISTORY_H

#include <Arduino.h>

struct HardwareData; /* fwd */
class SdStore;       /* fwd */

struct HourGraph {
  static const int N = 60; /* slots allocated */
  int16_t v[N] = {0};
  int head = 0;
  int count = 0;
  int cap = N; /* slots actually used (60 = per-minute, 24 = per-hour) */

  void setCap(int c) {
    cap = (c > 0 && c <= N) ? c : N;
    head = 0;
    count = 0;
  }
  void push(int val) {
    v[head] = (int16_t)val;
    head = (head + 1) % cap;
    if (count < cap) count++;
  }
  int at(int i) const { /* i: 0 = oldest */
    return v[(head - count + i + 2 * cap) % cap];
  }
  int maxVal(int floorMax = 1) const {
    int m = floorMax;
    for (int i = 0; i < count; i++)
      if (v[i] > m) m = v[i];
    return m;
  }
  int minVal() const {
    if (count == 0) return 0;
    int m = 32767;
    for (int i = 0; i < count; i++)
      if (v[i] < m) m = v[i];
    return m;
  }
  int now() const { return count ? at(count - 1) : 0; }
};

/* One scale's worth of series. */
struct GraphSet {
  HourGraph ct, gt, cl, gl, ram;
};

struct Histories {
  GraphSet hour; /* 60 x 1 min */
  GraphSet day;  /* 24 x 1 h   */

  Histories();
  void attach(SdStore *sd) { sd_ = sd; }
  /* accumulate ONE payload's values (call per payload, not per frame) */
  void accumulate(const HardwareData &hw);
  /* commit the minute/hour averages and flush the snapshot (call every loop) */
  void tick(unsigned long now);

private:
  void commitMinute();
  void commitHour();
  void save();
  void tryRestore();

  long sct = 0, sgt = 0, scl = 0, sgl = 0, sram = 0;
  int n = 0;                       /* payloads in the current minute */
  long hct = 0, hgt = 0, hcl = 0, hgl = 0, hram = 0;
  int hn = 0;                      /* minutes in the current hour */
  unsigned long lastCommit = 0;
  unsigned long lastSave = 0;
  SdStore *sd_ = nullptr;
  bool restored_ = false;          /* restore is attempted exactly once */
  bool savedOnce_ = false;         /* log the first successful save, then hush */
};

#endif
