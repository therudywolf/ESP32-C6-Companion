#include "net/WifiManager.h"

#include <esp_wifi.h>

void WifiManager::begin(const WifiCred *nets, size_t count, int forcedIdx) {
  count_ = 0;
  for (size_t i = 0; i < count && count_ < NOCT_WIFI_MAX_NETS; i++) {
    if (!nets[i].ssid || !nets[i].ssid[0]) continue;
    nets_[count_++] = nets[i];
  }
  forced_ = (forcedIdx >= 0 && forcedIdx < (int)count_) ? forcedIdx : -1;
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false); /* we own the reconnect policy */
  startScan(millis());
}

void WifiManager::setForced(int idx) {
  forced_ = (idx >= 0 && idx < (int)count_) ? idx : -1;
  if (connected()) WiFi.disconnect();
  phase_ = IDLE;
  lastAttempt_ = 0; /* retry immediately */
}

void WifiManager::startScan(unsigned long now) {
  if (forced_ >= 0) { /* locked: skip the scan, just connect */
    rankLen_ = 1;
    rank_[0] = forced_;
    rankPos_ = 0;
    connectTo(forced_, now);
    return;
  }
  WiFi.scanDelete();
  WiFi.scanNetworks(true /* async */);
  phase_ = SCANNING;
  phaseStart_ = now;
}

void WifiManager::rankAndConnect(unsigned long now) {
  int n = WiFi.scanComplete();
  int bestRssi[NOCT_WIFI_MAX_NETS];
  rankLen_ = 0;
  for (size_t k = 0; k < count_; k++) {
    int best = -127;
    for (int i = 0; i < n; i++) {
      if (WiFi.SSID(i) == nets_[k].ssid && WiFi.RSSI(i) > best)
        best = WiFi.RSSI(i);
    }
    if (best > -127) {
      /* insertion sort by RSSI desc */
      int pos = rankLen_++;
      while (pos > 0 && bestRssi[pos - 1] < best) {
        rank_[pos] = rank_[pos - 1];
        bestRssi[pos] = bestRssi[pos - 1];
        pos--;
      }
      rank_[pos] = (int)k;
      bestRssi[pos] = best;
    }
  }
  WiFi.scanDelete();
  if (rankLen_ == 0) { /* nobody home — try the priority list blind */
    for (size_t k = 0; k < count_; k++) rank_[k] = (int)k;
    rankLen_ = (int)count_;
  }
  rankPos_ = 0;
  Serial.printf("[WIFI] scan: %d candidates, best '%s'\n", rankLen_,
                nets_[rank_[0]].ssid);
  connectTo(rank_[0], now);
}

void WifiManager::connectTo(int idx, unsigned long now) {
  curIdx_ = idx;
  Serial.printf("[WIFI] connecting to '%s'...\n", nets_[idx].ssid);
  WiFi.begin(nets_[idx].ssid, nets_[idx].pass);
  WiFi.setSleep(false); /* USB powered; latency over power */
  esp_wifi_set_ps(WIFI_PS_NONE);
  phase_ = CONNECTING;
  phaseStart_ = now;
  lastAttempt_ = now;
}

void WifiManager::tick(unsigned long now) {
  if (count_ == 0) return;

  if (connected()) {
    phase_ = IDLE;
    wasConnected_ = true;
    return;
  }

  switch (phase_) {
  case SCANNING: {
    int n = WiFi.scanComplete();
    if (n >= 0) {
      rankAndConnect(now);
    } else if (now - phaseStart_ > 12000) { /* scan stuck */
      WiFi.scanDelete();
      phase_ = IDLE;
    }
    break;
  }
  case CONNECTING:
    if (now - phaseStart_ > NOCT_WIFI_CONNECT_TIMEOUT_MS) {
      Serial.printf("[WIFI] '%s' timed out\n", nets_[curIdx_].ssid);
      WiFi.disconnect();
      if (++rankPos_ < rankLen_) {
        connectTo(rank_[rankPos_], now);
      } else {
        phase_ = IDLE; /* exhausted: rescan after the retry interval */
        phaseStart_ = now;
      }
    }
    break;
  case IDLE:
    if (wasConnected_) { /* just dropped */
      wasConnected_ = false;
      Serial.println("[WIFI] disconnected, rescanning");
      startScan(now);
    } else if (now - lastAttempt_ > NOCT_WIFI_RETRY_INTERVAL_MS) {
      lastAttempt_ = now;
      startScan(now);
    }
    break;
  }
}
