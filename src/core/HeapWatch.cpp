#include "core/HeapWatch.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

namespace heapwatch {
namespace {

/* What ESP.getFreeHeap()/getMinFreeHeap() measure, so the numbers line up. */
const uint32_t kCaps = MALLOC_CAP_INTERNAL;
const uint64_t kPeriodUs = 10000; /* 10 ms windows */

struct Fail {
  uint32_t ms;
  uint32_t size;
  uint32_t caps;
  uint32_t tags;
  char task[12];
  const char *fn; /* IDF passes a string literal: safe to keep the pointer */
};
const int kFails = 8;

portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
/* ── guarded by gMux ── */
HeapStats gStats;
uint32_t gBootMin = UINT32_MAX;
uint32_t gPeriodLowest = UINT32_MAX;
uint32_t gPeriodLowestMs = 0;
uint32_t gPeriodLowestTags = 0;
uint32_t gPeriodDeep = 0;
Fail gFails[kFails];
uint32_t gFailsTotal = 0;
/* ── tags: atomics, written from any task ── */
volatile uint32_t gNow = 0;  /* active right now */
volatile uint32_t gSeen = 0; /* active at any moment since the last sample */

bool gStarted = false;
esp_timer_handle_t gTimer = nullptr;

/* Walk the heap when the CURRENT free heap is this low — a pile-up of
 * buffers lasts long enough to be caught in the act; a single transient
 * allocation does not, and the window minimum covers that case. */
const uint32_t kSnapBytes = 6144;
/* ── guarded by gMux ── */
BlockHist gLowSnap;
uint32_t gLowSnapMs = 0;
uint32_t gLowSnapFree = UINT32_MAX;
uint32_t gLowSnapTags = 0;
uint32_t gLowSnaps = 0;
/* Timer task only: the walk fills this, then it is copied under the lock. */
BlockHist gWalkTmp;
uint32_t gLastSnapMs = 0;

bool binBlock(walker_heap_into_t, walker_block_info_t b, void *user) {
  static_cast<BlockHist *>(user)->add((uint32_t)b.size, b.used);
  return true;
}

/* Printing state and snapshots live here, not on the loop task's stack:
 * that stack has under 4 KB of headroom and a HeapStats is ~400 B. */
HeapStats gSnap;
Fail gFailSnap[kFails];
uint32_t gPrintedDips = 0;
uint32_t gPrintedFails = 0;
uint32_t gLastSummaryMs = 0;

uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

/* Runs in whatever task's allocation failed — possibly the WiFi driver or
 * lwIP holding their own locks. Copy a few words and leave: no printing, no
 * allocation, nothing that can block. */
void onAllocFailed(size_t size, uint32_t caps, const char *fn) {
  Fail f;
  f.ms = nowMs();
  f.size = (uint32_t)size;
  f.caps = caps;
  f.tags = gNow | gSeen;
  f.fn = fn;
  const char *tn = xPortInIsrContext() ? "ISR" : pcTaskGetName(nullptr);
  strncpy(f.task, tn ? tn : "?", sizeof(f.task) - 1);
  f.task[sizeof(f.task) - 1] = 0;
  portENTER_CRITICAL_SAFE(&gMux);
  gFails[gFailsTotal % kFails] = f;
  gFailsTotal++;
  portEXIT_CRITICAL_SAFE(&gMux);
}

/* esp_timer task, every 10 ms: the lowest point of the window that just
 * ended, then a fresh window. */
void sample(void *) {
  uint32_t lm = (uint32_t)heap_caps_get_minimum_free_size(kCaps);
  heap_caps_monitor_local_minimum_free_size_stop();
  heap_caps_monitor_local_minimum_free_size_start();
  uint32_t active = gNow;
  uint32_t seen = __atomic_exchange_n(&gSeen, active, __ATOMIC_RELAXED) | active;
  uint32_t ms = nowMs();
  portENTER_CRITICAL(&gMux);
  if (lm < gBootMin) gBootMin = lm;
  if (lm < gPeriodLowest) {
    gPeriodLowest = lm;
    gPeriodLowestMs = ms;
    gPeriodLowestTags = seen;
  }
  if (lm < gStats.deepBytes) gPeriodDeep++;
  gStats.add(ms, lm, seen);
  uint32_t lowFree = gLowSnapFree;
  portEXIT_CRITICAL(&gMux);

  /* Caught in the act: the heap is nearly empty right now. Walk it — rarely
   * (10 s apart unless this is lower than the last snapshot), because the
   * walk holds the heap lock for a few hundred microseconds. */
  uint32_t curFree = (uint32_t)heap_caps_get_free_size(kCaps);
  if (curFree < kSnapBytes &&
      (ms - gLastSnapMs >= 10000 || curFree < lowFree)) {
    gLastSnapMs = ms;
    gWalkTmp = BlockHist();
    heap_caps_walk(kCaps, binBlock, &gWalkTmp);
    portENTER_CRITICAL(&gMux);
    gLowSnap = gWalkTmp;
    gLowSnapMs = ms;
    gLowSnapFree = curFree;
    gLowSnapTags = seen;
    gLowSnaps++;
    portEXIT_CRITICAL(&gMux);
  }
}

void printHist(Print &out, const char *label, const BlockHist &h) {
  out.printf("[HEAP] %s: used %lu B in %lu blocks, free %lu B in %lu blocks "
             "(largest %lu)\n",
             label, (unsigned long)h.usedBytes, (unsigned long)h.usedBlocks,
             (unsigned long)h.freeBytes, (unsigned long)h.freeBlocks,
             (unsigned long)h.largestFree);
  out.printf("[HEAP]   used by size:");
  for (int i = 0; i < BlockHist::kBins; i++) {
    if (!h.count[i]) continue;
    if (i < BlockHist::kBins - 1)
      out.printf(" <=%lu:%lux=%luB", (unsigned long)BlockHist::edge(i),
                 (unsigned long)h.count[i], (unsigned long)h.bytes[i]);
    else
      out.printf(" more:%lux=%luB", (unsigned long)h.count[i],
                 (unsigned long)h.bytes[i]);
  }
  out.println();
}

void printTs(Print &out, uint32_t ms) {
  out.printf("@%lu.%03lus", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
}

void printFail(Print &out, const Fail &f) {
  char tags[96];
  heaptag::format(f.tags, tags, sizeof(tags));
  out.printf("[HEAP] alloc FAILED %lu B caps %08lx task %s via %s ",
             (unsigned long)f.size, (unsigned long)f.caps, f.task,
             f.fn ? f.fn : "?");
  printTs(out, f.ms);
  out.printf(" tags %s\n", tags);
}

void printDip(Print &out, const HeapStats::Dip &d) {
  char tags[96];
  heaptag::format(d.tags, tags, sizeof(tags));
  out.printf("[HEAP] dip %lu B ", (unsigned long)d.minBytes);
  printTs(out, d.ms);
  out.printf(" tags %s\n", tags);
}

} // namespace

void begin() {
  if (gStarted) return;
  gStarted = true;
  gBootMin = (uint32_t)heap_caps_get_minimum_free_size(kCaps);
  heap_caps_register_failed_alloc_callback(onAllocFailed);
  heap_caps_monitor_local_minimum_free_size_start();
  esp_timer_create_args_t a = {};
  a.callback = sample;
  a.name = "heapwatch";
  a.dispatch_method = ESP_TIMER_TASK;
  if (esp_timer_create(&a, &gTimer) == ESP_OK)
    esp_timer_start_periodic(gTimer, kPeriodUs);
  gLastSummaryMs = nowMs();
}

void enter(uint32_t tags) {
  __atomic_fetch_or(&gNow, tags, __ATOMIC_RELAXED);
  __atomic_fetch_or(&gSeen, tags, __ATOMIC_RELAXED);
}

void leave(uint32_t tags) { __atomic_fetch_and(&gNow, ~tags, __ATOMIC_RELAXED); }

uint32_t bootMin() {
  /* The window in progress has not been folded in yet and may be lower. */
  uint32_t cur = (uint32_t)heap_caps_get_minimum_free_size(kCaps);
  if (!gStarted) return cur;
  portENTER_CRITICAL(&gMux);
  uint32_t v = gBootMin;
  portEXIT_CRITICAL(&gMux);
  return cur < v ? cur : v;
}

uint32_t failCount() { return gFailsTotal; }

void poll(Print &out, uint32_t summaryMs) {
  if (!gStarted) return;
  /* Unlocked peeks: a stale read only delays a print by one loop. */
  if (gStats.dipsTotal != gPrintedDips || gFailsTotal != gPrintedFails) {
    portENTER_CRITICAL(&gMux);
    gSnap = gStats;
    memcpy(gFailSnap, gFails, sizeof(gFails));
    uint32_t failsTotal = gFailsTotal;
    portEXIT_CRITICAL(&gMux);
    /* Oldest first, and only what the rings still hold. */
    uint32_t newDips = gSnap.dipsTotal - gPrintedDips;
    if (newDips > (uint32_t)HeapStats::kDips) newDips = HeapStats::kDips;
    for (uint32_t back = newDips; back-- > 0;) {
      const HeapStats::Dip *d = gSnap.dip(back);
      if (d) printDip(out, *d);
    }
    gPrintedDips = gSnap.dipsTotal;
    uint32_t newFails = failsTotal - gPrintedFails;
    if (newFails > (uint32_t)kFails) {
      out.printf("[HEAP] %lu failed allocations not shown (ring of %d)\n",
                 (unsigned long)(newFails - kFails), kFails);
      newFails = kFails;
    }
    for (uint32_t i = failsTotal - newFails; i < failsTotal; i++)
      printFail(out, gFailSnap[i % kFails]);
    gPrintedFails = failsTotal;
  }
  /* A heap walk taken in the act goes out at once: overnight the serial log
   * is the only witness. */
  static uint32_t printedSnaps = 0;
  if (gLowSnaps != printedSnaps) {
    static BlockHist low;
    portENTER_CRITICAL(&gMux);
    low = gLowSnap;
    uint32_t lowMs = gLowSnapMs, lowFree = gLowSnapFree, lowTags = gLowSnapTags;
    printedSnaps = gLowSnaps;
    portEXIT_CRITICAL(&gMux);
    char tags[96];
    heaptag::format(lowTags, tags, sizeof(tags));
    out.printf("[HEAP] snapshot #%lu at %lu B free ", (unsigned long)printedSnaps,
               (unsigned long)lowFree);
    printTs(out, lowMs);
    out.printf(" tags %s\n", tags);
    printHist(out, "at low", low);
  }
  uint32_t ms = nowMs();
  if (summaryMs && ms - gLastSummaryMs >= summaryMs) {
    gLastSummaryMs = ms;
    portENTER_CRITICAL(&gMux);
    uint32_t low = gPeriodLowest, lowMs = gPeriodLowestMs,
             lowTags = gPeriodLowestTags, deep = gPeriodDeep;
    gPeriodLowest = UINT32_MAX;
    gPeriodDeep = 0;
    portEXIT_CRITICAL(&gMux);
    char tags[96];
    heaptag::format(lowTags, tags, sizeof(tags));
    out.printf("[HEAP] last %lu s: lowest %lu B ", (unsigned long)(summaryMs / 1000),
               (unsigned long)low);
    printTs(out, lowMs);
    out.printf(" tags %s; deep windows %lu; free %u largest %u boot-min %lu fails %lu\n",
               tags, (unsigned long)deep, (unsigned)heap_caps_get_free_size(kCaps),
               (unsigned)heap_caps_get_largest_free_block(kCaps),
               (unsigned long)bootMin(), (unsigned long)gFailsTotal);
  }
}

void report(Print &out) {
  portENTER_CRITICAL(&gMux);
  gSnap = gStats;
  memcpy(gFailSnap, gFails, sizeof(gFails));
  uint32_t failsTotal = gFailsTotal;
  portEXIT_CRITICAL(&gMux);

  multi_heap_info_t info;
  heap_caps_get_info(&info, kCaps);
  out.printf("[HEAP] free %u B, largest %u B, boot-min %lu B; blocks: %u used, %u free\n",
             (unsigned)info.total_free_bytes, (unsigned)info.largest_free_block,
             (unsigned long)bootMin(), (unsigned)info.allocated_blocks,
             (unsigned)info.free_blocks);
  uint32_t w = gSnap.windows ? gSnap.windows : 1;
  out.printf("[HEAP] %lu windows of 10 ms (%lu s); below %lu B: %lu (%lu.%03lu%%)\n",
             (unsigned long)gSnap.windows, (unsigned long)(gSnap.windows / 100),
             (unsigned long)gSnap.deepBytes, (unsigned long)gSnap.deepWindows,
             (unsigned long)(gSnap.deepWindows * 100UL / w),
             (unsigned long)((gSnap.deepWindows * 100000ULL / w) % 1000));
  out.print("[HEAP] window minima:");
  for (int i = 0; i < HeapStats::kBuckets; i++) {
    if (i < HeapStats::kBuckets - 1)
      out.printf(" <%luK:%lu", (unsigned long)(HeapStats::edge(i) / 1024),
                 (unsigned long)gSnap.buckets[i]);
    else
      out.printf(" more:%lu", (unsigned long)gSnap.buckets[i]);
  }
  out.println();
  /* For each tag: how much of the time it runs, and how often the heap is
   * deep WHILE it runs. A tag whose deep ratio towers over the overall one
   * is the suspect; one that merely runs a lot is not. */
  out.printf("[HEAP] mean window minimum %lu B\n", (unsigned long)gSnap.meanMin());
  out.println("[HEAP] tag      active%   mean-min  lowest   deep%");
  for (int t = 0; t < heaptag::kCount; t++) {
    uint32_t any = gSnap.tagAny[t];
    if (!any) continue;
    out.printf("[HEAP]   %-8s %3lu.%02lu%%   %6lu  %6lu   %lu.%03lu%%\n",
               heaptag::name(t), (unsigned long)(any * 100ULL / w),
               (unsigned long)((any * 10000ULL / w) % 100),
               (unsigned long)gSnap.tagMeanMin(t), (unsigned long)gSnap.tagLow[t],
               (unsigned long)(gSnap.tagDeep[t] * 100ULL / any),
               (unsigned long)((gSnap.tagDeep[t] * 100000ULL / any) % 1000));
  }
  if (gSnap.lowest != UINT32_MAX) {
    char tags[96];
    heaptag::format(gSnap.lowestTags, tags, sizeof(tags));
    out.printf("[HEAP] lowest window %lu B ", (unsigned long)gSnap.lowest);
    printTs(out, gSnap.lowestMs);
    out.printf(" tags %s\n", tags);
  }
  out.println("[HEAP] dips, newest first:");
  for (uint32_t back = 0;; back++) {
    const HeapStats::Dip *d = gSnap.dip(back);
    if (!d) break;
    printDip(out, *d);
  }
  out.printf("[HEAP] failed allocations: %lu, newest first:\n", (unsigned long)failsTotal);
  uint32_t kept = failsTotal < (uint32_t)kFails ? failsTotal : (uint32_t)kFails;
  for (uint32_t i = 0; i < kept; i++)
    printFail(out, gFailSnap[(failsTotal - 1 - i) % kFails]);
  out.printf("[HEAP] uptime ");
  printTs(out, nowMs());
  out.println();
}

void walk(Print &out) {
  static BlockHist now;
  now = BlockHist();
  heap_caps_walk(kCaps, binBlock, &now);
  static BlockHist low;
  portENTER_CRITICAL(&gMux);
  low = gLowSnap;
  uint32_t lowMs = gLowSnapMs, lowFree = gLowSnapFree, lowTags = gLowSnapTags,
           snaps = gLowSnaps;
  portEXIT_CRITICAL(&gMux);
  printHist(out, "now", now);
  if (!snaps) {
    out.printf("[HEAP] no snapshot yet: free never fell under %lu B\n",
               (unsigned long)kSnapBytes);
    return;
  }
  char tags[96];
  heaptag::format(lowTags, tags, sizeof(tags));
  out.printf("[HEAP] snapshot #%lu at %lu B free ", (unsigned long)snaps,
             (unsigned long)lowFree);
  printTs(out, lowMs);
  out.printf(" tags %s\n", tags);
  printHist(out, "at low", low);
}

void resetStats() {
  portENTER_CRITICAL(&gMux);
  gStats.reset();
  gPeriodLowest = UINT32_MAX;
  gPeriodDeep = 0;
  portEXIT_CRITICAL(&gMux);
  gPrintedDips = 0;
}

void setDeep(uint32_t bytes) {
  portENTER_CRITICAL(&gMux);
  gStats.reset();
  gStats.deepBytes = bytes;
  gPeriodLowest = UINT32_MAX;
  gPeriodDeep = 0;
  portEXIT_CRITICAL(&gMux);
  gPrintedDips = 0;
}

} // namespace heapwatch
