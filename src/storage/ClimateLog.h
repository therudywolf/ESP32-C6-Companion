/*
 * Nocturne C6 — the climate archive: what the room did, kept on the card.
 *
 * One file per day, `/climate/YYYY-MM-DD.csv`, one row per report:
 *
 *     time,temp_c,rh,bat,press_hpa
 *     07:14,26.5,48,95,993
 *
 * The old log wrote uptime seconds, which is unusable as history — two rows
 * from either side of a reboot cannot be ordered, let alone plotted. Wall
 * clock costs the same bytes and makes the file mean something on a laptop.
 *
 * Sizing: a WSDCGQ11LM reports on change plus roughly hourly, so a day is
 * 30-80 rows — about 2 KB. A year is under a megabyte against 7.4 GB of card.
 * There is no rotation and none is needed.
 *
 * Reading back is deliberately cheap: `loadSeries` walks N daily files once and
 * buckets them into a fixed number of columns, so the ДОМ screen can draw a day
 * or a week without holding more than the buckets in RAM — which matters when
 * the Zigbee stack has left ~40 KB of heap.
 */
#ifndef NOCT_CLIMATE_LOG_H
#define NOCT_CLIMATE_LOG_H

#include <Arduino.h>

#include "core/Types.h"

class SdStore;

class ClimateLog {
public:
  /* Columns in a loaded series. 32 matches the live sparkline, so the ДОМ
   * screen draws every window with the same code. */
  static const int kCols = 32;

  struct Series {
    /* Temperature x10 and humidity %, averaged per bucket. INT_MIN / -1 mark
     * a bucket no reading fell into — a gap in the record is a fact, and
     * interpolating over it would invent readings the sensor never sent. */
    int temp10[kCols];
    int hum[kCols];
    int filled = 0;   /* how many buckets have data */
    int rows = 0;     /* rows actually read, for the footer */
    int loMin10 = 0, hiMax10 = 0;
    void clear();
  };

  void begin(SdStore *sd) { sd_ = sd; }
  /* Append one reading. `date`/`hm` come from the NTP clock; without a real
   * clock nothing is written, because a row that cannot be placed in time is
   * worse than no row. */
  void append(const char *date, const char *hm, const ZbSensor &z);
  /* Load the last `days` days (1 = today) into `out`. Returns false when the
   * card is absent or no file matched. */
  bool loadSeries(int days, Series &out);
  const char *dirPath() const { return "/climate"; }

private:
  SdStore *sd_ = nullptr;
  char lastDate_[12] = {0};
};

#endif
