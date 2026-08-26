#include "storage/ClimateLog.h"

#include <limits.h>
#include <time.h>

#include "core/config.h"
#include "storage/SdStore.h"

void ClimateLog::Series::clear() {
  for (int i = 0; i < kCols; i++) {
    temp10[i] = INT_MIN;
    hum[i] = -1;
  }
  filled = 0;
  rows = 0;
  loMin10 = 0;
  hiMax10 = 0;
}

void ClimateLog::append(const char *date, const char *hm, const ZbSensor &z) {
  if (!sd_ || !sd_->ok() || !date || !*date || !hm || !*hm) return;
  if (z.temp10 == -32768) return; /* nothing worth a row yet */

  char path[40];
  snprintf(path, sizeof(path), "%s/%s.csv", dirPath(), date);
  /* A header per file, written once, so the CSV opens in anything without the
   * reader having to be told what the columns are. */
  if (strcmp(lastDate_, date) != 0) {
    snprintf(lastDate_, sizeof(lastDate_), "%s", date);
    if (!sd_->exists(path))
      sd_->enqueueAppend(path, "time,temp_c,rh,bat,press_hpa");
  }

  int whole = z.temp10 / 10, frac = z.temp10 % 10;
  if (frac < 0) frac = -frac;
  char row[64];
  snprintf(row, sizeof(row), "%s,%d.%d,%d,%d,%d", hm, whole, frac, z.humidity,
           z.battery, z.pressure);
  sd_->enqueueAppend(path, row);
}

bool ClimateLog::loadSeries(int days, Series &out) {
  out.clear();
  if (!sd_ || !sd_->ok()) return false;
  if (days < 1) days = 1;
  if (days > 30) days = 30;

  time_t nowT = time(nullptr);
  if (nowT < 1700000000L) return false; /* no clock, no dated files to find */

  /* Bucket accumulators. Sums, not last-wins: a bucket that saw three reports
   * should show what the room was, not whichever arrived last. */
  long tSum[kCols] = {0};
  int tCnt[kCols] = {0};
  long hSum[kCols] = {0};
  int hCnt[kCols] = {0};

  /* The whole window in minutes, mapped onto kCols columns. */
  const long spanMin = (long)days * 24L * 60L;
  struct tm nowTm;
  localtime_r(&nowT, &nowTm);
  long nowMin = nowTm.tm_hour * 60L + nowTm.tm_min;

  int filesRead = 0;
  for (int d = days - 1; d >= 0; d--) {
    time_t t = nowT - (time_t)d * 86400;
    struct tm tmv;
    if (!localtime_r(&t, &tmv)) continue;
    char date[12], path[40];
    strftime(date, sizeof(date), "%Y-%m-%d", &tmv);
    snprintf(path, sizeof(path), "%s/%s.csv", dirPath(), date);
    if (!sd_->exists(path)) continue;

    String text;
    if (!sd_->readAll(path, text, NOCT_SD_READ_MAX) || !text.length()) continue;
    filesRead++;

    int start = 0;
    while (start < (int)text.length()) {
      int nl = text.indexOf('\n', start);
      String line = (nl < 0) ? text.substring(start) : text.substring(start, nl);
      start = (nl < 0) ? text.length() : nl + 1;
      line.trim();
      if (!line.length() || line.startsWith("time")) continue;

      /* HH:MM,tt.t,rh,bat,press */
      int c1 = line.indexOf(',');
      if (c1 < 4) continue;
      int hh = line.substring(0, 2).toInt();
      int mm = line.substring(3, 5).toInt();
      int c2 = line.indexOf(',', c1 + 1);
      if (c2 < 0) continue;
      float tc = line.substring(c1 + 1, c2).toFloat();
      int c3 = line.indexOf(',', c2 + 1);
      int rh = (c3 < 0) ? -1 : line.substring(c2 + 1, c3).toInt();

      /* Minutes back from now: today's rows are `d` days plus the clock
       * difference; older files are whole days plus their own time of day. */
      long ageMin = (long)d * 1440L + (nowMin - (hh * 60L + mm));
      if (ageMin < 0 || ageMin >= spanMin) continue;
      int col = kCols - 1 - (int)(ageMin * kCols / spanMin);
      if (col < 0 || col >= kCols) continue;

      tSum[col] += (long)(tc * 10.0f + (tc < 0 ? -0.5f : 0.5f));
      tCnt[col]++;
      if (rh >= 0) {
        hSum[col] += rh;
        hCnt[col]++;
      }
      out.rows++;
    }
  }
  if (!filesRead) return false;

  bool any = false;
  for (int i = 0; i < kCols; i++) {
    if (tCnt[i]) {
      out.temp10[i] = (int)(tSum[i] / tCnt[i]);
      if (!any) {
        out.loMin10 = out.hiMax10 = out.temp10[i];
        any = true;
      } else {
        if (out.temp10[i] < out.loMin10) out.loMin10 = out.temp10[i];
        if (out.temp10[i] > out.hiMax10) out.hiMax10 = out.temp10[i];
      }
      out.filled++;
    }
    if (hCnt[i]) out.hum[i] = (int)(hSum[i] / hCnt[i]);
  }
  Serial.printf("[CLIM] %d day(s): %d row(s) into %d bucket(s)\n", days,
                out.rows, out.filled);
  return out.filled > 0;
}

bool ClimateLog::trend(int hours, int &dTemp10, int &dHum, int &dPress10) {
  dTemp10 = dHum = dPress10 = 0;
  if (!sd_ || !sd_->ok()) return false;
  if (hours < 1) hours = 1;
  time_t nowT = time(nullptr);
  if (nowT < 1700000000L) return false;

  struct tm nowTm;
  localtime_r(&nowT, &nowTm);
  const long nowMin = nowTm.tm_hour * 60L + nowTm.tm_min;
  const long wantAge = (long)hours * 60L;

  /* Newest row, and the row closest to `hours` ago. Closest rather than
   * "first older than": a sensor that speaks hourly may have nothing at
   * exactly -3 h, and refusing to answer because the sample is at -3 h 20 min
   * would mean never answering at all. */
  bool haveNew = false, haveOld = false;
  long newAge = 1 << 30, oldErr = 1 << 30;
  int nT = 0, nH = 0, nP = 0, oT = 0, oH = 0, oP = 0;

  /* Two files at most: a 3-hour window at 01:00 reaches into yesterday. */
  for (int d = 0; d <= 1; d++) {
    time_t t = nowT - (time_t)d * 86400;
    struct tm tmv;
    if (!localtime_r(&t, &tmv)) continue;
    char date[12], path[40];
    strftime(date, sizeof(date), "%Y-%m-%d", &tmv);
    snprintf(path, sizeof(path), "%s/%s.csv", dirPath(), date);
    if (!sd_->exists(path)) continue;
    String text;
    if (!sd_->readAll(path, text, NOCT_SD_READ_MAX) || !text.length()) continue;

    int start = 0;
    while (start < (int)text.length()) {
      int nl = text.indexOf('\n', start);
      String line = (nl < 0) ? text.substring(start) : text.substring(start, nl);
      start = (nl < 0) ? text.length() : nl + 1;
      line.trim();
      if (!line.length() || line.startsWith("time")) continue;

      int c1 = line.indexOf(',');
      if (c1 < 4) continue;
      long age = (long)d * 1440L +
                 (nowMin - (line.substring(0, 2).toInt() * 60L +
                            line.substring(3, 5).toInt()));
      if (age < 0) continue; /* a row stamped in the future: clock moved */

      int c2 = line.indexOf(',', c1 + 1);
      if (c2 < 0) continue;
      int c3 = line.indexOf(',', c2 + 1);
      int c4 = (c3 < 0) ? -1 : line.indexOf(',', c3 + 1);
      float tc = line.substring(c1 + 1, c2).toFloat();
      int t10 = (int)(tc * 10.0f + (tc < 0 ? -0.5f : 0.5f));
      int rh = (c3 < 0) ? -1 : line.substring(c2 + 1, c3).toInt();
      int hpa = (c4 < 0) ? -1 : line.substring(c4 + 1).toInt();

      if (age < newAge) {
        newAge = age;
        nT = t10; nH = rh; nP = hpa;
        haveNew = true;
      }
      long err = age > wantAge ? age - wantAge : wantAge - age;
      if (err < oldErr) {
        oldErr = err;
        oT = t10; oH = rh; oP = hpa;
        haveOld = true;
      }
    }
  }

  /* The comparison point has to actually be old. Half the requested window is
   * the loosest thing still worth calling a trend; below that the two samples
   * are the same weather and the "change" is sensor noise. */
  if (!haveNew || !haveOld || oldErr > wantAge / 2) return false;

  dTemp10 = nT - oT;
  if (nH >= 0 && oH >= 0) dHum = nH - oH;
  if (nP > 0 && oP > 0) dPress10 = (nP - oP) * 10;
  return true;
}

/* ── export ─────────────────────────────────────────────────────────────── */

bool ClimateLog::exportBegin(int days) {
  exportAbort();
  if (!sd_ || !sd_->ok()) return false;
  if (time(nullptr) < 1700000000L) return false; /* undated files, no walk */
  if (days < 1) days = 1;
  if (days > 60) days = 60; /* two months is ~4000 rows; past that, take the
                             * card out and read it directly */
  exDays_ = days;
  exDay_ = days - 1; /* oldest first, so the receiver never has to sort */
  exRows_ = 0;
  return true;
}

void ClimateLog::exportAbort() {
  /* Clears the CURSOR, deliberately not the row count. The walk ends by
   * calling this from inside exportNextRow, so zeroing the total here meant
   * the caller read 0 the instant the transfer succeeded - the board
   * announced "archive uploaded: 0 rows" while 354 of them were arriving at
   * the other end. exportBegin() resets the count; until then it means "rows
   * sent by the last export", which is the only reading a caller wants. */
  exDays_ = exDay_ = exPos_ = 0;
  exBuf_ = "";
  exDate_[0] = 0;
}

/* Pull the next dated file into the buffer, skipping days with no file. */
bool ClimateLog::exportLoadDay() {
  while (exDays_ > 0) {
    time_t t = time(nullptr) - (time_t)exDay_ * 86400;
    struct tm tmv;
    bool got = localtime_r(&t, &tmv) != nullptr;
    char path[40];
    if (got) {
      strftime(exDate_, sizeof(exDate_), "%Y-%m-%d", &tmv);
      snprintf(path, sizeof(path), "%s/%s.csv", dirPath(), exDate_);
    }
    /* Step the cursor BEFORE any early exit, or a missing file loops here
     * forever and the export never finishes. */
    exDay_--;
    if (exDay_ < 0) exDays_ = 0; /* this is the last file */
    if (got && sd_->exists(path)) {
      exBuf_ = "";
      exPos_ = 0;
      if (sd_->readAll(path, exBuf_, NOCT_SD_READ_MAX) && exBuf_.length())
        return true;
    }
    if (exDays_ == 0) break;
  }
  return false;
}

bool ClimateLog::exportNextRow(char *out, size_t cap) {
  if (!out || cap < 24) return false;
  for (;;) {
    if (exPos_ >= (int)exBuf_.length()) {
      if (exDays_ <= 0) {
        exportAbort();
        return false;
      }
      if (!exportLoadDay()) {
        if (exDays_ <= 0) {
          exportAbort();
          return false;
        }
        continue;
      }
    }
    int nl = exBuf_.indexOf('\n', exPos_);
    String line =
        (nl < 0) ? exBuf_.substring(exPos_) : exBuf_.substring(exPos_, nl);
    exPos_ = (nl < 0) ? exBuf_.length() : nl + 1;
    line.trim();
    /* The per-file header is for whoever opens the CSV on a laptop; it is
     * noise on the wire, where the schema is already agreed. */
    if (!line.length() || line.startsWith("time")) continue;
    snprintf(out, cap, "%s,%s", exDate_, line.c_str());
    exRows_++;
    return true;
  }
}

bool ClimateLog::percentiles(int days, int temp10, int rh, int press,
                             int &pTemp, int &pHum, int &pPress) {
  pTemp = pHum = pPress = -1;
  if (!sd_ || !sd_->ok()) return false;
  if (days < 1) days = 1;
  if (days > 60) days = 60;
  time_t nowT = time(nullptr);
  if (nowT < 1700000000L) return false;

  long belowT = 0, totT = 0, belowH = 0, totH = 0, belowP = 0, totP = 0;
  for (int d = 0; d < days; d++) {
    time_t t = nowT - (time_t)d * 86400;
    struct tm tmv;
    if (!localtime_r(&t, &tmv)) continue;
    char date[12], path[40];
    strftime(date, sizeof(date), "%Y-%m-%d", &tmv);
    snprintf(path, sizeof(path), "%s/%s.csv", dirPath(), date);
    if (!sd_->exists(path)) continue;
    String text;
    if (!sd_->readAll(path, text, NOCT_SD_READ_MAX) || !text.length()) continue;

    int start = 0;
    while (start < (int)text.length()) {
      int nl = text.indexOf('\n', start);
      String line =
          (nl < 0) ? text.substring(start) : text.substring(start, nl);
      start = (nl < 0) ? text.length() : nl + 1;
      line.trim();
      if (!line.length() || line.startsWith("time")) continue;

      /* HH:MM,temp,rh,bat,press */
      int c1 = line.indexOf(',');
      if (c1 < 4) continue;
      int c2 = line.indexOf(',', c1 + 1);
      if (c2 < 0) continue;
      int c3 = line.indexOf(',', c2 + 1);
      if (c3 < 0) continue;
      int c4 = line.indexOf(',', c3 + 1);
      if (c4 < 0) continue;

      float tc = line.substring(c1 + 1, c2).toFloat();
      int t10 = (int)(tc * 10.0f + (tc < 0 ? -0.5f : 0.5f));
      int h = line.substring(c2 + 1, c3).toInt();
      int p = line.substring(c4 + 1).toInt();

      if (temp10 != -32768) {
        totT++;
        if (t10 < temp10) belowT++;
      }
      if (rh >= 0 && h >= 0) {
        totH++;
        if (h < rh) belowH++;
      }
      /* -1 is "this sensor has no barometer", not a reading of -1 hPa. */
      if (press > 0 && p > 0) {
        totP++;
        if (p < press) belowP++;
      }
    }
  }
  /* Under a day of readings is not a distribution, it is a handful of
   * numbers, and a percentile computed from it would look authoritative
   * while meaning nothing. */
  if (totT >= 24) pTemp = (int)(belowT * 100 / totT);
  if (totH >= 24) pHum = (int)(belowH * 100 / totH);
  if (totP >= 24) pPress = (int)(belowP * 100 / totP);
  return pTemp >= 0 || pHum >= 0 || pPress >= 0;
}
