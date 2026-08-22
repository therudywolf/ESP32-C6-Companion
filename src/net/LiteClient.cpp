#include "net/LiteClient.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <esp_sntp.h>

#include "net/lite_ca.h"

void LiteClient::begin(const char *url, const char *token) {
  url_ = url ? url : "";
  token_ = token ? token : "";
  if (!url_.length()) return;
  mux_ = xSemaphoreCreateMutex();
  xTaskCreate(taskEntry, "lite", 12288, this, 1, &task_);
}

void LiteClient::tick(unsigned long now, bool pcDown) {
  if (!url_.length() || !mux_) return;
  if (!pcDown) {
    /* PC is back: drop any unconsumed fallback so a later PC-down can't be
     * served a stale payload. gen_ also invalidates a fetch already in flight —
     * we must NOT reassign payload_ here if the task is about to write it. */
    gen_ = gen_ + 1; /* not gen_++: ++ on a volatile is deprecated in C++20 */
    pending_ = false;
    xSemaphoreTake(mux_, portMAX_DELAY);
    ready_ = false;
    payload_ = "";
    xSemaphoreGive(mux_);
    return;
  }
  /* one in flight at a time; steady cadence when healthy, faster after a fail */
  unsigned long iv = ok_ ? kIntervalMs : kRetryMs;
  if (!pending_ && !ready_ && now - lastReq_ >= iv) {
    lastReq_ = now;
    pending_ = true;
  }
}

bool LiteClient::take(String &out) {
  if (!ready_ || !mux_) return false;
  xSemaphoreTake(mux_, portMAX_DELAY);
  bool got = ready_;
  if (got) {
    out = payload_;
    payload_ = "";
    ready_ = false;
  }
  xSemaphoreGive(mux_);
  return got;
}

void LiteClient::taskEntry(void *self) {
  static_cast<LiteClient *>(self)->taskLoop();
}

void LiteClient::taskLoop() {
  for (;;) {
    if (pending_ && WiFi.status() == WL_CONNECTED) {
      pending_ = false;
      uint32_t myGen = gen_; /* snapshot: a cancel while we fetch voids this run */
      String body;
      bool good = fetch(body) && body.length();
      ok_ = good;
      if (good && myGen == gen_) {
        xSemaphoreTake(mux_, portMAX_DELAY);
        payload_ = body;
        ready_ = true;
        xSemaphoreGive(mux_);
        Serial.printf("[LITE] fallback payload %d B\n", (int)body.length());
      }
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

bool LiteClient::fetch(String &out) {
  /* CA validation checks the cert's validity dates against the wall clock — if
   * NTP hasn't synced yet (RTC ~1970) the handshake would fail with a bogus
   * "cert not yet valid". Skip until the clock is real; the retry cadence picks
   * it up once SNTP lands (seconds after WiFi). */
  if (time(nullptr) < 1700000000L) return false;
  /* Hold the first fetch until SNTP's own DNS query has finished. The RTC
   * keeps time across warm reboots, so the wall-clock guard above passes
   * seconds after boot while SNTP is still mid-lookup - and interleaving two
   * resolvers in that window is what the dns_clear_cache crash grew from.
   * The linker wrap is the real fix; this removes the collision entirely. */
  if (millis() < 60000UL && esp_sntp_enabled() &&
      esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET)
    return false;
  /* A TLS handshake needs tens of KB of heap. With the Zigbee stack resident
   * that can be most of what is free - skip the fetch rather than OOM the
   * board, and say so, once per dry spell. */
  size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  static bool warned = false;
  if (largest < 46 * 1024) {
    if (!warned) {
      warned = true;
      Serial.printf("[LITE] skipped: largest free block %u B < 46 KB\n",
                    (unsigned)largest);
    }
    return false;
  }
  warned = false;
  WiFiClientSecure client;
  client.setCACert(LITE_CA_BUNDLE); /* verify chain vs pinned ISRG Root X1+X2 — no MITM, no leak */
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
  else Serial.printf("[LITE] fetch HTTP %d\n", code); /* 401 token vs <0 network */
  http.end();
  return code == 200;
}
