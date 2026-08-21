/*
 * Nocturne C6 — optional overrides from /nocturne.ini on the SD card.
 *
 * WiFi credentials and the server address were compile-time only: they live in
 * secrets.h and get baked into the image, so moving the board to another
 * network meant a rebuild and a cable. With a working card they can be a text
 * file you edit on any laptop.
 *
 * secrets.h stays the source of truth — the card only OVERRIDES, key by key,
 * and anything absent keeps its compiled value. A missing, empty or malformed
 * file changes nothing. Read once at boot, straight after the card mounts and
 * before WiFi starts.
 *
 * The file holds a WiFi password in the clear on removable media. That is the
 * trade for editing it without a toolchain; it is the owner's own card in the
 * owner's own device, but it is worth knowing before putting one in a laptop
 * you don't control.
 */
#ifndef NOCT_CARD_CONFIG_H
#define NOCT_CARD_CONFIG_H

#include <Arduino.h>

#include "core/config.h"
#include "net/WifiManager.h"

class SdStore;

class CardConfig {
public:
  /* Parse /nocturne.ini if present. Returns true when at least one key was
   * applied — main() logs that so an ignored file is never a silent mystery. */
  bool load(SdStore *sd);

  bool loaded() const { return loaded_; }

  /* WiFi networks from the card, or 0 if the file supplied none (keep
   * secrets.h). The returned pointers stay valid for the life of the object. */
  const WifiCred *wifiNets() const { return nets_; }
  int wifiCount() const { return netCount_; }

  /* "" / 0 mean "not overridden". */
  const char *host() const { return host_.c_str(); }
  uint16_t port() const { return port_; }
  uint16_t panelPort() const { return panelPort_; }
  const char *llmEndpoint() const { return llm_.c_str(); }
  const char *llmModel() const { return llmModel_.c_str(); }
  const char *llmKey() const { return llmKey_.c_str(); }
  const char *skin() const { return skin_.c_str(); }
  /* Minutes past midnight, or -1 when no alarm is set. The board has an NTP
   * clock, a light and something that talks — it may as well wake you up. */
  int alarmMinutes() const { return alarm_; }

private:
  void apply(const String &section, const String &key, const String &val);

  /* Backing storage — WifiCred holds bare pointers, so the Strings must
   * outlive it and must not be reallocated after wifiNets() is handed out. */
  String ssid_[NOCT_WIFI_MAX_NETS], pass_[NOCT_WIFI_MAX_NETS];
  WifiCred nets_[NOCT_WIFI_MAX_NETS] = {};
  int netCount_ = 0;
  String host_, llm_, llmModel_, llmKey_, skin_;
  uint16_t port_ = 0, panelPort_ = 0;
  int alarm_ = -1;
  bool loaded_ = false;
  int applied_ = 0;
};

#endif
