/*
 * Nocturne C6 — rolling sparkline buffers, ported from Nocturne OS
 * RollingGraph (32 samples, max-tracked). Fed by TelemetryClient on every
 * payload; rendered by scene sparklines.
 */
#ifndef NOCT_GRAPHS_H
#define NOCT_GRAPHS_H

#include <Arduino.h>

struct RollingGraph {
  static const int kSamples = 32;
  int v[kSamples] = {0};
  int head = 0;
  int count = 0;

  void push(int val) {
    v[head] = val;
    head = (head + 1) % kSamples;
    if (count < kSamples) count++;
  }
  int at(int i) const { /* i: 0 = oldest */
    int idx = (head - count + i + 2 * kSamples) % kSamples;
    return v[idx];
  }
  int maxVal(int floorMax = 1) const {
    int m = floorMax;
    for (int i = 0; i < count; i++)
      if (v[i] > m) m = v[i];
    return m;
  }
};

struct Graphs {
  RollingGraph cpuLoad, gpuLoad, cpuTemp, gpuTemp, netDown, netUp, ramUsed;
  /* Indoor climate from the Zigbee hub. Not fed by onPayload() — a battery
   * sensor reports every half hour, so these are pushed when a reading
   * actually arrives, and 32 samples then span most of a day. Temperature is
   * stored x10 so half-degree moves survive; humidity is whole percent. */
  RollingGraph zbTemp, zbHum;
  /* The board's own die temperature (x10) and loop duty cycle, one sample a
   * second — 32 s of history, which is the right window for "is it climbing
   * right now" rather than a daily trend. */
  RollingGraph boardTemp, boardLoad;
  void onPayload(const struct HardwareData &hw);
};

#endif
