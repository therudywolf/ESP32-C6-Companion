/*
 * Nocturne C6 — microSD on the shared SPI bus (CS=4, MISO=5, MOSI=6, SCLK=7).
 * Concurrency rule: ALL SPI (LCD + SD) happens on the UI/loop task only.
 * Other tasks enqueue writes; the loop flushes them between frames.
 * The card is optional — everything degrades gracefully without it.
 *
 * Two rules earned the hard way. The panel bus MUST declare the card's MISO pin
 * (see Display.h) or nothing here works past display init. And every operation
 * runs on the render loop, so it is on the frame budget: the card is clocked as
 * fast as it will take, work is spread across frames, and anything slow says so.
 */
#ifndef NOCT_SD_STORE_H
#define NOCT_SD_STORE_H

#include <Arduino.h>

#include <functional>

#include "core/config.h"

class SdStore {
public:
  bool begin(); /* call BEFORE display init — the bus must be quiet */
  bool ok() const { return ok_; }
  uint32_t clockHz() const { return clockHz_; } /* what the card actually took */
  /* Health counters for the web panel and SYSINFO. A card that is "present"
   * but has quietly failed forty writes is worth knowing about BEFORE the
   * archive turns out to have a hole in it. */
  uint32_t usedMB() const { return usedMB_; }
  uint32_t totalMB() const { return totalMB_; }
  uint32_t writes() const { return writes_; }
  uint32_t slowOps() const { return slowOps_; }
  uint32_t failTotal() const { return failTotal_; }
  int failStreak() const { return failStreak_; }
  int queueDepth() const {
    int d = qTail_ - qHead_;
    return d < 0 ? d + kQueueMax : d;
  }
  uint32_t lastOpMs() const { return lastOpMs_; }
  /* Re-read the card's size/usage. f_getfree is flaky right after a mount and
   * sometimes answers 0 — which is why one read at boot left the panel showing
   * "0 / 0 MB" on a perfectly good 7.4 GB card. Also genuinely useful later:
   * usage GROWS as the archive fills. Rate-limited internally. */
  void refreshUsage(unsigned long now);

  /* Hook that must leave SPI2 idle before the card is selected. main() wires
   * this to Display::syncBus(); without it the LCD's in-flight DMA collides
   * with the card's chip-select and every access fails. Set after the display
   * is up — begin() deliberately runs before that, on an unshared bus. */
  void setBusSync(std::function<void()> fn) { busSync_ = fn; }

  /* Queue a line to append to a file (thread-safe; called from llm task).
   * maxBytes > 0 rotates the file once it grows past it (see rotate()). */
  void enqueueAppend(const char *path, const String &line, size_t maxBytes = 0);
  /* Drain up to NOCT_SD_FLUSH_MAX queued writes — loop task only. */
  void flush();
  bool queueEmpty() const { return qHead_ == qTail_; }

  /* Direct helpers — loop task only. */
  bool appendLine(const char *path, const String &line);
  bool readAll(const char *path, String &out, size_t maxBytes = NOCT_SD_READ_MAX);
  /* One WINDOW of a file, from `offset`, with the total size handed back so
   * the caller can walk the rest.
   *
   * readAll() keeps the TAIL of anything past the cap. That is right for a
   * log being tailed and wrong for an archive being shipped: it amputates the
   * oldest part of every file bigger than 4 KB, silently and with no error.
   * Three days of this archive were over the cap and each arrived missing its
   * morning - the 23rd began at 11:55 on the receiving end and at 00:00 on
   * the card. Nothing reported a failure, because nothing had failed; the
   * read did exactly what it was asked. */
  bool readWindow(const char *path, String &out, size_t offset,
                  size_t maxBytes, size_t *total = nullptr);
  bool readLastLines(const char *path, int n, String &out,
                     size_t maxBytes = NOCT_SD_READ_MAX);
  /* Halve a line file once it exceeds maxBytes, keeping the NEWEST lines.
   * The old behaviour — stop appending at the cap — silently froze the phrase
   * cache on whatever it had learned first and never refreshed it again. */
  bool rotate(const char *path, size_t maxBytes);
  /* Fixed-size binary blobs (history snapshot, cached album covers). */
  bool writeBlob(const char *path, const void *data, size_t len);
  bool readBlob(const char *path, void *data, size_t len);
  bool exists(const char *path);
  bool remove(const char *path);
  /* Keep at most `keep` files in a directory, deleting extras. Cheap LRU-ish
   * housekeeping for the cover cache; order is whatever the FS lists. */
  int pruneDir(const char *dir, int keep);
  /* Print a directory listing with sizes. The archive is written by a board
   * that is usually nowhere near a card reader, so "is it actually recording?"
   * has to be answerable from the serial log alone. */
  void logDir(const char *dir);
  void ensureDirs();
  /* Let a caller that opens SD:: directly (the screenshot writer streams rows
   * rather than buffering 110 KB) drain the LCD's DMA the same way we do. */
  void syncBusNow() { sync(); }

private:
  void sync() { if (busSync_) busSync_(); } /* drain the LCD's DMA first */
  /* Wrap one card operation: times it, whinges if it eats the frame budget,
   * and counts failures so a pulled card is noticed instead of retried
   * forever. */
  bool track(const char *what, unsigned long t0, bool good);

  std::function<void()> busSync_;
  struct PendingWrite {
    String path;
    String line;
    size_t cap;
  };
  static const int kQueueMax = 8;
  PendingWrite queue_[kQueueMax];
  volatile int qHead_ = 0, qTail_ = 0;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  bool ok_ = false;
  uint32_t clockHz_ = 0;
  int failStreak_ = 0;
  uint32_t usedMB_ = 0, totalMB_ = 0;
  uint32_t writes_ = 0, slowOps_ = 0, failTotal_ = 0, lastOpMs_ = 0;
  unsigned long lastUsage_ = 0;
};

#endif
