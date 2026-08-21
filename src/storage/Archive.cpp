#include "storage/Archive.h"

#include "core/config.h"
#include "storage/SdStore.h"

namespace {
const char *kDaily = "/logs/daily.csv";
const char *kState = "/logs/today.bin";
const char *kHeader =
    "date,mins,cpu_avg,cpu_max,gpu_avg,gpu_max,cpu_pct,gpu_pct,ram_pct,"
    "idle_mins,cpu_idle,gpu_idle";

/* "Idle" is the owner's machine doing nothing much, not the board. Both loads
 * low for a whole minute is a conservative definition — it under-counts rather
 * than polluting the baseline with light work. */
bool isIdleMinute(int cl, int gl) { return cl < 15 && gl < 15; }

/* How many past days the baseline averages over, and how far today has to
 * drift before it is worth mentioning. 5 C is well outside day-to-day noise
 * (which is ~1-2 C from ambient) and well inside "you should look at this". */
const int kBaselineDays = 14;
const int kMinBaselineDays = 5; /* below this there is no baseline worth having */
const int kDriftC = 5;

struct StateBlob {
  uint32_t magic;
  char date[12];
  uint32_t mins, idleMins;
  long sct, sgt, scl, sgl, sram, ictSum, igtSum;
  int ctMax, gtMax;
};
const uint32_t kMagic = 0x41524331; /* "ARC1" */
} // namespace

void Archive::begin(SdStore *sd) {
  sd_ = sd;
  restore();
}

void Archive::restore() {
  if (!sd_) return;
  StateBlob b{};
  if (!sd_->readBlob(kState, &b, sizeof(b)) || b.magic != kMagic) return;
  b.date[sizeof(b.date) - 1] = '\0';
  dayDate_ = b.date;
  acc_.mins = b.mins;
  acc_.idleMins = b.idleMins;
  acc_.sct = b.sct;
  acc_.sgt = b.sgt;
  acc_.scl = b.scl;
  acc_.sgl = b.sgl;
  acc_.sram = b.sram;
  acc_.ictSum = b.ictSum;
  acc_.igtSum = b.igtSum;
  acc_.ctMax = b.ctMax;
  acc_.gtMax = b.gtMax;
  Serial.printf("[ARC] resumed %s (%lu min so far)\n", dayDate_.c_str(),
                (unsigned long)acc_.mins);
}

void Archive::save() {
  if (!sd_ || !dayDate_.length()) return;
  StateBlob b{};
  b.magic = kMagic;
  strncpy(b.date, dayDate_.c_str(), sizeof(b.date) - 1);
  b.mins = acc_.mins;
  b.idleMins = acc_.idleMins;
  b.sct = acc_.sct;
  b.sgt = acc_.sgt;
  b.scl = acc_.scl;
  b.sgl = acc_.sgl;
  b.sram = acc_.sram;
  b.ictSum = acc_.ictSum;
  b.igtSum = acc_.igtSum;
  b.ctMax = acc_.ctMax;
  b.gtMax = acc_.gtMax;
  sd_->writeBlob(kState, &b, sizeof(b));
}

void Archive::onMinute(const char *date, int ct, int gt, int cl, int gl,
                       int ram) {
  if (!sd_ || !date || !*date) return;
  if (dayDate_.length() && dayDate_ != date) rollover();
  if (!dayDate_.length()) dayDate_ = date;

  acc_.mins++;
  acc_.sct += ct;
  acc_.sgt += gt;
  acc_.scl += cl;
  acc_.sgl += gl;
  acc_.sram += ram;
  if (ct > acc_.ctMax) acc_.ctMax = ct;
  if (gt > acc_.gtMax) acc_.gtMax = gt;
  if (isIdleMinute(cl, gl)) {
    acc_.idleMins++;
    acc_.ictSum += ct;
    acc_.igtSum += gt;
  }
  save(); /* a reboot mid-day must not cost the day */
}

void Archive::writeDailyRow() {
  if (acc_.mins == 0) return;
  int idleCt = acc_.idleMins ? (int)(acc_.ictSum / (long)acc_.idleMins) : -1;
  int idleGt = acc_.idleMins ? (int)(acc_.igtSum / (long)acc_.idleMins) : -1;
  char row[160];
  snprintf(row, sizeof(row), "%s,%lu,%ld,%d,%ld,%d,%ld,%ld,%ld,%lu,%d,%d",
           dayDate_.c_str(), (unsigned long)acc_.mins,
           acc_.sct / (long)acc_.mins, acc_.ctMax, acc_.sgt / (long)acc_.mins,
           acc_.gtMax, acc_.scl / (long)acc_.mins, acc_.sgl / (long)acc_.mins,
           acc_.sram / (long)acc_.mins, (unsigned long)acc_.idleMins, idleCt,
           idleGt);
  if (!sd_->exists(kDaily)) sd_->enqueueAppend(kDaily, kHeader);
  sd_->enqueueAppend(kDaily, row);

  /* Prose for the wolf's journal prompt — a model does more with a sentence
   * than with a CSV row. */
  char sum[320]; /* Cyrillic is 2 B/char - this sentence is ~200 B before numbers */
  snprintf(sum, sizeof(sum),
           "за день: процессор в среднем %ld градусов (пик %d), видеокарта %ld "
           "(пик %d), загрузка CPU %ld%% и GPU %ld%%, память %ld%%, "
           "наблюдений %lu минут",
           acc_.sct / (long)acc_.mins, acc_.ctMax, acc_.sgt / (long)acc_.mins,
           acc_.gtMax, acc_.scl / (long)acc_.mins, acc_.sgl / (long)acc_.mins,
           acc_.sram / (long)acc_.mins, (unsigned long)acc_.mins);
  lastSummary_ = sum;
  Serial.printf("[ARC] %s closed: %s\n", dayDate_.c_str(), sum);
}

/* Compare the day just closed against the board's own recent history. This is
 * the whole point of keeping an archive: absolute numbers mean little, drift
 * against your own past means a lot. */
void Archive::checkBaseline() {
  if (acc_.idleMins < 30) return; /* too little idle time to judge */
  String text;
  /* daily.csv is ~60 B a row, so the tail is plenty of days. */
  if (!sd_->readAll(kDaily, text, NOCT_SD_READ_MAX) || text.length() == 0)
    return;

  long sumCt = 0, sumGt = 0;
  int days = 0;
  int start = 0;
  /* Walk every complete line, keeping the most recent kBaselineDays that have
   * a usable idle sample and are not the day we just wrote. */
  while (start < text.length() && days < 400) {
    int nl = text.indexOf('\n', start);
    String line = (nl < 0) ? text.substring(start) : text.substring(start, nl);
    start = (nl < 0) ? text.length() : nl + 1;
    line.trim();
    if (!line.length() || line.startsWith("date")) continue;
    /* fields: date,mins,...,idle_mins,cpu_idle,gpu_idle — take the last three */
    int c3 = line.lastIndexOf(',');
    int c2 = c3 > 0 ? line.lastIndexOf(',', c3 - 1) : -1;
    int c1 = c2 > 0 ? line.lastIndexOf(',', c2 - 1) : -1;
    if (c1 < 0) continue;
    String d = line.substring(0, line.indexOf(','));
    if (d == dayDate_) continue; /* today is the thing being judged */
    long idleMins = line.substring(c1 + 1, c2).toInt();
    int ict = line.substring(c2 + 1, c3).toInt();
    int igt = line.substring(c3 + 1).toInt();
    if (idleMins < 30 || ict <= 0 || igt <= 0) continue;
    sumCt += ict;
    sumGt += igt;
    days++;
  }
  if (days < kMinBaselineDays) return;
  /* only the most recent window matters; days beyond it dilute a real trend */
  if (days > kBaselineDays) days = kBaselineDays;

  int baseCt = (int)(sumCt / days), baseGt = (int)(sumGt / days);
  int todayCt = (int)(acc_.ictSum / (long)acc_.idleMins);
  int todayGt = (int)(acc_.igtSum / (long)acc_.idleMins);
  int dCt = todayCt - baseCt, dGt = todayGt - baseGt;

  if (dGt >= kDriftC)
    finding_ = "видеокарта в простое стала горячее на " + String(dGt) +
               " градусов, чем обычно за последние " + String(days) +
               " дней — похоже, пора чистить от пыли";
  else if (dCt >= kDriftC)
    finding_ = "процессор в простое стал горячее на " + String(dCt) +
               " градусов, чем обычно за последние " + String(days) +
               " дней — стоит проверить охлаждение";
  if (finding_.length()) Serial.printf("[ARC] finding: %s\n", finding_.c_str());
}

void Archive::rollover() {
  writeDailyRow();
  checkBaseline();
  acc_ = Acc();
  dayDate_ = "";
  if (sd_) sd_->remove(kState);
}

/* daily.csv -> five series the ИСТОРИЯ scene can draw with the code it already
 * has. Averages, not peaks: a month of peaks is a month of spikes and tells you
 * nothing about the trend. */
int Archive::loadSeries(int maxDays) {
  seriesDays_ = 0;
  if (!sd_) return 0;
  if (maxDays < 1 || maxDays > HourGraph::N) maxDays = HourGraph::N;
  series_.ct.setCap(maxDays);
  series_.gt.setCap(maxDays);
  series_.cl.setCap(maxDays);
  series_.gl.setCap(maxDays);
  series_.ram.setCap(maxDays);

  String text;
  if (!sd_->readAll(kDaily, text, NOCT_SD_READ_MAX) || !text.length()) return 0;
  int start = 0;
  while (start < text.length()) {
    int nl = text.indexOf('\n', start);
    String line = (nl < 0) ? text.substring(start) : text.substring(start, nl);
    start = (nl < 0) ? text.length() : nl + 1;
    line.trim();
    if (!line.length() || line.startsWith("date")) continue;
    /* date,mins,cpu_avg,cpu_max,gpu_avg,gpu_max,cpu_pct,gpu_pct,ram_pct,... */
    int f = 0, from = 0;
    int v[9] = {0};
    while (f < 9) {
      int comma = line.indexOf(',', from);
      String piece = (comma < 0) ? line.substring(from) : line.substring(from, comma);
      if (f > 0) v[f] = piece.toInt();
      if (comma < 0) break;
      from = comma + 1;
      f++;
    }
    if (f < 8) continue; /* truncated row */
    series_.ct.push(v[2]);
    series_.gt.push(v[4]);
    series_.cl.push(v[6]);
    series_.gl.push(v[7]);
    series_.ram.push(v[8]);
    seriesDays_++;
  }
  if (seriesDays_ > maxDays) seriesDays_ = maxDays;
  Serial.printf("[ARC] archive view: %d day(s)\n", seriesDays_);
  return seriesDays_;
}

bool Archive::takeFinding(String &out) {
  if (!finding_.length()) return false;
  out = finding_;
  finding_ = "";
  return true;
}
