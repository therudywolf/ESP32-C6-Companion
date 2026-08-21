/*
 * Nocturne C6 — microSD on the shared SPI bus (CS=4, MISO=5, MOSI=6, SCLK=7).
 * Concurrency rule: ALL SPI (LCD + SD) happens on the UI/loop task only.
 * Other tasks enqueue writes; the loop flushes them between frames.
 * The card is optional — everything degrades gracefully without it.
 */
#ifndef NOCT_SD_STORE_H
#define NOCT_SD_STORE_H

#include <Arduino.h>

#include <functional>

class SdStore {
public:
  bool begin(); /* call AFTER display init (bus already configured) */
  bool ok() const { return ok_; }

  /* Hook that must leave SPI2 idle before the card is selected. main() wires
   * this to Display::syncBus(); without it the LCD's in-flight DMA collides
   * with the card's chip-select and every access fails. Set after the display
   * is up — begin() deliberately runs before that, on an unshared bus. */
  void setBusSync(std::function<void()> fn) { busSync_ = fn; }

  /* Queue a line to append to a file (thread-safe; called from llm task).
   * maxBytes > 0 rotates the file once it grows past it (see rotate()). */
  void enqueueAppend(const char *path, const String &line, size_t maxBytes = 0);
  /* Flush queued writes — loop task only, after the frame is pushed. */
  void flush();

  /* Direct helpers — loop task only. */
  bool appendLine(const char *path, const String &line);
  bool readAll(const char *path, String &out, size_t maxBytes = 8192);
  bool readLastLines(const char *path, int n, String &out);
  /* Halve a line file once it exceeds maxBytes, keeping the NEWEST lines.
   * The old behaviour — stop appending at the cap — silently froze the phrase
   * cache on whatever it had learned first and never refreshed it again. */
  bool rotate(const char *path, size_t maxBytes);
  /* Fixed-size binary blobs (the hour/day history snapshot). */
  bool writeBlob(const char *path, const void *data, size_t len);
  bool readBlob(const char *path, void *data, size_t len);
  void ensureDirs();

private:
  void sync() { if (busSync_) busSync_(); } /* drain the LCD's DMA first */

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
};

#endif
