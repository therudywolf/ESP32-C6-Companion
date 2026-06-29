/*
 * Nocturne C6 — standalone fallback. When the PC telemetry server is down, the
 * board pulls a reduced payload (weather + forest/services health + clock) from
 * an always-on lite endpoint over HTTPS, on a
 * background task so the render loop never blocks. The main loop feeds the JSON
 * to the existing TelemetryClient parser, so the WEATHER / FOREST / SERVICES
 * scenes stay alive with the PC off. URL+token live in gitignored secrets.h.
 */
#ifndef NOCT_LITE_CLIENT_H
#define NOCT_LITE_CLIENT_H

#include <Arduino.h>

class LiteClient {
public:
  void begin(const char *url, const char *token);
  /* call every loop; only fetches while pcDown, at most every kIntervalMs */
  void tick(unsigned long now, bool pcDown);
  /* moves a freshly-fetched payload into `out` and returns true (consumes it) */
  bool take(String &out);

private:
  static void taskEntry(void *self);
  void taskLoop();
  bool fetch(String &out);

  String url_;
  String token_;
  volatile bool pending_ = false;
  volatile bool ready_ = false;
  volatile bool ok_ = false; /* last fetch succeeded */
  String payload_;
  unsigned long lastReq_ = 0;
  TaskHandle_t task_ = nullptr;
  static const unsigned long kIntervalMs = 30000; /* refresh cadence when PC off */
  static const unsigned long kRetryMs = 8000;     /* faster retry after a failure */
};

#endif
