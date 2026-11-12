/* Nocturne C6 — the heap's low-water mark, per 10 ms window, with blame.
 *
 * ESP.getMinFreeHeap() is a boot-time low-water mark: it only goes down, and
 * on this board it sat at 0 in every boot for a month. A number stuck at the
 * floor answers none of the questions that matter — when, how often, during
 * what. HeapWatch answers them:
 *
 *   - an esp_timer reads IDF's LOCAL minimum every 10 ms and restarts it
 *     (heap_caps_monitor_local_minimum_free_size_*), so every window has its
 *     own minimum; those feed HeapStats (histogram, per-tag attribution,
 *     ring of the deepest dips);
 *   - code that allocates wraps itself in heapwatch::Scope with a heaptag
 *     bit, so a dip carries the list of what was running when it happened;
 *   - a failed-allocation hook records size, caps and the TASK that asked,
 *     which is the one thing a dip cannot tell.
 *
 * Because local monitoring changes what heap_caps_get_minimum_free_size()
 * and ESP.getMinFreeHeap() return (the window, not the boot), every report of
 * "min free heap" must read heapwatch::bootMin() instead. */
#ifndef NOCT_HEAP_WATCH_H
#define NOCT_HEAP_WATCH_H

#include <stdint.h>

#include "core/HeapStats.h"

class Print;

namespace heapwatch {

void begin();
void enter(uint32_t tags);
void leave(uint32_t tags);

struct Scope {
  uint32_t t;
  explicit Scope(uint32_t tags) : t(tags) { enter(t); }
  ~Scope() { leave(t); }
  Scope(const Scope &) = delete;
  Scope &operator=(const Scope &) = delete;
};

/* Lowest free internal heap since boot, bytes. Replaces ESP.getMinFreeHeap(). */
uint32_t bootMin();
uint32_t failCount();

/* Print what is new since the last call (dips, failed allocations) and, every
 * `summaryMs`, one summary line. Call from the loop; never from the timer. */
void poll(Print &out, uint32_t summaryMs = 600000UL);
/* The whole picture: histogram, per-tag ratios, dips, failures. */
void report(Print &out);
/* Walk the heap now and print it by block size next to the snapshot the
 * timer took the last time the free heap fell under kSnapBytes. */
void walk(Print &out);
void resetStats();
/* Where "deep" starts for the per-tag table; resets the statistics. */
void setDeep(uint32_t bytes);

} // namespace heapwatch

#endif
