/*
 * Nocturne C6 — single-button input, ported from Nocturne OS InputHandler.
 * BOOT button GPIO9: external 10K pull-up, active LOW (safe input after
 * boot; never driven). Grammar: SHORT=next, LONG=action, DOUBLE=menu,
 * TRIPLE=home.
 *
 * Hold-to-repeat (EV_REPEAT) is OPT-IN: SceneManager calls setRepeatEnabled()
 * for list-like contexts only (menu, pickers, colour editor). Everywhere else
 * the grammar is byte-for-byte what it was — which matters, because "hold" is
 * already spoken for by LONG, and silently turning a 2-second hold in ЛОГОВО
 * into a repeat stream would break feeding the wolf. When repeat IS enabled,
 * a hold past NOCT_BTN_REPEAT_DELAY_MS emits EV_REPEAT every
 * NOCT_BTN_REPEAT_MS and suppresses the LONG that release would otherwise fire.
 */
#ifndef NOCT_BUTTON_H
#define NOCT_BUTTON_H

#include <Arduino.h>

#include "core/config.h"

enum ButtonEvent { EV_NONE, EV_SHORT, EV_LONG, EV_DOUBLE, EV_TRIPLE, EV_REPEAT };

struct InputSystem {
  const int pin;
  unsigned long pressTime = 0;
  unsigned long releaseTime = 0;
  bool btnState = false;
  int clickCount = 0;
  bool repeatEnabled = false;   /* set per frame by the UI */
  bool repeating = false;       /* this hold has already emitted repeats */
  unsigned long lastRepeat = 0;

  explicit InputSystem(int p) : pin(p) { pinMode(pin, INPUT); }

  /* The UI declares whether a hold should auto-repeat in the CURRENT context. */
  void setRepeatEnabled(bool en) {
    repeatEnabled = en;
    if (!en) repeating = false;
  }

  ButtonEvent update() {
    bool down = (digitalRead(pin) == LOW);
    unsigned long now = millis();
    ButtonEvent event = EV_NONE;

    if (down && !btnState) {
      btnState = true;
      pressTime = now;
      repeating = false;
    } else if (down && btnState) {
      /* held: emit repeats only where the UI asked for them */
      if (repeatEnabled && now - pressTime >= NOCT_BTN_REPEAT_DELAY_MS &&
          now - lastRepeat >= NOCT_BTN_REPEAT_MS) {
        lastRepeat = now;
        repeating = true;
        clickCount = 0; /* a hold is not a tap sequence */
        return EV_REPEAT;
      }
    } else if (!down && btnState) {
      btnState = false;
      unsigned long duration = now - pressTime;
      if (repeating) {
        repeating = false; /* the hold already spoke; release adds nothing */
        return EV_NONE;
      }
      if (duration > 50 && duration < NOCT_BTN_LONG_MS) {
        clickCount++;
        releaseTime = now;
        if (clickCount >= 4) {
          clickCount = 0;
          return EV_TRIPLE;
        }
      } else if (duration >= NOCT_BTN_LONG_MS) {
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
