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

  /* Queue a line to append to a file (thread-safe; called from llm task). */
  void enqueueAppend(const char *path, const String &line);
  /* Flush queued writes — loop task only, after the frame is pushed. */
  void flush();

  /* Direct helpers — loop task only. */
  bool appendLine(const char *path, const String &line);
  bool readAll(const char *path, String &out, size_t maxBytes = 8192);
  bool readLastLines(const char *path, int n, String &out);
  void ensureDirs();

private:
  struct PendingWrite {
    String path;
    String line;
  };
  static const int kQueueMax = 8;
  PendingWrite queue_[kQueueMax];
  volatile int qHead_ = 0, qTail_ = 0;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  bool ok_ = false;
};

#endif
