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
  /* ИСТОРИЯ scale, read into UiCtx by main so the scene can pick a series.
   * 0 = hour, 1 = day, 2 = the card archive. */
  int historyMode() const { return histMode_; }
  /* ДОМ trend window; main loads the matching series from the card. */
  int homeMode() const { return homeMode_; }
  void setHomeMode(int m) { homeMode_ = (m < 0 || m > 2) ? 0 : m; }
  /* Backlight is owned by main(): it folds the dim state, quiet hours and the
   * user setting into ONE value per frame, so no path can fight another. */
  bool screenDimmed() const { return dimmed_; }
  unsigned long lastInputMs() const { return lastInput_; }
  /* Something outside asked for attention (the alarm): drop the screensaver
   * and restart the idle clock so the panel is bright when you look at it. */
  void wakeScreen() {
    dimmed_ = false;
    lastInput_ = millis();
  }
  /* Should a held button auto-repeat right now? True only in list-like modes
   * (menu, pickers, colour editor) — see Button.h. */
  bool wantsButtonRepeat() const;
  /* Menu asked for a screenshot; main() owns the card, so it consumes this. */
  bool takeZbJoinRequest() {
    bool r = zbJoinRequested_;
    zbJoinRequested_ = false;
    return r;
  }
  /* Ask for a screenshot on the next completed frame. The console uses this
   * too rather than grabbing the sprite itself — see main.cpp. */
  void requestShot() { shotRequested_ = true; }
  /* Push the alert takeover aside for a while — the same thing a long press
   * does, reachable from the console. A hardware alert that legitimately holds
   * the screen makes every other scene unverifiable, which matters when the
   * screenshot IS the test. */
  void snoozeAlert(unsigned long ms) { alertSnoozeUntil_ = millis() + ms; }
  bool takeShotRequest() {
    bool r = shotRequested_;
    shotRequested_ = false;
    return r;
  }
  /* Full-screen OTA takeover: drawn straight to the panel from the update
   * callback, because the normal frame loop is blocked while flash is written. */
  void drawOtaScreen(UiCtx &ui, int pct, const char *msg);

  void bootAnimation(UiCtx &ui); /* blocking, ~3 s, incl. panel test card */

private:
  void gotoScene(int s, UiCtx &ui);
  void drawMenu(UiCtx &ui);
  void drawScreensaver(UiCtx &ui);
  void drawColorEditor(UiCtx &ui);
  void editLoadRole(); /* pull the focused role's RGB out of the live palette */
  void drawScenePicker(UiCtx &ui);
  void drawElemPicker(UiCtx &ui);
  void drawNotifCard(UiCtx &ui); /* the notification flyover */
  void drawGame(UiCtx &ui);      /* the one-button runner */
  void gameReset();
  /* next ring scene after `from` that is enabled in the mask (DEN always ok). */
  int nextVisibleScene(int from, uint32_t mask, bool allowDen,
                       bool pcOffline = false) const;
  void menuAction(UiCtx &ui, int itemId);
  int denActionSel(UiCtx &ui) const;
  bool alertActive(UiCtx &ui) const;
  /* menu model: rows depend on whether we are in the category list or inside
   * one, so every count/label/value comes from ONE table (see kMenu). */
  int menuRowCount() const;
  const char *menuRowName(int row) const;
  void menuRowValue(int row, const Settings &s, char *out, size_t cap) const;
  void menuActivateRow(UiCtx &ui, int row);

  Deps d_{};
  int scene_ = SCENE_DEN;
  unsigned long transStart_ = 0;
  int transDir_ = 1; /* +1 forward (reveal L→R), -1 back (reveal R→L) */

  bool menuOpen_ = false;
  int menuSel_ = 0;
  int menuCat_ = -1; /* -1 = category list, else the category being browsed */
  bool sysInfo_ = false;
  bool achView_ = false;
  /* ── The runner ───────────────────────────────────────────────────────
   * One button, so: press = jump, and that is the entire control scheme.
   * The wolf runs, fences come at it, the score is distance. High score goes
   * to NVS so it means something. */
  bool gameMode_ = false;
  float gameY_ = 0, gameVy_ = 0;   /* wolf offset above the ground, px */
  int gameScore_ = 0, gameBest_ = 0;
  int gameSpeed_ = 0;
  unsigned long gameTick_ = 0, gameOverAt_ = 0;
  static const int kObstacles = 3;
  int obsX_[kObstacles] = {0};
  int obsH_[kObstacles] = {0};
  int histMode_ = 0;     /* ИСТОРИЯ: 0 hour, 1 day, 2 card archive */
  /* ДОМ trend window: 0 last reports (RAM), 1 today, 2 the week (card). */
  int homeMode_ = 0;
  unsigned long resetArmedUntil_ = 0; /* factory reset needs a 2nd confirm */
  bool shotRequested_ = false;
  bool zbJoinRequested_ = false;

  unsigned long alertSnoozeUntil_ = 0;
  int preAlertScene_ = -1;

  bool forzaLatched_ = false;
  int preForzaScene_ = -1;
  int pendingScene_ = -1; /* set by requestScene(), applied in draw() */

  /* media peek: a Spotify play / track change briefly takes over МЕДИА */
  unsigned long mediaPeekUntil_ = 0;
  int preMediaScene_ = -1;
  String lastPeekTrack_;
  bool lastPeekPlaying_ = false;

  /* notification flyover: a server "notif" event flies in over any scene */
  unsigned long notifUntil_ = 0; /* deadline; 0 = not showing */
  unsigned long notifAt_ = 0;    /* when it started (for the slide-in) */
  unsigned long notifDurMs_ = 7000; /* this card's show duration (for countdown) */
  long lastNotifSeq_ = 0;        /* last shown notif seq, to detect new ones */
  bool notifSeeded_ = false;     /* baselined the seq once connected (no replay) */

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
