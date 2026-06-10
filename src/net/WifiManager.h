/*
 * Nocturne C6 — WiFi: scan known SSIDs, rank by RSSI, connect strongest;
 * walk down the ranking on failure; rescan when exhausted. NVS "netSel"
 * (-1 = auto) force-locks one network. Re-ranks only on disconnect — an
 * established TCP session beats 5 dB of RSSI.
 */
#ifndef NOCT_WIFI_MANAGER_H
#define NOCT_WIFI_MANAGER_H

#include <WiFi.h>

#include "core/config.h"

struct WifiCred {
  const char *ssid;
  const char *pass;
};

class WifiManager {
public:
  void begin(const WifiCred *nets, size_t count, int forcedIdx);
  void tick(unsigned long now);
  bool connected() const { return WiFi.status() == WL_CONNECTED; }
  int rssi() const { return connected() ? WiFi.RSSI() : 0; }
  const char *ssid() const {
    return curIdx_ >= 0 ? nets_[curIdx_].ssid : "";
  }
  int currentIndex() const { return curIdx_; }
  void setForced(int idx); /* -1 = auto */

private:
  enum Phase { IDLE, SCANNING, CONNECTING };
  void startScan(unsigned long now);
  void rankAndConnect(unsigned long now);
  void connectTo(int idx, unsigned long now);

  WifiCred nets_[NOCT_WIFI_MAX_NETS];
  size_t count_ = 0;
  int forced_ = -1;
  int curIdx_ = -1;
  int rank_[NOCT_WIFI_MAX_NETS]; /* candidate order after scan */
  int rankLen_ = 0;
  int rankPos_ = 0;
  Phase phase_ = IDLE;
  unsigned long phaseStart_ = 0;
  unsigned long lastAttempt_ = 0;
  bool wasConnected_ = false;
};

#endif
