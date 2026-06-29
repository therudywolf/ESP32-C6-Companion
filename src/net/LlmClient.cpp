#include "net/LlmClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "core/TextUtil.h"

/* Natural-language persona — verified on the live model to produce coherent,
 * in-character one-liners (the old cryptic "состояние=N" context made the
 * small model ramble). gemma-4-e2b is a reasoning model: thoughts route to
 * reasoning_content (separateReasoningContentInAPI), content arrives clean;
 * budget ~800 tokens covers the thinking. NOTE: gemma-4-12b was tested and
 * REJECTED — it reasons 6000+ chars and takes 40-54s for one line. */
static const char *kSystemPrompt =
    "Ты — Ноктюрн, домашний волк-компаньон. Ты живёшь в маленьком экране на "
    "столе хозяина и приглядываешь за его компьютером и его жизнью. Характер: "
    "умный, с лёгкой иронией, преданный, иногда ворчливый — как старый друг. "
    "Говоришь живым разговорным русским от первого лица. ВСЕГДА называй "
    "конкретную вещь, о которой говоришь — точное имя программы, процесса, "
    "трека или события — а не общими словами (что-то, какая-то программа "
    "запрещены). Отвечай РОВНО одной репликой длиной 5-14 слов. Без кавычек, "
    "без эмодзи, без описания действий — только сама фраза.";

void LlmClient::begin(const char *const *endpoints, int endpointCount,
                      const char *apiKey, const char *model,
                      const char *modelBig) {
  endpoints_ = endpoints;
  endpointCount_ = endpointCount;
  apiKey_ = apiKey;
  model_ = model;
  modelBig_ = modelBig;
  mux_ = xSemaphoreCreateMutex();
  xTaskCreate(taskEntry, "llm", 8192, this, 1, &task_);
}

bool LlmClient::request(const String &context, bool big) {
  if (!task_ || busy_) return false;
  xSemaphoreTake(mux_, portMAX_DELAY);
  pendingCtx_ = context;
  pendingBig_ = big;
  hasPending_ = true;
  xSemaphoreGive(mux_);
  busy_ = true;
  return true;
}

bool LlmClient::takeReply(String &phrase, bool &ok) {
  if (!hasReply_) return false;
  xSemaphoreTake(mux_, portMAX_DELAY);
  phrase = reply_;
  ok = replyOk_;
  hasReply_ = false;
  xSemaphoreGive(mux_);
  return true;
}

void LlmClient::taskEntry(void *self) {
  static_cast<LlmClient *>(self)->taskLoop();
}

void LlmClient::taskLoop() {
  for (;;) {
    String ctx;
    bool big = false;
    bool work = false;
    xSemaphoreTake(mux_, portMAX_DELAY);
    if (hasPending_) {
      ctx = pendingCtx_;
      big = pendingBig_;
      hasPending_ = false;
      work = true;
    }
    xSemaphoreGive(mux_);

    if (!work) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    const char *model = (big && modelBig_ && modelBig_[0]) ? modelBig_ : model_;
    String phrase;
    bool ok = false;
    if (WiFi.status() == WL_CONNECTED) {
      /* sticky endpoint first, then the rest */
      for (int i = 0; i < endpointCount_ && !ok; i++) {
        int idx = (stickyEndpoint_ + i) % endpointCount_;
        ok = callOnce(endpoints_[idx], ctx, model, phrase);
        if (ok) stickyEndpoint_ = idx;
      }
    }

    xSemaphoreTake(mux_, portMAX_DELAY);
    reply_ = phrase;
    replyOk_ = ok;
    hasReply_ = true;
    xSemaphoreGive(mux_);
    lastOk_ = ok;
    busy_ = false;
  }
}

/* The Authorization: Bearer <sk-lm token> must never cross the open internet in
 * the clear. https is always fine; plain http is allowed ONLY to a private/LAN
 * literal IP. A public http endpoint (e.g. a DDNS LM Studio on a hotspot) is
 * skipped entirely — front it with TLS to use it off-LAN. */
static bool endpointSafe(const char *base) {
  if (strncmp(base, "https://", 8) == 0) return true;
  const char *h = strstr(base, "://");
  h = h ? h + 3 : base;
  if (strncmp(h, "10.", 3) == 0 || strncmp(h, "192.168.", 8) == 0 ||
      strncmp(h, "127.", 4) == 0)
    return true;
  if (strncmp(h, "172.", 4) == 0) { /* 172.16.0.0/12 */
    int second = atoi(h + 4);
    return second >= 16 && second <= 31;
  }
  return false;
}

bool LlmClient::callOnce(const char *base, const String &context,
                         const char *model, String &out) {
  if (!endpointSafe(base)) {
    Serial.printf("[LLM] skip %s (token would cross cleartext to a public host)\n",
                  base);
    return false;
  }
  HTTPClient http;
  String url = String(base) + "/v1/chat/completions";
  if (!http.begin(url)) return false;
  http.setTimeout(NOCT_LLM_TIMEOUT_MS);
  http.setConnectTimeout(2500); /* fail fast on a dead endpoint (PC off) */
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + apiKey_);

  JsonDocument req;
  req["model"] = model;
  req["max_tokens"] = NOCT_LLM_MAX_TOKENS;
  req["temperature"] = 0.9;
  req["stream"] = false;
  JsonArray msgs = req["messages"].to<JsonArray>();
  JsonObject sys = msgs.add<JsonObject>();
  sys["role"] = "system";
  sys["content"] = kSystemPrompt;
  JsonObject usr = msgs.add<JsonObject>();
  usr["role"] = "user";
  usr["content"] = context;

  String body;
  serializeJson(req, body);

  unsigned long t0 = millis();
  int code = http.POST(body);
  if (code != 200) {
    Serial.printf("[LLM] %s -> HTTP %d\n", base, code);
    http.end();
    return false;
  }

  JsonDocument resp;
  DeserializationError err = deserializeJson(resp, http.getString());
  http.end();
  if (err) {
    Serial.printf("[LLM] JSON error: %s\n", err.c_str());
    return false;
  }
  const char *content = resp["choices"][0]["message"]["content"] | "";
  out = sanitize(String(content));
  Serial.printf("[LLM] %s ok in %lums: %s\n", model, millis() - t0,
                out.c_str());
  return out.length() > 0;
}

String LlmClient::sanitize(const String &raw) {
  String s = stripGlyphs(raw); /* drop guillemets / emoji / tofu the model added */
  s.trim();
  s.replace("\r", " ");
  s.replace("\n", " ");
  s.replace("\"", "");
  s.replace("*", "");
  while (s.indexOf("  ") >= 0) s.replace("  ", " ");

  /* reasoning leak guard: if the model narrates its analysis instead of
   * speaking, the actual line usually comes LAST — keep the final sentence */
  static const char *markers[] = {"Проанализ", "Анализ",  "Оценка",
                                  "Роль:",     "Персонаж", "Задача",
                                  "Формат",    "рассужд"};
  bool leaked = false;
  for (auto m : markers) {
    int idx = s.indexOf(m);
    if (idx >= 0 && idx < 48) {
      leaked = true;
      break;
    }
  }
  if (leaked) {
    int cut = -1;
    for (int i = s.length() - 8; i > 0; i--) {
      char c = s[i];
      if (c == '.' || c == '!' || c == '?' || c == ':') {
        cut = i + 1;
        break;
      }
    }
    if (cut > 0 && cut < (int)s.length() - 6) {
      s = s.substring(cut);
      s.trim();
    }
  }
  /* hard truncate at a UTF-8 boundary */
  if (s.length() > NOCT_LLM_REPLY_MAX) {
    int cut = NOCT_LLM_REPLY_MAX;
    while (cut > 0 && (s[cut] & 0xC0) == 0x80) cut--;
    s = s.substring(0, cut);
    s += "...";
  }
  return s;
}
