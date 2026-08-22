#include "net/ClimateAlert.h"

#include "core/Barometer.h"
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

  /* ── the barometer ───────────────────────────────────────────────────── */
  /* Not gated on zbAlert: the climate thresholds are the owner's numbers for
   * their room, while this is the sky doing something. It also fires far more
   * rarely — a front is a couple of times a week, not a couple of times an
   * hour — so it does not need the same restraint. */
  if (st.zbTrendOk) {
    int tend = (int)barometer::classify(st.zbPress10Delta3h, 3);
    bool interesting = tend != (int)barometer::TEND_STEADY &&
                       tend != (int)barometer::TEND_UNKNOWN &&
                       tend != (int)barometer::TEND_FALL_SLOW &&
                       tend != (int)barometer::TEND_RISE_SLOW;
    /* Six hours of quiet after any announcement — a needle hovering on a
     * threshold would otherwise re-announce every time it crossed back and
     * forth, which is exactly what a needle on a threshold does.
     *
     * BUT the quiet window must never swallow an ESCALATION. "Slowly falling"
     * at noon and a storm at two o'clock is precisely the sequence worth
     * hearing, and suppressing the second because the first was recent would
     * silence the alert exactly when it matters. Distance from steady is the
     * severity, so anything further out than the last announcement gets
     * through regardless. */
    int sev = tend - (int)barometer::TEND_STEADY;
    if (sev < 0) sev = -sev;
    int lastSev = lastTend_ ? lastTend_ - (int)barometer::TEND_STEADY : 0;
    if (lastSev < 0) lastSev = -lastSev;
    bool quiet = tendQuietUntil_ != 0 && (long)(now - tendQuietUntil_) <= 0;
    if (interesting && tend != lastTend_ && (!quiet || sev > lastSev)) {
      lastTend_ = tend;
      tendQuietUntil_ = now + 6UL * 3600UL * 1000UL;
      int dp = st.zbPress10Delta3h;
      snprintf(msg, sizeof(msg), "%+d.%d гПа/3ч - %s", dp / 10, abs(dp % 10),
               barometer::forecast((barometer::Tendency)tend));
      ui.toast(msg);
      Serial.printf("[BARO] alert: %+d.%d hPa/3h - %s\n", dp / 10,
                    abs(dp % 10),
                    barometer::forecast((barometer::Tendency)tend));
      switch ((barometer::Tendency)tend) {
      case barometer::TEND_FALL_FAST:
        /* The association between a fast-falling barometer and migraine is
         * documented and tied to the RATE of the drop. Say what the pressure
         * did and who tends to notice it; do not diagnose the owner. */
        brain.notice("давление резко падает - идет непогода, и на таких "
                     "перепадах у метеочувствительных болит голова; "
                     "предупреди по-доброму");
        break;
      case barometer::TEND_FALL:
        brain.notice("давление падает - к дождю, посоветуй взять зонт");
        break;
      case barometer::TEND_RISE_FAST:
        brain.notice("давление резко растет - небо расчистится и "
                     "похолодает, особенно ночью");
        break;
      case barometer::TEND_RISE:
        brain.notice("давление растет - погода налаживается, порадуйся");
        break;
      default:
        break;
      }
    }
    /* Back to steady: re-arm so the next front is heard even inside the quiet
     * window. A settled needle is the end of the event, not a new one. */
    if (tend == (int)barometer::TEND_STEADY && lastTend_ != 0) {
      lastTend_ = 0;
      tendQuietUntil_ = 0;
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
