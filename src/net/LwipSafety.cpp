/*
 * Nocturne C6 — a linker-wrap fix for an upstream thread-safety bug.
 *
 * arduino-esp32's NetworkManager::hostByName() clears the DNS cache with a
 * RAW lwIP core call — `dns_clear_cache()` — straight from whatever task asked
 * for the lookup, holding no TCP/IP core lock. Everything else in that
 * function goes through the safe socket API; this one line does not.
 *
 * Why it matters here: clearing the cache aborts in-flight DNS queries and
 * runs their completion callbacks *in the calling task*. If SNTP has a query
 * pending (it does on every boot, for pool.ntp.org), its callback re-issues a
 * request, DNS allocates a fresh UDP port, and `udp_new_ip_type()` hits
 * lwIP's thread-safety assert:
 *
 *   assert failed: udp_new_ip_type udp.c:1278
 *   (Required to lock TCPIP core functionality!)
 *
 * The race window is milliseconds wide, which is why this firmware ran for
 * days without seeing it — until the Zigbee coordinator arrived. Network
 * formation occupies the shared radio for ~2 s right after WiFi associates,
 * SNTP's DNS query stays pending that whole time, and the lite fallback's
 * first fetch lands inside the window on every single boot. Six identical
 * panics in the boot journal, one identical backtrace.
 *
 * The fix: intercept `dns_clear_cache` at link time (-Wl,--wrap) and take the
 * TCP/IP core lock around the real call. CONFIG_LWIP_TCPIP_CORE_LOCKING=y in
 * this SDK, so taking the lock from an app task is the sanctioned pattern —
 * the cascade then runs in exactly the environment it expects, and the assert
 * (which checks the lock HOLDER, not the thread) passes. The holder check
 * below keeps the wrap re-entrant-safe should lwIP ever call this internally
 * from the tcpip thread (today nothing in the shipped tree does).
 */
#include "lwip/opt.h"
#include "lwip/tcpip.h"

#if LWIP_TCPIP_CORE_LOCKING

extern "C" void __real_dns_clear_cache(void);

extern "C" void __wrap_dns_clear_cache(void) {
  if (sys_thread_tcpip(LWIP_CORE_LOCK_QUERY_HOLDER)) {
    /* already inside the tcpip context — locking again would deadlock */
    __real_dns_clear_cache();
    return;
  }
  LOCK_TCPIP_CORE();
  __real_dns_clear_cache();
  UNLOCK_TCPIP_CORE();
}

#else
#error "This fix assumes LWIP_TCPIP_CORE_LOCKING; the SDK config changed"
#endif
