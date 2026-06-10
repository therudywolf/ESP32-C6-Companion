#include "storage/SdStore.h"

#include <SD.h>
#include <SPI.h>

#include "core/config.h"

bool SdStore::begin() {
  /* Flaky cards prefer a slow clock; a "mounted" card reporting 0 bytes is
   * NOT mounted (CSD read failed) — treat as failure and keep walking. */
  static const uint32_t freqs[] = {4000000, 10000000, NOCT_SD_SPI_HZ};
  ok_ = false;
  for (uint32_t f : freqs) {
    if (SD.begin(NOCT_PIN_SD_CS, SPI, f) && SD.totalBytes() > 0) {
      Serial.printf("[SD] mounted at %lu Hz\n", (unsigned long)f);
      ok_ = true;
      break;
    }
    SD.end();
    delay(100);
  }
  if (ok_) {
    ensureDirs();
    Serial.printf("[SD] mounted, %llu MB used / %llu MB\n",
                  SD.usedBytes() / (1024ULL * 1024ULL),
                  SD.totalBytes() / (1024ULL * 1024ULL));
  } else {
    Serial.println("[SD] no card / mount failed — running without SD");
  }
  return ok_;
}

void SdStore::ensureDirs() {
  if (!ok_) return;
  SD.mkdir("/wolf");
  SD.mkdir("/wolf/cache");
  SD.mkdir("/logs");
}

void SdStore::enqueueAppend(const char *path, const String &line) {
  if (!ok_) return;
  /* single producer (main loop) / single consumer (main loop): the slot at
   * qHead_ is ours until the index advances. No heap ops inside critical. */
  portENTER_CRITICAL(&mux_);
  int head = qHead_;
  int next = (head + 1) % kQueueMax;
  bool full = (next == qTail_);
  portEXIT_CRITICAL(&mux_);
  if (full) return; /* drop newest on overflow — logs, not ledgers */
  queue_[head].path = path;
  queue_[head].line = line;
  portENTER_CRITICAL(&mux_);
  qHead_ = next;
  portEXIT_CRITICAL(&mux_);
}

void SdStore::flush() {
  if (!ok_) return;
  while (qTail_ != qHead_) {
    PendingWrite &w = queue_[qTail_];
    appendLine(w.path.c_str(), w.line);
    portENTER_CRITICAL(&mux_);
    qTail_ = (qTail_ + 1) % kQueueMax;
    portEXIT_CRITICAL(&mux_);
  }
}

bool SdStore::appendLine(const char *path, const String &line) {
  if (!ok_) return false;
  File f = SD.open(path, FILE_APPEND);
  if (!f) return false;
  f.println(line);
  f.close();
  return true;
}

bool SdStore::readAll(const char *path, String &out, size_t maxBytes) {
  if (!ok_) return false;
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  size_t size = f.size();
  if (size > maxBytes) f.seek(size - maxBytes);
  out = f.readString();
  f.close();
  return true;
}

bool SdStore::readLastLines(const char *path, int n, String &out) {
  String all;
  if (!readAll(path, all, 4096)) return false;
  int count = 0;
  for (int i = all.length() - 1; i >= 0; i--) {
    if (all[i] == '\n' && ++count > n) {
      out = all.substring(i + 1);
      return true;
    }
  }
  out = all;
  return true;
}
