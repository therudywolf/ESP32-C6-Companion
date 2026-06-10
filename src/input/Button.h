/*
 * Nocturne C6 — single-button input, ported from Nocturne OS InputHandler.
 * BOOT button GPIO9: external 10K pull-up, active LOW (safe input after
 * boot; never driven). Grammar: SHORT=next, LONG=action, DOUBLE=menu,
 * TRIPLE=home.
 */
#ifndef NOCT_BUTTON_H
#define NOCT_BUTTON_H

#include <Arduino.h>

enum ButtonEvent { EV_NONE, EV_SHORT, EV_LONG, EV_DOUBLE, EV_TRIPLE };

struct InputSystem {
  const int pin;
  unsigned long pressTime = 0;
  unsigned long releaseTime = 0;
  bool btnState = false;
  int clickCount = 0;

  explicit InputSystem(int p) : pin(p) { pinMode(pin, INPUT); }

  ButtonEvent update() {
    bool down = (digitalRead(pin) == LOW);
    unsigned long now = millis();
    ButtonEvent event = EV_NONE;

    if (down && !btnState) {
      btnState = true;
      pressTime = now;
    } else if (!down && btnState) {
      btnState = false;
      unsigned long duration = now - pressTime;
      if (duration > 50 && duration < 500) {
        clickCount++;
        releaseTime = now;
        if (clickCount >= 4) {
          clickCount = 0;
          return EV_TRIPLE;
        }
      } else if (duration >= 500) {
        clickCount = 0;
        return EV_LONG;
      }
    }

    const unsigned long multiTapWindowMs = 300;
    if (clickCount > 0 && !btnState && (now - releaseTime > multiTapWindowMs)) {
      if (clickCount == 1)
        event = EV_SHORT;
      else if (clickCount == 2)
        event = EV_DOUBLE;
      else
        event = EV_TRIPLE;
      clickCount = 0;
    }
    return event;
  }
};

struct IntervalTimer {
  unsigned long intervalMs;
  unsigned long lastMs = 0;
  explicit IntervalTimer(unsigned long interval) : intervalMs(interval) {}
  bool check(unsigned long now) {
    if (now - lastMs >= intervalMs) {
      lastMs = now;
      return true;
    }
    return false;
  }
  void reset(unsigned long now) { lastMs = now; }
};

#endif
