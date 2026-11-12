/* Nocturne C6 — where the free heap goes, one 10 ms window at a time.
 *
 * Pure logic, host-tested. The device (HeapWatch) feeds it the LOWEST free
 * heap of each window together with the activity tags seen during that
 * window; this keeps the histogram of window minima, how often each tag was
 * active in a deep window, and a short ring of the deepest dips.
 *
 * Why windows and not ESP.getMinFreeHeap(): the boot-time low-water mark only
 * ever goes down, so once it reads 0 it says nothing more — not when, not how
 * often, not during what. On this board it read 0 in every boot for a month,
 * stepping down a kilobyte or two at a time over many hours. */
#ifndef NOCT_HEAP_STATS_H
#define NOCT_HEAP_STATS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace heaptag {
/* What the firmware is doing. Set around anything that allocates more than a
 * few hundred bytes, so a dip can be pinned on it. Append only: the names
 * below are indexed by bit. */
enum : uint32_t {
  PARSE = 1u << 0,   /* payload JSON parse (link or lite) */
  LINK = 1u << 1,    /* TelemetryClient::tick: receive + parse + uplink */
  CONNECT = 1u << 2, /* TCP connect / return probe to a hub */
  COVER = 1u << 3,   /* cover fetch over HTTP (cover task) */
  LITE = 1u << 4,    /* lite TLS fetch (lite task) */
  LLM = 1u << 5,     /* LLM request (llm task) */
  PANEL = 1u << 6,   /* board web panel serving a request */
  EXPORT = 1u << 7,  /* climate archive streaming to the hub */
  SD = 1u << 8,      /* SD flush / cover cache */
  ZB = 1u << 9,      /* Zigbee: the loop's tick and the stack's callbacks */
  DRAW = 1u << 10,   /* scene draw + framebuffer push */
  BRAIN = 1u << 11,  /* wolf brain tick (phrases, speech) */
  SHOT = 1u << 12,    /* screenshot to the card */
  CONSOLE = 1u << 13, /* a console command running */
  ARCHIVE = 1u << 14, /* daily archive: minute commit, rollover, ИСТОРИЯ load */
  CLIMLOG = 1u << 15  /* climate log: append, trends, percentiles, ДОМ load */
};
static const int kCount = 16;
inline const char *name(int bit) {
  static const char *const n[kCount] = {
      "PARSE", "LINK", "CONNECT", "COVER", "LITE", "LLM",     "PANEL",   "EXPORT",
      "SD",    "ZB",   "DRAW",    "BRAIN", "SHOT", "CONSOLE", "ARCHIVE", "CLIMLOG"};
  return bit >= 0 && bit < kCount ? n[bit] : "?";
}
/* "PARSE|DRAW", or "-" for none. Always NUL-terminated, never overruns. */
inline void format(uint32_t tags, char *out, size_t cap) {
  if (!out || !cap) return;
  size_t n = 0;
  out[0] = 0;
  for (int b = 0; b < kCount; b++) {
    if (!(tags & (1u << b))) continue;
    int w = snprintf(out + n, cap - n, "%s%s", n ? "|" : "", name(b));
    if (w < 0 || (size_t)w >= cap - n) {
      out[cap - 1] = 0;
      return;
    }
    n += (size_t)w;
  }
  if (!n) snprintf(out, cap, "-");
}
} // namespace heaptag

struct HeapStats {
  static const int kBuckets = 8;
  static const int kTags = 16;
  static const int kDips = 16;
  /* Two dips closer than this are one event unless the second is deeper —
   * otherwise one slow trough fills the whole ring. */
  static const uint32_t kDipGapMs = 1000;

  /* Upper edges of buckets 0..6 in bytes; bucket 7 is everything above. */
  static uint32_t edge(int i) {
    static const uint32_t e[kBuckets - 1] = {1024,  2048,  4096, 8192,
                                             12288, 16384, 24576};
    return e[i];
  }
  static int bucketOf(uint32_t bytes) {
    for (int i = 0; i < kBuckets - 1; i++)
      if (bytes < edge(i)) return i;
    return kBuckets - 1;
  }

  struct Dip {
    uint32_t ms;
    uint32_t minBytes;
    uint32_t tags;
  };

  uint32_t deepBytes = 8192; /* a window below this is "deep" */
  uint32_t windows = 0;
  uint32_t deepWindows = 0;
  uint32_t buckets[kBuckets] = {};
  uint32_t tagAny[kTags] = {};  /* windows in which the tag was active */
  uint32_t tagDeep[kTags] = {}; /* ...and of those, how many were deep */
  /* Mean window minimum overall and while each tag runs: the gap between the
   * two is what that activity costs, even when it never gets near the floor. */
  uint64_t minSum = 0;
  uint64_t tagMinSum[kTags] = {};
  uint32_t tagLow[kTags] = {}; /* lowest window minimum seen with the tag */
  uint32_t lowest = UINT32_MAX;
  uint32_t lowestMs = 0;
  uint32_t lowestTags = 0;
  Dip dips[kDips] = {};
  uint32_t dipsTotal = 0; /* monotonic; the ring slot is dipsTotal % kDips */
  uint32_t lastDipMs = 0;
  uint32_t lastDipMin = UINT32_MAX;

  void add(uint32_t ms, uint32_t minBytes, uint32_t tags) {
    windows++;
    minSum += minBytes;
    buckets[bucketOf(minBytes)]++;
    const bool deep = minBytes < deepBytes;
    if (deep) deepWindows++;
    for (int t = 0; t < kTags; t++) {
      if (!(tags & (1u << t))) continue;
      if (!tagAny[t] || minBytes < tagLow[t]) tagLow[t] = minBytes;
      tagAny[t]++;
      tagMinSum[t] += minBytes;
      if (deep) tagDeep[t]++;
    }
    if (minBytes < lowest) {
      lowest = minBytes;
      lowestMs = ms;
      lowestTags = tags;
    }
    if (!deep) return;
    /* Unsigned subtraction: correct across a millis() wrap. */
    if (dipsTotal == 0 || minBytes < lastDipMin || ms - lastDipMs >= kDipGapMs) {
      dips[dipsTotal % kDips] = Dip{ms, minBytes, tags};
      dipsTotal++;
      lastDipMs = ms;
      lastDipMin = minBytes;
    }
  }

  /* back = 0 is the newest dip; nullptr past the end of what is kept. */
  const Dip *dip(uint32_t back) const {
    uint32_t kept = dipsTotal < (uint32_t)kDips ? dipsTotal : (uint32_t)kDips;
    if (back >= kept) return nullptr;
    return &dips[(dipsTotal - 1 - back) % kDips];
  }

  uint32_t meanMin() const { return windows ? (uint32_t)(minSum / windows) : 0; }
  uint32_t tagMeanMin(int t) const {
    return t >= 0 && t < kTags && tagAny[t] ? (uint32_t)(tagMinSum[t] / tagAny[t]) : 0;
  }

  void reset() {
    uint32_t keepDeep = deepBytes;
    *this = HeapStats();
    deepBytes = keepDeep;
  }
};

/* What the heap is made of at one moment: used and free blocks by size.
 * Taken by walking the heap when the free heap is nearly gone, and on
 * request for comparison — whatever piled up shows as the difference. The
 * 1537–1700 bin exists for WiFi/lwIP frame buffers, which are exactly the
 * kind of thing that piles up in dozens when a radio stalls. */
struct BlockHist {
  static const int kBins = 14;
  /* Inclusive upper edges of bins 0..12; bin 13 is everything larger. */
  static uint32_t edge(int i) {
    static const uint32_t e[kBins - 1] = {32,   64,   128,  256,  512,   1024, 1536,
                                          1700, 2048, 4096, 8192, 16384, 32768};
    return e[i];
  }
  static int binOf(uint32_t size) {
    for (int i = 0; i < kBins - 1; i++)
      if (size <= edge(i)) return i;
    return kBins - 1;
  }

  uint32_t count[kBins] = {};
  uint32_t bytes[kBins] = {};
  uint32_t usedBlocks = 0;
  uint32_t usedBytes = 0;
  uint32_t freeBlocks = 0;
  uint32_t freeBytes = 0;
  uint32_t largestFree = 0;

  void add(uint32_t size, bool used) {
    if (used) {
      int b = binOf(size);
      count[b]++;
      bytes[b] += size;
      usedBlocks++;
      usedBytes += size;
    } else {
      freeBlocks++;
      freeBytes += size;
      if (size > largestFree) largestFree = size;
    }
  }
};

#endif
