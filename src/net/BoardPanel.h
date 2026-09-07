/*
 * Nocturne C6 — BoardPanel: the board's OWN web page.
 *
 * The companion web panel is served by the PC (control_panel.py on :8899),
 * which means the one time you most want to change a setting from the sofa
 * — the PC is off, the board is showing the room and the wolf — is the one
 * time there is no panel at all. This is the half of it that lives on the
 * board: a small page on port 80 at the address ПЛАТА C6 shows,
 * with the settings that matter at arm's length and the pet's four actions.
 *
 * It is not a second panel to keep in sync. Every control here writes the
 * SAME rc fields the PC's payload writes and raises rcNew, so main() applies
 * them through the one code path that already exists — theme, brightness,
 * pet, looks, all of it — and the PC panel sees the result in the next cfg:
 * line like any other change.
 *
 * Cost, measured for the decision: WebServer is ~11 KB of flash on a
 * 3.62 MB partition and a few KB of heap per open connection. The page is
 * served from flash, the JSON is a few hundred bytes, and handleClient() is
 * refused when free heap drops under a floor — the panel is the least
 * important thing on the board and must be the first to stand aside.
 */
#ifndef NOCT_BOARD_PANEL_H
#define NOCT_BOARD_PANEL_H

#include <Arduino.h>

#include "core/Types.h"

class WebServer;
class WolfPet;
class Achievements;

class BoardPanel {
public:
  void begin(AppState *st, WolfPet *pet, Achievements *ach);
  /* Bring the server up once WiFi has an address; stop it when the owner
   * turns it off. Both idempotent, both cheap to call every loop. */
  void tick(bool wifiUp, bool enabled);
  bool running() const { return running_; }

private:
  void start();
  void stop();
  void handleRoot();
  void handleState();
  void handleSet();
  void handleNotFound();
  String stateJson();

  WebServer *srv_ = nullptr;
  AppState *st_ = nullptr;
  WolfPet *pet_ = nullptr;
  Achievements *ach_ = nullptr;
  bool running_ = false;
};

#endif
