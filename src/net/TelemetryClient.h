/*
 * Nocturne C6 — TCP telemetry client, ported from Nocturne OS NetManager.
 * Wire-compatible with the running PC server (schema sv 1.0): newline-
 * delimited JSON over plain TCP, server pushes a full payload on connect,
 * device sends HELO / screen:N / cmd:claude / cmd:status.
 */
#ifndef NOCT_TELEMETRY_CLIENT_H
#define NOCT_TELEMETRY_CLIENT_H

#include <WiFi.h>

#include "core/Graphs.h"
#include "core/Types.h"
#include "core/config.h"

class TelemetryClient {
public:
  void setServer(const char *host, uint16_t port) {
    host_ = host;
    port_ = port;
  }
  void tick(unsigned long now, bool wifiUp, AppState &state, Graphs &graphs);
  void sendScreen(int n);
  void sendCmd(const char *cmd); /* "claude" | "status" */
  /* report wolf state upstream so the companion app can show it */
  void sendWolf(int hunger, int joy, int energy, int mood, bool alive,
                bool sleeping, unsigned long ageDays);
  /* report current device settings upstream so the panel mirrors the board */
  void sendCfg(const Settings &s);
  /* feed an externally-fetched payload (the lite fallback) through
   * the same parser, so weather/forest/services scenes work with the PC off */
  void feedExternal(const char *json, AppState &state, Graphs &graphs) {
    if (json && *json) parsePayload(json, strlen(json), state, graphs);
  }
  bool connected() const { return tcpConnected_; }
  bool signalLost(unsigned long now) const;
  bool hasData() const { return firstData_; }
  unsigned long lastPayloadMs() const { return lastUpdate_; }

private:
  void tryConnect(unsigned long now);
  void parsePayload(const char *line, size_t len, AppState &state,
                    Graphs &graphs);

  WiFiClient client_;
  const char *host_ = nullptr;
  uint16_t port_ = 0;
  bool tcpConnected_ = false;
  bool firstData_ = false;
  unsigned long lastAttempt_ = 0;
  unsigned long connectTime_ = 0;
  unsigned long lastUpdate_ = 0;
  int lastSentScreen_ = -1;
  char line_[NOCT_TCP_LINE_MAX];
  size_t lineLen_ = 0;
};

#endif
