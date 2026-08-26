/*
 * Nocturne C6 — Wake-on-LAN: six bytes of 0xFF and the MAC sixteen times.
 *
 * The whole protocol is one UDP broadcast. There is no acknowledgement and
 * there cannot be one: the machine being woken is, by definition, not
 * listening. So this returns whether the PACKET WAS SENT, never whether
 * anything woke up — and every caller has to keep those apart, because
 * "sent" is the only fact available and reporting it as "woke the PC" is the
 * kind of lie that gets debugged for an hour at the wall socket.
 *
 * Broadcast rather than unicast on purpose. A sleeping machine drops out of
 * every ARP table it was in, so a unicast packet has nowhere to go; the
 * subnet broadcast reaches the NIC, which is awake even when the rest of the
 * board is not.
 *
 * Port 9 (discard) by convention. Nothing listens there — the NIC matches the
 * magic pattern in hardware, anywhere in any frame, and never involves the
 * operating system at all. That is why this works on a machine with no
 * software running.
 */
#ifndef NOCT_WOL_CLIENT_H
#define NOCT_WOL_CLIENT_H

#include <Arduino.h>
#include <WiFiUdp.h>

namespace wol {

/* "D8-5E-D3-54-9A-EF", "d8:5e:d3:54:9a:ef" and "D85ED3549AEF" all parse.
 * Returns false unless exactly six bytes were found: a MAC one nibble short
 * is not a MAC, and half-parsing it would send a packet that wakes nothing
 * while reporting success. */
inline bool parseMac(const char *s, uint8_t out[6]) {
  if (!s) return false;
  int n = 0, hi = -1;
  for (const char *p = s; *p && n < 6; p++) {
    int v;
    if (*p >= '0' && *p <= '9') v = *p - '0';
    else if (*p >= 'a' && *p <= 'f') v = *p - 'a' + 10;
    else if (*p >= 'A' && *p <= 'F') v = *p - 'A' + 10;
    else continue; /* separators of any flavour */
    if (hi < 0) {
      hi = v;
    } else {
      out[n++] = (uint8_t)((hi << 4) | v);
      hi = -1;
    }
  }
  return n == 6 && hi < 0;
}

/* Send the magic packet to the subnet broadcast address.
 *
 * `bcast` should be the local broadcast (e.g. 10.77.77.255) rather than
 * 255.255.255.255: the limited broadcast is dropped by more stacks and
 * switches than the directed one, and the board knows its own subnet. */
inline bool send(const uint8_t mac[6], IPAddress bcast, uint16_t port = 9) {
  uint8_t frame[102];
  for (int i = 0; i < 6; i++) frame[i] = 0xFF;
  for (int r = 0; r < 16; r++)
    for (int i = 0; i < 6; i++) frame[6 + r * 6 + i] = mac[i];

  /* Print the head of the frame. There is no acknowledgement to check and
   * the firewall on a RUNNING machine blocks inbound UDP 9 — which says
   * nothing about Wake-on-LAN, because the NIC matches the magic pattern in
   * hardware while the operating system is asleep and never involves it. So
   * the only thing observable from here is the bytes themselves, and they are
   * worth being able to see. */
  Serial.printf("[WOL] frame %02X%02X%02X%02X%02X%02X | %02X:%02X:%02X:%02X:"
                "%02X:%02X x16 | %u bytes\n",
                frame[0], frame[1], frame[2], frame[3], frame[4], frame[5],
                frame[6], frame[7], frame[8], frame[9], frame[10], frame[11],
                (unsigned)sizeof(frame));

  WiFiUDP udp;
  if (!udp.begin(0)) return false; /* any local port */
  bool ok = udp.beginPacket(bcast, port) == 1;
  if (ok) ok = udp.write(frame, sizeof(frame)) == sizeof(frame);
  if (ok) ok = udp.endPacket() == 1;
  udp.stop();
  return ok;
}

/* The subnet broadcast for an address/mask, so the caller does not have to
 * hardcode 10.77.77.255 and quietly break when the network moves. */
inline IPAddress broadcastFor(IPAddress ip, IPAddress mask) {
  IPAddress b;
  for (int i = 0; i < 4; i++) b[i] = (ip[i] & mask[i]) | (uint8_t)~mask[i];
  return b;
}

} // namespace wol

#endif
