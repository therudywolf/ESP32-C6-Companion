/*
 * Nocturne C6 — LM Studio client (OpenAI-compatible chat completions).
 * Runs in its own FreeRTOS task: network only, NEVER touches SPI. The UI
 * polls the mailbox. Endpoints tried in order (LAN first — the public DDNS
 * name is unreachable from inside the home network, hairpin NAT), sticky on
 * success. gemma-4 is a reasoning model: reasoning_effort "none" keeps the
 * replies fast (~0.4 s warm) and the tokens cheap; a cold call pays the JIT
 * model load (~13 s), hence the generous timeout.
 */
#ifndef NOCT_LLM_CLIENT_H
#define NOCT_LLM_CLIENT_H

#include <Arduino.h>

#include "core/config.h"

class LlmClient {
public:
  void begin(const char *const *endpoints, int endpointCount,
             const char *apiKey, const char *model);

  /* Fire a speech request (drops if one is already in flight).
   * `context` becomes the user message. Returns false if busy/not started. */
  bool request(const String &context);

  bool busy() const { return busy_; }
  bool lastOk() const { return lastOk_; }

  /* Poll: true once per finished request; phrase is UTF-8, sanitized. */
  bool takeReply(String &phrase, bool &ok);

private:
  static void taskEntry(void *self);
  void taskLoop();
  bool callOnce(const char *base, const String &context, String &out);
  static String sanitize(const String &raw);

  const char *const *endpoints_ = nullptr;
  int endpointCount_ = 0;
  int stickyEndpoint_ = 0;
  const char *apiKey_ = nullptr;
  const char *model_ = nullptr;

  TaskHandle_t task_ = nullptr;
  SemaphoreHandle_t mux_ = nullptr;
  String pendingCtx_;
  bool hasPending_ = false;
  String reply_;
  bool replyOk_ = false;
  bool hasReply_ = false;
  volatile bool busy_ = false;
  volatile bool lastOk_ = false;
};

#endif
