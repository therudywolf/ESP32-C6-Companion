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
