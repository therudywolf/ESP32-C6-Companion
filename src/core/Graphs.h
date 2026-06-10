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
  void onPayload(const struct HardwareData &hw);
};

#endif
