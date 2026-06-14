/*
 * Nocturne C6 — WS2812B-0807 mood light on GPIO8 (RMT via core rgbLedWrite).
 * A real status channel, not a power LED:
 *   ALERT  hard red strobe            FORZA  rpm zones + shift strobe
 *   LLM    cyan thinking pulse        SPEAK  magenta glow while talking
 *   NOLINK blue double-blink          WARNP  amber pulse (temps in warn zone)
 *   BREATHE mood-colored breathing (wolf happy=yellow, ok=amber, sad=blue,
 *           asleep=dim deep blue)
 */
#ifndef NOCT_STATUS_LED_H
#define NOCT_STATUS_LED_H

#include <Arduino.h>

class StatusLed {
public:
  enum Mode { OFF, BREATHE, ALERT, LLM, BLIP_OK, FORZA, SPEAK, NOLINK, WARNP };

  void setForzaPct(float p) { forzaPct_ = p; }
  void setMoodColor(uint8_t r, uint8_t g, uint8_t b) {
    moodR_ = r;
    moodG_ = g;
    moodB_ = b;
  }
  /* brief solid-colour flash that overrides any mode (overtake blip, spikes) */
  void flash(uint8_t r, uint8_t g, uint8_t b, unsigned long ms) {
    flashR_ = r;
    flashG_ = g;
    flashB_ = b;
    flashUntil_ = millis() + ms;
  }

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
    if (now < flashUntil_) { /* override everything for the flash window */
      px(flashR_, flashG_, flashB_);
      return;
    }
    switch (mode_) {
    case OFF:
      px(0, 0, 0);
      break;
    case BREATHE: { /* mood-colored breathing, period ~4s */
      phase_ = (phase_ + 1) % 120;
      int tri = phase_ < 60 ? phase_ : 120 - phase_;
      int v = 8 + tri * 92 / 60; /* 8..100 scale % */
      px((uint8_t)(moodR_ * v / 255), (uint8_t)(moodG_ * v / 255),
         (uint8_t)(moodB_ * v / 255));
      break;
    }
    case ALERT: { /* hard red strobe ~4Hz */
      phase_ = (phase_ + 1) % 8;
      uint8_t v = phase_ < 4 ? 160 : 0;
      px(v, 0, 0);
      break;
    }
    case LLM: { /* cyan thinking pulse, fast */
      phase_ = (phase_ + 1) % 30;
      int tri = phase_ < 15 ? phase_ : 30 - phase_;
      uint8_t v = 6 + tri * 70 / 15;
      px(0, (uint8_t)(v * 3 / 4), v);
      break;
    }
    case SPEAK: { /* magenta glow while the wolf talks */
      phase_ = (phase_ + 1) % 45;
      int tri = phase_ < 23 ? phase_ : 45 - phase_;
      uint8_t v = 12 + tri * 60 / 23;
      px(v, 0, (uint8_t)(v * 4 / 5));
      break;
    }
    case NOLINK: { /* lonely blue double-blink every ~2s */
      phase_ = (phase_ + 1) % 60;
      bool on = (phase_ < 4) || (phase_ >= 8 && phase_ < 12);
      px(0, 0, on ? 90 : 0);
      break;
    }
    case WARNP: { /* amber pulse ~1Hz: something is getting hot */
      phase_ = (phase_ + 1) % 30;
      int tri = phase_ < 15 ? phase_ : 30 - phase_;
      uint8_t v = 10 + tri * 110 / 15;
      px(v, (uint8_t)(v / 2), 0);
      break;
    }
    case BLIP_OK: { /* short green flash then back to breathing */
      if (millis() - blipStart_ > 350) {
        mode_ = BREATHE;
      } else {
        px(0, 70, 20);
      }
      break;
    }
    case FORZA: { /* shift light: green -> yellow -> red -> strobe */
      if (forzaPct_ >= 0.95f) {
        phase_ = (phase_ + 1) % 4;
        uint8_t v = phase_ < 2 ? 180 : 0;
        px(v, 0, 0);
      } else if (forzaPct_ >= 0.80f) {
        px(140, 10, 0);
      } else if (forzaPct_ >= 0.60f) {
        px(110, 80, 0);
      } else {
        uint8_t v = 25 + (uint8_t)(forzaPct_ * 90);
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
  uint8_t moodR_ = 252, moodG_ = 238, moodB_ = 10;
  unsigned long lastMs_ = 0;
  unsigned long blipStart_ = 0;
  unsigned long flashUntil_ = 0;
  uint8_t flashR_ = 0, flashG_ = 0, flashB_ = 0;
};

#endif
