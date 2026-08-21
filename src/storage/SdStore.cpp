#include "storage/SdStore.h"

#include <SD.h>
#include <SPI.h>

#include "core/config.h"

bool SdStore::begin() {
  /* Fastest first, but PROVEN. The old order started at 4 MHz and broke on the
   * first success, so 25 MHz was never attempted and the card always ran at the
   * slowest rung. Simply reversing the order was worse: measured on this board,
   * the same card mounts at 25 MHz and then cannot create a directory or write
   * a byte — and sometimes it can, on the very next boot. Neither "always slow"
   * nor "always fast" is right, so each rung is settled by a real write below
   * rather than by anyone's guess. */
  static const uint32_t freqs[] = {NOCT_SD_SPI_HZ, 10000000, 4000000};
  ok_ = false;
  clockHz_ = 0;
  for (uint32_t f : freqs) {
    /* a "mounted" card reporting 0 bytes is NOT mounted (CSD read failed) */
    if (!SD.begin(NOCT_PIN_SD_CS, SPI, f) || SD.totalBytes() == 0) {
      SD.end();
      delay(100);
      continue;
    }
    /* Mounting proves nothing. This card mounts happily at 25 MHz and then
     * cannot create a directory or write a byte — which is what the original
     * "flaky cards prefer a slow clock" note was really about. So every rung is
     * PROVEN with a real write before it is accepted, on a quiet bus, before
     * the display exists. Fast when the card can take it, slow when it cannot,
     * decided by evidence rather than by either of our guesses. */
    ok_ = true; /* provisional: ensureDirs/probe need it */
    ensureDirs();
    const char *probe = "/logs/.wtest";
    File wt = SD.open(probe, FILE_WRITE);
    bool wok = wt && wt.print("ok") == 2;
    if (wt) wt.close();
    if (wok) SD.remove(probe);
    if (!wok) {
      Serial.printf("[SD] %lu Hz mounts but cannot write - stepping down\n",
                    (unsigned long)f);
      ok_ = false;
      SD.end();
      delay(100);
      continue;
    }
    clockHz_ = f;
    Serial.printf("[SD] mounted at %lu Hz (write verified)\n",
                  (unsigned long)f);
    /* f_getfree is flaky on some cards right after a re-mount and answers 0.
     * Printing "0 MB used / 0 MB" underneath a passing write test reads like a
     * failure, so say nothing rather than something wrong. */
    uint64_t total = SD.totalBytes();
    if (total > 0)
      Serial.printf("[SD] %llu MB used / %llu MB\n",
                    SD.usedBytes() / (1024ULL * 1024ULL),
                    total / (1024ULL * 1024ULL));
    break;
  }
  if (!ok_) Serial.println("[SD] no card / unusable - running without SD");
  return ok_;
}

/* One place decides whether an operation was slow, whether it failed, and
 * whether enough of them have failed that the card should be considered gone. */
bool SdStore::track(const char *what, unsigned long t0, bool good) {
  unsigned long ms = millis() - t0;
  if (ms >= NOCT_SD_SLOW_MS)
    Serial.printf("[SD] slow %s: %lu ms (frame budget is %d ms)\n", what, ms,
                  NOCT_FRAME_MS);
  if (good) {
    failStreak_ = 0;
    return true;
  }
  if (++failStreak_ >= NOCT_SD_FAIL_LIMIT) {
    /* Card pulled, or the bus went bad. Stop pretending: every later call
     * returns immediately and the UI shows SD as absent, instead of the board
     * grinding out one failed transaction per call forever. A reboot (or
     * reinserting and rebooting) re-detects it. */
    ok_ = false;
    Serial.printf("[SD] %d consecutive failures - card marked absent\n",
                  failStreak_);
  }
  return false;
}

void SdStore::ensureDirs() {
  if (!ok_) return;
  static const char *dirs[] = {"/wolf", "/wolf/cache", "/logs", "/covers", "/shots", "/forza",
                                "/wolf/journal"};
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
  /* Bounded: the queue holds 8 and each entry is an open/append/close. Draining
   * the lot in one call put all of that inside a single frame. */
  for (int done = 0; done < NOCT_SD_FLUSH_MAX && qTail_ != qHead_; done++) {
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
  unsigned long t0 = millis();
  File f = SD.open(path, FILE_APPEND);
  if (!f) return track("append-open", t0, false);
  f.println(line);
  f.close();
  return track("append", t0, true);
}

bool SdStore::readAll(const char *path, String &out, size_t maxBytes) {
  if (!ok_) return false;
  sync();
  unsigned long t0 = millis();
  File f = SD.open(path, FILE_READ);
  if (!f) return false; /* a missing file is not a card failure */
  size_t size = f.size();
  if (size > maxBytes) f.seek(size - maxBytes);
  out = f.readString();
  f.close();
  return track("read", t0, true);
}

bool SdStore::readLastLines(const char *path, int n, String &out,
                            size_t maxBytes) {
  String all;
  if (!readAll(path, all, maxBytes)) return false;
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

bool SdStore::exists(const char *path) {
  if (!ok_) return false;
  sync();
  return SD.exists(path);
}

bool SdStore::remove(const char *path) {
  if (!ok_) return false;
  sync();
  return SD.remove(path);
}

int SdStore::pruneDir(const char *dir, int keep) {
  if (!ok_ || keep < 0) return 0;
  sync();
  File d = SD.open(dir);
  if (!d || !d.isDirectory()) {
    if (d) d.close();
    return 0;
  }
  /* Count first, then delete the excess on a second pass — deleting while
   * iterating a FAT directory is not something to rely on. */
  int total = 0;
  for (File e = d.openNextFile(); e; e = d.openNextFile()) {
    if (!e.isDirectory()) total++;
    e.close();
  }
  int excess = total - keep;
  if (excess <= 0) {
    d.close();
    return 0;
  }
  d.rewindDirectory();
  int killed = 0;
  for (File e = d.openNextFile(); e && killed < excess; e = d.openNextFile()) {
    if (e.isDirectory()) {
      e.close();
      continue;
    }
    String p = String(e.path());
    e.close();
    if (SD.remove(p.c_str())) killed++;
  }
  d.close();
  if (killed) Serial.printf("[SD] pruned %d file(s) from %s\n", killed, dir);
  return killed;
}

void SdStore::logDir(const char *dir) {
  if (!ok_) return;
  sync();
  File d = SD.open(dir);
  if (!d || !d.isDirectory()) {
    if (d) d.close();
    Serial.printf("[SD] %s: missing\n", dir);
    return;
  }
  int n = 0;
  unsigned long bytes = 0;
  for (File e = d.openNextFile(); e; e = d.openNextFile()) {
    if (!e.isDirectory()) {
      Serial.printf("[SD]   %s  %u B\n", e.name(), (unsigned)e.size());
      bytes += e.size();
      n++;
    }
    e.close();
  }
  d.close();
  Serial.printf("[SD] %s: %d file(s), %lu B\n", dir, n, bytes);
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
  unsigned long t0 = millis();
  /* keep the newest half, starting at the first whole line after the seek */
  f.seek(size - maxBytes / 2);
  String tail = f.readString();
  f.close();
  int nl = tail.indexOf('\n');
  if (nl >= 0) tail = tail.substring(nl + 1);

  String tmp = String(path) + ".tmp";
  SD.remove(tmp.c_str());
  File out = SD.open(tmp.c_str(), FILE_WRITE);
  if (!out) return track("rotate-open", t0, false);
  out.print(tail);
  out.close();
  SD.remove(path);
  bool moved = SD.rename(tmp.c_str(), path);
  Serial.printf("[SD] rotate %s: %u -> %u B%s\n", path, (unsigned)size,
                (unsigned)tail.length(), moved ? "" : " (rename failed)");
  return track("rotate", t0, moved);
}

bool SdStore::writeBlob(const char *path, const void *data, size_t len) {
  if (!ok_ || !data || !len) return false;
  sync();
  unsigned long t0 = millis();
  File f = SD.open(path, FILE_WRITE); /* FILE_WRITE truncates */
  if (!f) {
    /* The usual cause is a missing parent directory - a card formatted while
     * the board was running, or an mkdir that quietly failed at mount. Rebuild
     * the tree and try once more before giving up. */
    ensureDirs();
    f = SD.open(path, FILE_WRITE);
    if (!f) {
      Serial.printf("[SD] open %s for write FAILED\n", path);
      return track("write-open", t0, false);
    }
  }
  size_t n = f.write((const uint8_t *)data, len);
  f.close();
  if (n != len)
    Serial.printf("[SD] short write %s: %u/%u B\n", path, (unsigned)n,
                  (unsigned)len);
  return track("write", t0, n == len);
}

bool SdStore::readBlob(const char *path, void *data, size_t len) {
  if (!ok_ || !data || !len) return false;
  sync();
  unsigned long t0 = millis();
  File f = SD.open(path, FILE_READ);
  if (!f) return false; /* a missing file is not a card failure */
  bool good = (f.size() == len) && (f.read((uint8_t *)data, len) == (int)len);
  f.close();
  return track("read-blob", t0, good);
}
