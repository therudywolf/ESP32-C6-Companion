/*
 * Nocturne C6 — long-window history for the on-device "last hour" graphs.
 * 60 samples, one per minute, each the average of the payloads seen in that
 * minute. ~600 bytes total for five metrics — cheap.
 */
#ifndef NOCT_HOUR_HISTORY_H
#define NOCT_HOUR_HISTORY_H

#include <Arduino.h>

struct HardwareData; /* fwd */

struct HourGraph {
  static const int N = 60; /* minutes shown */
  int16_t v[N] = {0};
  int head = 0;
  int count = 0;
  void push(int val) {
    v[head] = (int16_t)val;
    head = (head + 1) % N;
    if (count < N) count++;
  }
  int at(int i) const { /* i: 0 = oldest */
    return v[(head - count + i + 2 * N) % N];
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

struct Histories {
  HourGraph ct, gt, cl, gl, ram;

  /* accumulate one payload's values into the current minute */
  void accumulate(const HardwareData &hw);
  /* commit the minute-average once 60 s have elapsed (call every loop) */
  void tick(unsigned long now);

private:
  long sct = 0, sgt = 0, scl = 0, sgl = 0, sram = 0;
  int n = 0;
  unsigned long lastCommit = 0;
};

#endif
