#include "net/LlmClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

/* gemma-4-e2b is a reasoning model: let it think (separateReasoningContentInAPI
 * routes thoughts to reasoning_content; content arrives clean), give it
 * enough budget to finish thinking (~300-450 tokens typical), and ask it to
 * keep the thinking short. reasoning_effort:"none" looked tempting but leaks
 * meta-analysis into content — verified against the live model. */
static const char *kSystemPrompt =
    "Ты Ноктюрн — саркастичный киберпанк-волк, живущий в карманном "
    "устройстве, которое следит за компьютером хозяина. Ответь ОДНОЙ короткой "
    "репликой от первого лица, максимум 90 символов. Без кавычек, без эмодзи. "
    "Только русский. Думай кратко.";

void LlmClient::begin(const char *const *endpoints, int endpointCount,
                      const char *apiKey, const char *model) {
  endpoints_ = endpoints;
  endpointCount_ = endpointCount;
  apiKey_ = apiKey;
  model_ = model;
  mux_ = xSemaphoreCreateMutex();
  xTaskCreate(taskEntry, "llm", 8192, this, 1, &task_);
}

bool LlmClient::request(const String &context) {
  if (!task_ || busy_) return false;
  xSemaphoreTake(mux_, portMAX_DELAY);
  pendingCtx_ = context;
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
    bool work = false;
    xSemaphoreTake(mux_, portMAX_DELAY);
    if (hasPending_) {
      ctx = pendingCtx_;
      hasPending_ = false;
      work = true;
    }
    xSemaphoreGive(mux_);

    if (!work) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    String phrase;
    bool ok = false;
    if (WiFi.status() == WL_CONNECTED) {
      /* sticky endpoint first, then the rest */
      for (int i = 0; i < endpointCount_ && !ok; i++) {
        int idx = (stickyEndpoint_ + i) % endpointCount_;
        ok = callOnce(endpoints_[idx], ctx, phrase);
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

bool LlmClient::callOnce(const char *base, const String &context,
                         String &out) {
  HTTPClient http;
  String url = String(base) + "/v1/chat/completions";
  if (!http.begin(url)) return false;
  http.setTimeout(NOCT_LLM_TIMEOUT_MS);
  http.setConnectTimeout(4000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + apiKey_);

  JsonDocument req;
  req["model"] = model_;
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
  Serial.printf("[LLM] ok in %lums: %s\n", millis() - t0, out.c_str());
  return out.length() > 0;
}

String LlmClient::sanitize(const String &raw) {
  String s = raw;
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
