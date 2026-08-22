#include "net/ClimateAlert.h"

#include "pet/PetBrain.h"
#include "ui/SceneManager.h"

void ClimateAlert::tick(unsigned long now, AppState &st, PetBrain &brain,
                        SceneManager &ui) {
  const Settings &s = st.settings;
  if (!s.zbAlert || st.zb.count <= 0) {
    /* Switched off, or nothing to watch. Drop any latched state so turning it
     * back on starts from what the room is doing NOW rather than replaying an
     * alert from before. */
    tempHigh_ = tempLow_ = humHigh_ = humLow_ = battLow_ = false;
    return;
  }

  const ZbSensor &z = st.zb.list[0];

  /* A sensor that has gone quiet must not clear anything: "no reading" and
   * "the room is fine" are different facts. Hold the latch, say nothing. */
  bool stale = z.ageSec < 0 || z.ageSec > 3600;
  if (stale) return;

  char msg[96];

  /* ── temperature ─────────────────────────────────────────────────────── */
  if (z.temp10 != -32768 && z.temp10 != lastTemp10_) {
    lastTemp10_ = z.temp10;
    int t10 = z.temp10;
    int hi10 = s.zbTempMax * 10, lo10 = s.zbTempMin * 10;

    if (s.zbTempMax < 99) {
      if (!tempHigh_ && t10 > hi10) {
        tempHigh_ = true;
        snprintf(msg, sizeof(msg), "дома жарко: %d.%d, порог %d",
                 t10 / 10, abs(t10 % 10), s.zbTempMax);
        ui.toast(msg);
        Serial.printf("[CLIMATE] high temp %d.%d > %d\n", t10 / 10,
                      abs(t10 % 10), s.zbTempMax);
        brain.notice("дома стало жарче порога - скажи хозяину");
      } else if (tempHigh_ && t10 < hi10 - kTempHystX10) {
        tempHigh_ = false;
        Serial.println("[CLIMATE] temp back in band");
        ui.toast("температура вернулась в норму");
      }
    }
    if (s.zbTempMin > -99) {
      if (!tempLow_ && t10 < lo10) {
        tempLow_ = true;
        snprintf(msg, sizeof(msg), "дома холодно: %d.%d, порог %d",
                 t10 / 10, abs(t10 % 10), s.zbTempMin);
        ui.toast(msg);
        Serial.printf("[CLIMATE] low temp %d.%d < %d\n", t10 / 10,
                      abs(t10 % 10), s.zbTempMin);
        brain.notice("дома стало холоднее порога - забеспокойся");
      } else if (tempLow_ && t10 > lo10 + kTempHystX10) {
        tempLow_ = false;
        Serial.println("[CLIMATE] temp back in band");
        ui.toast("температура вернулась в норму");
      }
    }
  }

  /* ── humidity ────────────────────────────────────────────────────────── */
  if (z.humidity >= 0 && z.humidity != lastHum_) {
    lastHum_ = z.humidity;
    int h = z.humidity;

    if (s.zbHumMax <= 100) {
      if (!humHigh_ && h > s.zbHumMax) {
        humHigh_ = true;
        snprintf(msg, sizeof(msg), "сыро: %d%%, порог %d%%", h, s.zbHumMax);
        ui.toast(msg);
        Serial.printf("[CLIMATE] high humidity %d%% > %d%%\n", h, s.zbHumMax);
        brain.notice("дома слишком сыро - это к плесени, предупреди");
      } else if (humHigh_ && h < s.zbHumMax - kHumHyst) {
        humHigh_ = false;
        Serial.println("[CLIMATE] humidity back in band");
        ui.toast("влажность вернулась в норму");
      }
    }
    if (s.zbHumMin >= 0) {
      if (!humLow_ && h < s.zbHumMin) {
        humLow_ = true;
        snprintf(msg, sizeof(msg), "сухо: %d%%, порог %d%%", h, s.zbHumMin);
        ui.toast(msg);
        Serial.printf("[CLIMATE] low humidity %d%% < %d%%\n", h, s.zbHumMin);
        brain.notice("дома слишком сухой воздух - посоветуй увлажнить");
      } else if (humLow_ && h > s.zbHumMin + kHumHyst) {
        humLow_ = false;
        Serial.println("[CLIMATE] humidity back in band");
        ui.toast("влажность вернулась в норму");
      }
    }
  }

  /* ── battery ─────────────────────────────────────────────────────────── */
  /* A coin cell moves a few percent a WEEK, so an edge-triggered warning that
   * the owner dismisses is gone for good — and the sensor dies silently three
   * weeks later. This one nags instead, once a day, until the cell is
   * changed. */
  if (s.zbBattMin > 0 && z.battery >= 0) {
    if (z.battery <= s.zbBattMin) {
      if (!battLow_ || (long)(now - battNagUntil_) > 0) {
        battLow_ = true;
        battNagUntil_ = now + 24UL * 3600UL * 1000UL;
        snprintf(msg, sizeof(msg), "батарея датчика %d%%", z.battery);
        ui.toast(msg);
        Serial.printf("[CLIMATE] battery %d%% <= %d%%\n", z.battery,
                      s.zbBattMin);
        brain.notice("у датчика климата садится батарейка - напомни сменить");
      }
    } else if (battLow_ && z.battery > s.zbBattMin + 5) {
      battLow_ = false; /* cell changed */
    }
  }
}
