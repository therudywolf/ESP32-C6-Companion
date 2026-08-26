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

  /* Change over the last `hours`, read back off the card.
   *
   * Pressure is the one reading here that describes the OUTDOORS. A building
   * is not airtight, so indoor pressure tracks the atmosphere within a
   * fraction of a hPa — which is why a barometer indoors forecasts weather
   * while an indoor thermometer only ever describes the radiator. That
   * distinction is the whole basis of the hints below; do not extend them to
   * temperature or humidity.
   *
   * Returns false when there is no reading old enough to compare against —
   * "no trend yet" and "flat" are different answers and must not be conflated.
   * dPress is in tenths of a hPa so a 0.7 hPa drift is not rounded to zero. */
  bool trend(int hours, int &dTemp10, int &dHum, int &dPress10);
  const char *dirPath() const { return "/climate"; }

  /* ── export ───────────────────────────────────────────────────────────
   * The archive is worth nothing while it is stranded on a card inside a
   * case. These three walk it out to whoever asked, a FEW ROWS AT A TIME:
   * a month is a couple of thousand rows, and pushing them all inside one
   * loop iteration is precisely the blocking uplink that tripped the task
   * watchdog once already.
   *
   * The card is also the more complete record. The server only hears the
   * board while the PC is awake; the board writes every reading regardless.
   * So the card is the source and the server is the mirror, never the
   * other way round. */
  /* Where `press` sits in the room's own recorded distribution, as a
   * percentage of readings below it. -1 when there is not enough archive to
   * mean anything.
   *
   * This exists because ABSOLUTE pressure thresholds are unusable here: the
   * sensor reports station pressure and reducing it to sea level needs an
   * elevation nobody has entered, so "below 1000 hPa is a low" would be
   * wrong by a fixed offset at every reading. A percentile against the
   * board's own history needs no calibration at all - the offset cancels.
   *
   * Counted in one pass without an array: sorting a month of readings would
   * cost more heap than the Zigbee stack has left to give. */
  int pressurePercentile(int days, int press);

  bool exportBegin(int days);
  bool exportActive() const { return exDays_ > 0; }
  /* One row as "YYYY-MM-DD,HH:MM,temp_c,rh,bat,press_hpa" — the date is
   * prefixed because a bare HH:MM cannot be placed in time by the reader.
   * False when the walk is finished. */
  bool exportNextRow(char *out, size_t cap);
  int exportRowsSent() const { return exRows_; }
  int exportDaysLeft() const { return exDays_; }
  void exportAbort();

private:
  bool exportLoadDay();

  SdStore *sd_ = nullptr;
  char lastDate_[12] = {0};
  /* Export cursor. exDays_ counts DOWN to zero; exDay_ is how many days back
   * from today the current file is, so the walk runs oldest-first and the
   * receiving end gets a series already in order. */
  int exDays_ = 0, exDay_ = 0, exRows_ = 0;
  String exBuf_;
  int exPos_ = 0;
  char exDate_[12] = {0};
};

#endif
