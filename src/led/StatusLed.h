/*
 * Nocturne C6 — WS2812B-0807 mood light on GPIO8 (RMT via core rgbLedWrite).
 * Mirrors the wolf/system state: breathing orange idle, red strobe on
 * CRITICAL alert, cyan pulse while an LLM request is in flight, green blip
 * on telemetry (re)connect. Brightness kept low — it's a status pixel, not
 * a flashlight.
 */
#ifndef NOCT_STATUS_LED_H
#define NOCT_STATUS_LED_H

#include <Arduino.h>

class StatusLed {
public:
  enum Mode { OFF, BREATHE, ALERT, LLM, BLIP_OK, FORZA };

  /* 0..1 rev range for FORZA mode coloring. */
  void setForzaPct(float p) { forzaPct_ = p; }

  void begin() { px(0, 0, 0); }

  void setEnabled(bool en) {
    enabled_ = en;
    if (!en) px(0, 0, 0);
  }

  void setMode(Mode m) {
    if (m == mode_) return;
    mode_ = m;
    phase_ = 0;
    if (m == BLIP_OK) blipStart_ = millis();
  }

  void tick(unsigned long now) {
    if (!enabled_) return;
    if (now - lastMs_ < 33) return;
    lastMs_ = now;
    switch (mode_) {
    case OFF:
      px(0, 0, 0);
      break;
    case BREATHE: { /* slow orange breathing, period ~4s */
      phase_ = (phase_ + 1) % 120;
      int tri = phase_ < 60 ? phase_ : 120 - phase_;
      uint8_t v = 2 + tri * 28 / 60; /* 2..30 */
      px(v, v / 2, 0);
      break;
    }
    case ALERT: { /* hard red strobe ~4Hz */
      phase_ = (phase_ + 1) % 8;
      uint8_t v = phase_ < 4 ? 120 : 0;
      px(v, 0, 0);
      break;
    }
    case LLM: { /* cyan thinking pulse, fast */
      phase_ = (phase_ + 1) % 30;
      int tri = phase_ < 15 ? phase_ : 30 - phase_;
      uint8_t v = 4 + tri * 50 / 15;
      px(0, v / 2, v);
      break;
    }
    case BLIP_OK: { /* short green flash then back to breathing */
      if (millis() - blipStart_ > 350) {
        mode_ = BREATHE;
      } else {
        px(0, 60, 10);
      }
      break;
    }
    case FORZA: { /* shift light: green -> yellow -> red -> strobe */
      if (forzaPct_ >= 0.95f) {
        phase_ = (phase_ + 1) % 4;
        uint8_t v = phase_ < 2 ? 160 : 0;
        px(v, 0, 0);
      } else if (forzaPct_ >= 0.80f) {
        px(120, 8, 0);
      } else if (forzaPct_ >= 0.60f) {
        px(90, 60, 0);
      } else {
        uint8_t v = 20 + (uint8_t)(forzaPct_ * 80);
        px(0, v, 0);
      }
      break;
    }
    }
  }

private:
  /* This board's WS2812B-0807 is RGB-ordered; the core's rgbLedWrite targets
   * GRB parts. Swap R/G on the way out so colors mean what they say. */
  static void px(uint8_t r, uint8_t g, uint8_t b) {
    rgbLedWrite(pin_, g, r, b);
  }

  static const int pin_ = 8;
  Mode mode_ = BREATHE;
  bool enabled_ = true;
  int phase_ = 0;
  float forzaPct_ = 0;
  unsigned long lastMs_ = 0;
  unsigned long blipStart_ = 0;
};

#endif
