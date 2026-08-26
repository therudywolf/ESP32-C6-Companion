#include "net/Presence.h"

#include <WiFi.h>

#include "core/config.h"
#include "net/TelemetryClient.h"
#include "net/WolClient.h"
#include "storage/CardConfig.h"
#include "ui/SceneManager.h"

Presence::Action Presence::tick(unsigned long now, AppState &st,
                                TelemetryClient &tcp, SceneManager &ui) {
  const Settings &s = st.settings;
  if (!s.pcWake) return ACT_NONE;

  /* The freshest motion across every sensor that reports it. A climate
   * sensor answers -1 here and is skipped by that alone — the kind of a
   * device is told by which fields it populates, the same convention the
   * screens use. */
  int age = -1;
  for (int i = 0; i < st.zb.count; i++) {
    int a = st.zb.list[i].motionAgeSec;
    if (a < 0) continue;
    if (age < 0 || a < age) age = a;
  }
  if (age < 0) {
    lastSeenAge_ = -1;
    return ACT_NONE;
  }

  /* Fire on a NEW report, not on a fresh-looking number.
   *
   * motionAgeSec counts up second by second, so "age < 60" is true for a
   * whole minute after one wave of a hand. Acting on that would fire on
   * every loop iteration for sixty seconds. What marks an actual event is
   * the age going DOWN — the only way that happens is a new report. */
  bool newReport = lastSeenAge_ >= 0 && age < lastSeenAge_;
  bool firstEver = lastSeenAge_ < 0 && age <= 5;
  lastSeenAge_ = age;
  if (!newReport && !firstEver) return ACT_NONE;
  if (age > 30) return ACT_NONE; /* a stale report is not an arrival */

  /* The action's own cooldown, measured from the last time it FIRED. Timing
   * it from the last motion instead would let continuous presence push the
   * window out forever, so it would never settle and never fire again. */
  if (lastFire_ && (long)(now - lastFire_) < (long)NOCT_PRESENCE_COOLDOWN_MS)
    return ACT_NONE;

  lastFire_ = now;

  /* The board's own screen comes up regardless. It is the one thing here
   * that is free, reversible and certain. */
  ui.wakeDisplay();

  if (tcp.connected()) {
    /* The PC is awake enough to hold a TCP socket, so a magic packet would
     * do nothing — what is asleep is the monitor, and only something running
     * ON the PC can wake that. */
    tcp.sendCmd("wake");
    lastAct_ = ACT_WAKE_PC;
    Serial.println("[PRES] motion at the desk - asked the PC to wake its "
                   "display");
    return ACT_WAKE_PC;
  }

  /* No link: the PC is off or asleep. This is Wake-on-LAN's case. */
  uint8_t mac[6];
  const char *macStr = cfg_ ? cfg_->pcMac() : "";
  if (!macStr || !*macStr) {
    lastAct_ = ACT_LOCAL;
    Serial.println("[PRES] motion, but no PC mac configured - see `wolmac`");
    return ACT_LOCAL;
  }
  if (!wol::parseMac(macStr, mac)) {
    lastAct_ = ACT_LOCAL;
    Serial.printf("[PRES] motion, but '%s' is not a MAC\n", macStr);
    return ACT_LOCAL;
  }
  if (WiFi.status() != WL_CONNECTED) {
    lastAct_ = ACT_LOCAL;
    Serial.println("[PRES] motion, but no wifi to broadcast on");
    return ACT_LOCAL;
  }

  IPAddress bc = wol::broadcastFor(WiFi.localIP(), WiFi.subnetMask());
  bool sent = wol::send(mac, bc);
  lastAct_ = sent ? ACT_WOL : ACT_LOCAL;
  /* "sent", never "woke". The machine being woken is by definition not
   * listening, so there is no acknowledgement to have — and reporting the
   * packet as a wake-up is what gets debugged for an hour at the wall
   * socket. */
  Serial.printf("[PRES] motion, PC offline - magic packet to %s via %d.%d.%d.%d"
                " %s\n",
                macStr, bc[0], bc[1], bc[2], bc[3],
                sent ? "sent" : "FAILED TO SEND");
  return lastAct_;
}
