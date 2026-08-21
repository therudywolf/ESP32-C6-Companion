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
    /* Write once here, before the display exists and the bus is contended. If
     * this succeeds and later writes fail, the card is fine and the problem is
     * arbitration, not the media - which is exactly the distinction that cost
     * an evening. */
    const char *probe = "/logs/.wtest";
    File wt = SD.open(probe, FILE_WRITE);
    bool wok = wt && wt.print("ok") == 2;
    if (wt) wt.close();
    if (wok) SD.remove(probe);
    Serial.printf("[SD] write test on a quiet bus: %s\n",
                  wok ? "ok" : "FAILED");
    Serial.printf("[SD] mounted, %llu MB used / %llu MB\n",
                  SD.usedBytes() / (1024ULL * 1024ULL),
                  SD.totalBytes() / (1024ULL * 1024ULL));
  } else {
    Serial.println("[SD] no card / mount failed - running without SD");
  }
  return ok_;
}

void SdStore::ensureDirs() {
  if (!ok_) return;
  static const char *dirs[] = {"/wolf", "/wolf/cache", "/logs"};
  for (const char *d : dirs) {
    if (SD.exists(d)) continue;
    if (!SD.mkdir(d)) Serial.printf("[SD] mkdir %s FAILED\n", d);
  }
}

void SdStore::enqueueAppend(const char *path, const String &line,
                            size_t maxBytes) {
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
  queue_[head].cap = maxBytes;
  portENTER_CRITICAL(&mux_);
  qHead_ = next;
  portEXIT_CRITICAL(&mux_);
}

void SdStore::flush() {
  if (!ok_) return;
  sync();
  while (qTail_ != qHead_) {
    PendingWrite &w = queue_[qTail_];
    if (w.cap) rotate(w.path.c_str(), w.cap);
    appendLine(w.path.c_str(), w.line);
    portENTER_CRITICAL(&mux_);
    qTail_ = (qTail_ + 1) % kQueueMax;
    portEXIT_CRITICAL(&mux_);
  }
}

bool SdStore::appendLine(const char *path, const String &line) {
  if (!ok_) return false;
  sync();
  File f = SD.open(path, FILE_APPEND);
  if (!f) return false;
  f.println(line);
  f.close();
  return true;
}

bool SdStore::readAll(const char *path, String &out, size_t maxBytes) {
  if (!ok_) return false;
  sync();
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

bool SdStore::rotate(const char *path, size_t maxBytes) {
  if (!ok_ || maxBytes == 0) return false;
  sync();
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  size_t size = f.size();
  if (size <= maxBytes) {
    f.close();
    return false; /* nothing to do */
  }
  /* keep the newest half, starting at the first whole line after the seek */
  f.seek(size - maxBytes / 2);
  String tail = f.readString();
  f.close();
  int nl = tail.indexOf('\n');
  if (nl >= 0) tail = tail.substring(nl + 1);

  String tmp = String(path) + ".tmp";
  SD.remove(tmp.c_str());
  File out = SD.open(tmp.c_str(), FILE_WRITE);
  if (!out) return false;
  out.print(tail);
  out.close();
  SD.remove(path);
  bool moved = SD.rename(tmp.c_str(), path);
  Serial.printf("[SD] rotate %s: %u -> %u B%s\n", path, (unsigned)size,
                (unsigned)tail.length(), moved ? "" : " (rename failed)");
  return moved;
}

bool SdStore::writeBlob(const char *path, const void *data, size_t len) {
  if (!ok_ || !data || !len) return false;
  sync();
  File f = SD.open(path, FILE_WRITE); /* FILE_WRITE truncates */
  if (!f) {
    /* The usual cause is a missing parent directory - a card formatted while
     * the board was running, or an mkdir that quietly failed at mount. Rebuild
     * the tree and try once more before giving up. */
    ensureDirs();
    f = SD.open(path, FILE_WRITE);
    if (!f) {
      Serial.printf("[SD] open %s for write FAILED\n", path);
      return false;
    }
  }
  size_t n = f.write((const uint8_t *)data, len);
  f.close();
  if (n != len)
    Serial.printf("[SD] short write %s: %u/%u B\n", path, (unsigned)n,
                  (unsigned)len);
  return n == len;
}

bool SdStore::readBlob(const char *path, void *data, size_t len) {
  if (!ok_ || !data || !len) return false;
  sync();
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  bool good = (f.size() == len) && (f.read((uint8_t *)data, len) == (int)len);
  f.close();
  return good;
}
