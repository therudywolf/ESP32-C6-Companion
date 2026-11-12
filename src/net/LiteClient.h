/*
 * Nocturne C6 — standalone fallback. When the PC telemetry server is down, the
 * board pulls a reduced payload (weather + forest/services health + clock) from
 * an always-on lite endpoint over HTTPS, on a
 * background task so the render loop never blocks. The main loop feeds the JSON
 * to the existing TelemetryClient parser, so the WEATHER / FOREST / SERVICES
 * scenes stay alive with the PC off. URL+token live in gitignored secrets.h.
 *
 * Concurrency: `payload_` is an Arduino String touched by BOTH the fetch task
 * and the loop task, so every access goes through mux_. A cancel (the PC coming
 * back mid-fetch) bumps gen_ instead of clearing the String underneath the task
 * — assigning to a String reallocates, and doing that from two tasks at once
 * corrupts the heap.
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
  /* Test hook: force one fetch right now, regardless of pcDown. This exercises
   * the exact DNS + TLS path that crashed the Zigbee build (see LwipSafety.cpp),
   * so the fix can be PROVEN from the console instead of waited for. */
  void debugFetchNow();
  bool taskRunning() const { return task_ != nullptr; }

private:
  static void taskEntry(void *self);
  void taskLoop();
  bool fetch(String &out);
  bool startTask();

  String url_;
  String token_;
  volatile bool pending_ = false;
  volatile bool ready_ = false;
  volatile bool ok_ = false;      /* last fetch succeeded */
  volatile uint32_t gen_ = 0;     /* bumped on cancel; a stale result is dropped */
  String payload_;                /* guarded by mux_ */
  SemaphoreHandle_t mux_ = nullptr;
  unsigned long lastReq_ = 0;
  unsigned long downSince_ = 0;   /* 0 = the hub link is up */
  TaskHandle_t task_ = nullptr;
  static const unsigned long kIntervalMs = 30000; /* refresh cadence when PC off */
  static const unsigned long kRetryMs = 8000;     /* faster retry after a failure */
  /* "Down" only after this long: at boot the link is simply not up YET, and a
   * fallback fetch then is a TLS handshake whose answer is thrown away the
   * moment the hub connects a few seconds later. */
  static const unsigned long kGraceMs = 30000;
  /* A TLS handshake needs this much CONTIGUOUS heap. */
  static const size_t kTlsBlock = 46 * 1024;
  static const uint32_t kStackBytes = 12288;
};

#endif
