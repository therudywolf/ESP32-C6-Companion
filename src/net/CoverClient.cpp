#include "net/CoverClient.h"

#include <HTTPClient.h>
#include <WiFi.h>

#include "core/HeapWatch.h"
#include "core/config.h"
#include "storage/SdStore.h"

/* /covers/<token>.565 — raw little-endian RGB565, exactly W*H*2 bytes, so a
 * size check is a sufficient integrity test on read. */
static void coverPath(char *out, size_t cap, long tok) {
  snprintf(out, cap, "/covers/%ld.565", tok);
}

bool CoverClient::dominant(uint8_t &dr, uint8_t &dg, uint8_t &db) const {
  if (!ready_) return false;
  uint32_t sr = 0, sg = 0, sb = 0, wsum = 0;
  /* every 4th pixel is plenty for a colour average and keeps this off the
   * frame budget: 576 samples instead of 9216 */
  for (int i = 0; i < W * H; i += 4) {
    uint16_t p = buf_[i];
    uint8_t r = ((p >> 11) & 0x1F) << 3;
    uint8_t g = ((p >> 5) & 0x3F) << 2;
    uint8_t b = (p & 0x1F) << 3;
    uint8_t mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    uint8_t mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    uint32_t wgt = 1 + (uint32_t)(mx - mn); /* chroma: grey counts for almost nothing */
    sr += (uint32_t)r * wgt;
    sg += (uint32_t)g * wgt;
    sb += (uint32_t)b * wgt;
    wsum += wgt;
  }
  if (!wsum) return false;
  dr = (uint8_t)(sr / wsum);
  dg = (uint8_t)(sg / wsum);
  db = (uint8_t)(sb / wsum);
  return true;
}

bool CoverClient::serveFromCache(SdStore *sd) {
  if (!sd || !sd->ok() || skipCache_) return false;
  long want = wantTok_;
  if (!pending_ || want <= 0 || want == haveTok_) return false;
  /* One look per token. This runs every loop while the fetch is pending, and
   * a miss used to be re-tried each time — six card opens in 1.2 s at boot,
   * 4 KB of heap apiece (the FATFS window), exactly while the download
   * itself was emptying the heap. */
  if (want == missTok_) return false;
  char path[40];
  coverPath(path, sizeof(path), want);
  if (!sd->exists(path)) {
    missTok_ = want;
    return false;
  }
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

void CoverClient::setEndpoint(const char *host, int port) {
  if (host && *host) {
    strlcpy(host_, host, sizeof(host_));
  }
  if (port > 0) port_ = port;
}

void CoverClient::begin(const char *host, int port) {
  setEndpoint(host, port);
  /* Идемпотентно. Команда `server` меняет адрес на живой плате, и раньше
   * она звала begin() повторно: каждый вызов добавлял ещё одну задачу с
   * 8 КБ стека, обе крутили один taskLoop() поверх общего буфера обложки
   * и общих флагов pending_/wantTok_. Две загрузки в одну память — это не
   * «медленнее», это порванная картинка и утечка стека за раз. */
  if (task_) return;
  xTaskCreate(taskEntry, "cover", 8192, this, 1, &task_);
}

void CoverClient::update(long tok) {
  /* Not the one being downloaded right now either. This runs every loop, and
   * haveTok_ only changes once the fetch is done — so during the download it
   * re-armed pending_ for the very same token, and every new track was
   * fetched twice, back to back: twice the 18 KB and twice the ~10 KB of
   * heap the TCP window pins in WiFi buffers. Every boot log showed it. */
  if (tok > 0 && tok != haveTok_ && tok != inFlightTok_) {
    wantTok_ = tok;
    pending_ = true;
  }
}

void CoverClient::forceRefetch() {
  long tok = haveTok_ > 0 ? haveTok_ : wantTok_;
  if (tok <= 0) return;
  skipCache_ = true;
  haveTok_ = 0;
  wantTok_ = tok;
  pending_ = true;
}

void CoverClient::taskEntry(void *self) {
  static_cast<CoverClient *>(self)->taskLoop();
}

void CoverClient::taskLoop() {
  for (;;) {
    if (pending_ && WiFi.status() == WL_CONNECTED) {
      pending_ = false;
      long want = wantTok_;
      inFlightTok_ = want;
      ready_ = false; /* UI must not read buf_ while fetch() overwrites it */
      if (fetch()) {
        haveTok_ = want;
        ready_ = true;
        Serial.printf("[COVER] fetched tok=%ld\n", want);
      }
      inFlightTok_ = 0;
      skipCache_ = false;
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

bool CoverClient::fetch() {
  heapwatch::Scope hws(heaptag::COVER);
  const uint32_t h0 = ESP.getFreeHeap();
  const int want = W * H * 2;
  HTTPClient http;
  String url = String("http://") + host_ + ":" + port_ + "/cover.bin";
  if (!http.begin(url)) return false;
  http.setTimeout(8000);
  http.setConnectTimeout(4000);
  int code = http.GET();
  const uint32_t h1 = ESP.getFreeHeap();
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
  const uint32_t h2 = ESP.getFreeHeap();
  http.end();
  Serial.printf("[COVER] heap free: start %u, after GET %u, after body %u, "
                "after end %u\n",
                (unsigned)h0, (unsigned)h1, (unsigned)h2,
                (unsigned)ESP.getFreeHeap());
  return got == want;
}
