/*
 * Nocturne C6 — SceneManager: scene ring, one-button navigation, menu,
 * alert takeover, toasts, carousel, screen dim. Owns the frame composition;
 * scenes only draw the content band.
 */
#ifndef NOCT_SCENE_MANAGER_H
#define NOCT_SCENE_MANAGER_H

#include "core/SettingsStore.h"
#include "input/Button.h"
#include "led/StatusLed.h"
#include "net/TelemetryClient.h"
#include "net/WifiManager.h"
#include "ui/Display.h"
#include "ui/Scenes.h"

class SceneManager {
public:
  struct Deps {
    Display *disp;
    WifiManager *wifi;
    TelemetryClient *tcp;
    StatusLed *led;
    const char *wifiNames[NOCT_WIFI_MAX_NETS];
    int wifiCount;
  };

  void begin(const Deps &deps);
  void handleInput(ButtonEvent ev, UiCtx &ui);
  void draw(UiCtx &ui);
  void toast(const String &msg);
  int currentScene() const { return scene_; }
  /* remote-control: jump to a scene on the next draw (companion app). */
  void requestScene(int s) { pendingScene_ = s; }

  void bootAnimation(UiCtx &ui); /* blocking, ~3 s, incl. panel test card */

private:
  void gotoScene(int s, UiCtx &ui);
  void drawMenu(UiCtx &ui);
  void drawScreensaver(UiCtx &ui);
  void menuAction(UiCtx &ui);
  int denActionSel(UiCtx &ui) const;
  bool alertActive(UiCtx &ui) const;

  Deps d_{};
  int scene_ = SCENE_DEN;
  unsigned long transStart_ = 0;

  bool menuOpen_ = false;
  int menuSel_ = 0;
  bool sysInfo_ = false;

  unsigned long alertSnoozeUntil_ = 0;
  int preAlertScene_ = -1;

  bool forzaLatched_ = false;
  int preForzaScene_ = -1;
  int pendingScene_ = -1; /* set by requestScene(), applied in draw() */

  /* DEN action submenu: LONG enters, SHORT cycles, LONG executes */
  bool denActionMode_ = false;
  int denSel_ = 0;
  unsigned long denModeAt_ = 0;

  String toast_;
  unsigned long toastUntil_ = 0;

  unsigned long lastCarousel_ = 0;
  unsigned long lastInput_ = 0;
  bool dimmed_ = false;
};

#endif
