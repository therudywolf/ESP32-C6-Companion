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
#include "core/ClimateAnalysis.h"
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
  /* Report a local Zigbee sensor upstream. The board is the coordinator now,
   * so the server side (and through it the Yandex skill) learns the readings
   * from us - the reverse of the zb payload block, one line per sensor. */
  void sendZbSensor(const ZbSensor &z);
  /* Barometric tendency over 3 h, tenths of a hPa. Sent separately from the
   * readings because it is derived from the CARD archive, not from the last
   * report — and because a server-side alert rule needs the rate, not the
   * absolute pressure, to say anything about the weather. */
  void sendZbTrend(int dPress10, int dTemp10, int dHum);
  /* The multi-window analysis: one `zbw:` line with every window, then one
   * `zbpat:` line per finding. Sent together and only when recomputed, so a
   * consumer can treat the pair as one atomic report rather than guessing
   * whether a finding belongs to the numbers it just saw. */
  void sendClimatePatterns(const analysis::Finding *f, int n, int dew10,
                           int pressPct, const analysis::Windows &w);
  /* Archive export: a begin line, then one row per reading, then an end line
   * carrying the total so the receiver can tell a finished transfer from a
   * dropped connection. */
  void sendClimateCsvBegin(int days);
  bool sendClimateCsvRow(const char *row);
  void sendClimateCsvEnd(int rows, bool complete);
  /* Hub state for the panel's "check connection" button: is the
   * coordinator up, on what channel, is the network open for joining, how
   * many sensors, and how long since any of them last spoke. */
  void sendZbStatus(bool up, int channel, int joinLeft, int devices,
                    int lastHeard);
  /* The board's own vitals. It watches a PC all day and had no way to say
   * how IT was doing — die temperature, loop duty cycle, heap, uptime and
   * the restart counters that turn a silent self-heal into a number. */
  void sendBoard(float temp, float tempMax, int load, int fps, int heapFree,
                 int heapMin, int heapLargest, unsigned long uptime,
                 int cpuMhz, int rssi, unsigned long boots,
                 unsigned long faults, const char *reason);
  /* Report the SD card's health upstream. The board is the only thing that can
   * see the card, and "the archive has a hole in it" is discovered months
   * later unless the failures are visible while they happen. */
  void sendSdStats(bool ok, uint32_t clockHz, uint32_t usedMB, uint32_t totalMB,
                   uint32_t writes, uint32_t slow, uint32_t fails, int queue,
                   uint32_t lastMs);
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
  /* All uplink goes through here. The socket is switched to non-blocking after
   * connect, and a line that does not fit the send buffer is DROPPED, whole.
   * The alternative was measured the hard way: with WiFi RX starved (Zigbee
   * coexistence), ACKs stop, the ~5.7 KB send buffer fills in ~70 s of wolf/cfg
   * reports, and the next blocking printf hangs the render loop until the task
   * watchdog kills the board. Telemetry uplink is best-effort by nature. */
  /* False when the line did not go out whole. Callers that push in bulk must
   * back off on false rather than keep shovelling into a full socket. */
  bool sendLine(const char *line);
  void parsePayload(const char *line, size_t len, AppState &state,
                    Graphs &graphs);

  WiFiClient client_;
  const char *host_ = nullptr;
  uint16_t port_ = 0;
  bool tcpConnected_ = false;
  bool firstData_ = false;
  unsigned long lastAttempt_ = 0;
  unsigned int failCount_ = 0; /* consecutive failed connects -> exponential backoff */
  unsigned long connectTime_ = 0;
  unsigned long lastUpdate_ = 0;
  int lastSentScreen_ = -1;
  char line_[NOCT_TCP_LINE_MAX];
  size_t lineLen_ = 0;
};

#endif
