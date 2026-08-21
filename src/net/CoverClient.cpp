#include "net/CoverClient.h"

#include <HTTPClient.h>
#include <WiFi.h>

#include "core/config.h"
#include "storage/SdStore.h"

/* /covers/<token>.565 — raw little-endian RGB565, exactly W*H*2 bytes, so a
 * size check is a sufficient integrity test on read. */
static void coverPath(char *out, size_t cap, long tok) {
  snprintf(out, cap, "/covers/%ld.565", tok);
}

bool CoverClient::serveFromCache(SdStore *sd) {
  if (!sd || !sd->ok()) return false;
  long want = wantTok_;
  if (!pending_ || want <= 0 || want == haveTok_) return false;
  char path[40];
  coverPath(path, sizeof(path), want);
  if (!sd->readBlob(path, buf_, sizeof(buf_))) return false;
  /* Beat the fetch task to it: clear the request so nothing downloads what we
   * just read off the card. */
  pending_ = false;
  haveTok_ = want;
  cachedTok_ = want;
  ready_ = true;
  Serial.printf("[COVER] tok=%ld from card\n", want);
  return true;
}

void CoverClient::storeToCache(SdStore *sd) {
  if (!sd || !sd->ok() || !ready_) return;
  long tok = haveTok_;
  if (tok <= 0 || tok == cachedTok_) return;
  cachedTok_ = tok; /* one attempt per token, success or not */
  char path[40];
  coverPath(path, sizeof(path), tok);
  if (sd->exists(path)) return;
  if (sd->writeBlob(path, buf_, sizeof(buf_)))
    sd->pruneDir("/covers", NOCT_COVER_CACHE_MAX);
}

void CoverClient::begin(const char *host, int port) {
  host_ = host;
  port_ = port;
  xTaskCreate(taskEntry, "cover", 8192, this, 1, &task_);
}

void CoverClient::update(long tok) {
  if (tok > 0 && tok != haveTok_) {
    wantTok_ = tok;
    pending_ = true;
  }
}

void CoverClient::taskEntry(void *self) {
  static_cast<CoverClient *>(self)->taskLoop();
}

void CoverClient::taskLoop() {
  for (;;) {
    if (pending_ && WiFi.status() == WL_CONNECTED) {
      pending_ = false;
      long want = wantTok_;
      ready_ = false; /* UI must not read buf_ while fetch() overwrites it */
      if (fetch()) {
        haveTok_ = want;
        ready_ = true;
        Serial.printf("[COVER] fetched tok=%ld\n", want);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

bool CoverClient::fetch() {
  const int want = W * H * 2;
  HTTPClient http;
  String url = String("http://") + host_ + ":" + port_ + "/cover.bin";
  if (!http.begin(url)) return false;
  http.setTimeout(8000);
  http.setConnectTimeout(4000);
  int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }
  int len = http.getSize();
  if (len >= 0 && len != want) { /* unexpected size — skip */
    http.end();
    return false;
  }
  WiFiClient *s = http.getStreamPtr();
  uint8_t *p = (uint8_t *)buf_;
  int got = 0;
  unsigned long t0 = millis();
  while (got < want && millis() - t0 < 8000) {
    int avail = s->available();
    if (avail > 0) {
      int n = s->readBytes(p + got, want - got);
      got += n;
      t0 = millis();
    } else {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  http.end();
  return got == want;
}
