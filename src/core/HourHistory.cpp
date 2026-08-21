#include "core/HourHistory.h"

#include <time.h>

#include "core/Types.h"
#include "core/config.h"
#include "storage/SdStore.h"

/* On-disk snapshot. Only this firmware reads it back, so a raw struct is fine —
 * magic + version + size guard a layout change across builds. */
namespace {
const uint32_t kMagic = 0x4E484753; /* "NHGS" */
const uint16_t kVersion = 1;
const char *kPath = "/logs/hist.bin";
/* Adopt a snapshot only while it still describes the present: a minute series
 * is nonsense after a long gap, an hourly one tolerates more. */
const long kMaxGapHourSec = 5 * 60;
const long kMaxGapDaySec = 60 * 60;

struct HistBlob {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t savedAt; /* unix seconds */
  GraphSet hour;
  GraphSet day;
};
} // namespace

Histories::Histories() {
  day.ct.setCap(24);
  day.gt.setCap(24);
  day.cl.setCap(24);
  day.gl.setCap(24);
  day.ram.setCap(24);
}

void Histories::accumulate(const HardwareData &hw) {
  sct += hw.ct;
  sgt += hw.gt;
  scl += hw.cl;
  sgl += hw.gl;
  sram += hw.ra > 0.1f ? (int)(hw.ru * 100.0f / hw.ra) : 0;
  n++;
}

void Histories::commitMinute() {
  int mct = (int)(sct / n), mgt = (int)(sgt / n), mcl = (int)(scl / n);
  int mgl = (int)(sgl / n), mram = (int)(sram / n);
  hour.ct.push(mct);
  hour.gt.push(mgt);
  hour.cl.push(mcl);
  hour.gl.push(mgl);
  hour.ram.push(mram);
  sct = sgt = scl = sgl = sram = 0;
  n = 0;
  /* roll the minute into the current hour bucket */
  hct += mct;
  hgt += mgt;
  hcl += mcl;
  hgl += mgl;
  hram += mram;
  if (++hn >= 60) commitHour();
}

void Histories::commitHour() {
  if (hn == 0) return;
  day.ct.push((int)(hct / hn));
  day.gt.push((int)(hgt / hn));
  day.cl.push((int)(hcl / hn));
  day.gl.push((int)(hgl / hn));
  day.ram.push((int)(hram / hn));
  hct = hgt = hcl = hgl = hram = 0;
  hn = 0;
}

void Histories::tick(unsigned long now) {
  tryRestore();
  if (lastCommit == 0) lastCommit = now;
  if (now - lastCommit >= 60000UL) { /* one sample per minute */
    lastCommit = now;
    if (n > 0) commitMinute();
  }
  if (sd_ && (lastSave == 0 || now - lastSave >= NOCT_HIST_SAVE_MS)) {
    lastSave = now;
    save();
  }
}

void Histories::save() {
  if (!sd_) return;
  time_t t = time(nullptr);
  if (t < 1700000000L) return; /* no real clock yet — a snapshot we can't date */
  if (hour.ct.count == 0 && day.ct.count == 0) return;
  HistBlob b{};
  b.magic = kMagic;
  b.version = kVersion;
  b.size = (uint16_t)sizeof(HistBlob);
  b.savedAt = (uint32_t)t;
  b.hour = hour;
  b.day = day;
  sd_->writeBlob(kPath, &b, sizeof(b));
}

void Histories::tryRestore() {
  if (restored_ || !sd_) return;
  time_t t = time(nullptr);
  if (t < 1700000000L) return; /* wait for NTP: without it we can't age-check */
  restored_ = true;            /* one attempt, whatever the outcome */
  HistBlob b{};
  if (!sd_->readBlob(kPath, &b, sizeof(b))) return;
  if (b.magic != kMagic || b.version != kVersion ||
      b.size != (uint16_t)sizeof(HistBlob))
    return;
  long gap = (long)t - (long)b.savedAt;
  if (gap < 0) return; /* clock went backwards — don't trust it */
  if (gap <= kMaxGapDaySec) day = b.day;
  if (gap <= kMaxGapHourSec) hour = b.hour;
  Serial.printf("[HIST] restored (gap %lds): hour=%d day=%d\n", gap,
                hour.ct.count, day.ct.count);
}
