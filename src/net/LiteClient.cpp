#include "net/LiteClient.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "net/lite_ca.h"

void LiteClient::begin(const char *url, const char *token) {
  url_ = url ? url : "";
  token_ = token ? token : "";
  if (url_.length()) xTaskCreate(taskEntry, "lite", 12288, this, 1, &task_);
}

void LiteClient::tick(unsigned long now, bool pcDown) {
  if (!url_.length() || !pcDown) return;
  /* one in flight at a time; refresh no more than every kIntervalMs */
  if (!pending_ && !ready_ && now - lastReq_ >= kIntervalMs) {
    lastReq_ = now;
    pending_ = true;
  }
}

bool LiteClient::take(String &out) {
  if (!ready_) return false;
  out = payload_;
  payload_ = "";
  ready_ = false;
  return true;
}

void LiteClient::taskEntry(void *self) {
  static_cast<LiteClient *>(self)->taskLoop();
}

void LiteClient::taskLoop() {
  for (;;) {
    if (pending_ && WiFi.status() == WL_CONNECTED) {
      pending_ = false;
      String body;
      if (fetch(body) && body.length()) {
        payload_ = body;
        ready_ = true;
        Serial.printf("[LITE] fallback payload %d B\n", (int)body.length());
      }
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

bool LiteClient::fetch(String &out) {
  WiFiClientSecure client;
  client.setCACert(LITE_CA_ISRG_X1); /* verify the chain — no MITM, no leak */
  HTTPClient http;
  if (!http.begin(client, url_)) return false;
  http.setTimeout(9000);
  http.setConnectTimeout(5000);
  /* token in the Authorization header, not the URL query, so it never lands in
   * a reverse-proxy access log */
  if (token_.length())
    http.addHeader("Authorization", String("Bearer ") + token_);
  int code = http.GET();
  if (code == 200) out = http.getString();
  http.end();
  return code == 200;
}
