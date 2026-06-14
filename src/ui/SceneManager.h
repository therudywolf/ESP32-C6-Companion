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
  void drawColorEditor(UiCtx &ui);
  void editLoadRole(); /* pull the focused role's RGB out of the live palette */
  void drawScenePicker(UiCtx &ui);
  void drawElemPicker(UiCtx &ui);
  /* next ring scene after `from` that is enabled in the mask (DEN always ok). */
  int nextVisibleScene(int from, uint32_t mask, bool allowDen) const;
  void menuAction(UiCtx &ui);
  int denActionSel(UiCtx &ui) const;
  bool alertActive(UiCtx &ui) const;

  Deps d_{};
  int scene_ = SCENE_DEN;
  unsigned long transStart_ = 0;
  int transDir_ = 1; /* +1 forward (reveal L→R), -1 back (reveal R→L) */

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
  unsigned long sceneOsdAt_ = 0; /* scene-change name OSD timer */
  bool dimmed_ = false;

  /* on-device colour editor (10 roles x R/G/B, saves to a theme slot) */
  bool editMode_ = false;
  int editRole_ = 0;       /* 0..9 colour role under the cursor */
  bool editChan_ = false;  /* false = choosing a role, true = tuning channels */
  int editCh_ = 0;         /* 0=R 1=G 2=B */
  int editR_ = 0, editG_ = 0, editB_ = 0; /* focused role's working channels */

  /* on-device screen-composition picker (which scenes are in the ring) */
  bool scenePickMode_ = false;
  int scenePickSel_ = SCENE_DASH; /* 1..SCENE_FORZA-1 */

  /* on-device element-composition picker (which widget classes show) */
  bool elemPickMode_ = false;
  int elemPickSel_ = 0; /* 0..UI_ELEM_COUNT-1 */
};

#endif
