#include "core/HourHistory.h"

#include "core/Types.h"

void Histories::accumulate(const HardwareData &hw) {
  sct += hw.ct;
  sgt += hw.gt;
  scl += hw.cl;
  sgl += hw.gl;
  sram += hw.ra > 0.1f ? (int)(hw.ru * 100.0f / hw.ra) : 0;
  n++;
}

void Histories::tick(unsigned long now) {
  if (lastCommit == 0) lastCommit = now;
  if (now - lastCommit < 60000UL) return; /* one sample per minute */
  lastCommit = now;
  if (n == 0) return;
  ct.push((int)(sct / n));
  gt.push((int)(sgt / n));
  cl.push((int)(scl / n));
  gl.push((int)(sgl / n));
  ram.push((int)(sram / n));
  sct = sgt = scl = sgl = sram = 0;
  n = 0;
}
