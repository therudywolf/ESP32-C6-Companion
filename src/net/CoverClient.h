/*
 * Nocturne C6 — album-cover fetcher. The PC server (control panel, port 8899)
 * serves the current Spotify cover as raw little-endian RGB565 at /cover.bin.
 * On a track change (the payload's cover token changes) we pull the ~18 KB
 * image on a background task so the render loop never blocks, then the MEDIA
 * scene pushImage()s it.
 */
#ifndef NOCT_COVER_CLIENT_H
#define NOCT_COVER_CLIENT_H

#include <Arduino.h>

class CoverClient {
public:
  static const int W = 96, H = 96;

  void begin(const char *host, int port);
  /* call with the latest cover token each payload; fetches if it changed */
  void update(long tok);
  bool ready() const { return ready_; }
  const uint16_t *data() const { return buf_; }

private:
  static void taskEntry(void *self);
  void taskLoop();
  bool fetch();

  String host_;
  int port_ = 8899;
  volatile long wantTok_ = 0;
  long haveTok_ = 0;
  volatile bool pending_ = false;
  volatile bool ready_ = false;
  uint16_t buf_[W * H]; /* 18 KB RGB565 framebuffer for the cover */
  TaskHandle_t task_ = nullptr;
};

#endif
