/*
 * Nocturne C6 — daily rollup and baseline watch over the SD archive.
 *
 * /logs/YYYY-MM-DD.csv holds a row a minute. That is the raw material; this is
 * what makes it useful: one row per DAY in /logs/daily.csv, and a comparison of
 * today against the board's own recent history.
 *
 * The metric that matters is IDLE temperature — the average CPU/GPU temperature
 * over minutes when the machine was doing nothing. Load temperature says what
 * the owner was doing; idle temperature says what the cooling can do, and it
 * creeps up as dust collects, months before anything throttles. A board that
 * has been watching for three weeks can say "your GPU idles six degrees hotter
 * than it used to", which is the one thing here that no amount of live
 * telemetry can produce.
 *
 * The running day survives a reboot via a small snapshot, because this board
 * gets restarted often enough that an in-RAM-only accumulator would lose most
 * of its days.
 */
#ifndef NOCT_ARCHIVE_H
#define NOCT_ARCHIVE_H

#include <Arduino.h>

#include "core/HourHistory.h"

class SdStore;

class Archive {
public:
  void begin(SdStore *sd);
  /* Feed one committed minute. `date` is "YYYY-MM-DD"; a change rolls the day
   * over, writes its summary and runs the baseline check. */
  void onMinute(const char *date, int ct, int gt, int cl, int gl, int ram);

  /* A finding worth telling the owner about, consumed once. Empty when there
   * is nothing to say — which is most days, and should be. */
  bool takeFinding(String &out);

  /* Parse /logs/daily.csv into drawable series — one point per DAY, newest
   * last. The daily file is ~60 B a row, so a month is under 2 KB and fits a
   * single read; the minute files never have to be touched for this. Returns
   * how many days were loaded. Call it when the view is opened, not per frame. */
  int loadSeries(int maxDays = 30);
  const GraphSet &series() const { return series_; }
  int seriesDays() const { return seriesDays_; }

  /* Yesterday's summary line for the wolf's journal prompt, "" if unknown. */
  const String &lastSummary() const { return lastSummary_; }
  const String &lastDate() const { return dayDate_; }

private:
  void rollover();
  void writeDailyRow();
  void checkBaseline();
  void save();
  void restore();

  SdStore *sd_ = nullptr;
  String dayDate_;   /* the day currently being accumulated */
  String lastSummary_;
  String finding_;
  GraphSet series_;   /* daily points for the ИСТОРИЯ archive view */
  int seriesDays_ = 0;

  /* accumulators for the running day */
  struct Acc {
    uint32_t mins = 0;
    uint32_t idleMins = 0;
    long sct = 0, sgt = 0, scl = 0, sgl = 0, sram = 0;
    long ictSum = 0, igtSum = 0; /* temps sampled only while idle */
    int ctMax = 0, gtMax = 0;
  } acc_;
};

#endif
