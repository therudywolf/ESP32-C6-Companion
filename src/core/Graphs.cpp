#include "core/Graphs.h"

#include "core/Types.h"

void Graphs::onPayload(const HardwareData &hw) {
  cpuLoad.push(hw.cl);
  gpuLoad.push(hw.gl);
  cpuTemp.push(hw.ct);
  gpuTemp.push(hw.gt);
  netDown.push(hw.nd);
  netUp.push(hw.nu);
  ramUsed.push(hw.ra > 0.1f ? (int)(hw.ru * 100.0f / hw.ra) : 0);
}
