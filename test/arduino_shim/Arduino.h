/*
 * Minimal Arduino shim for the HOST test build (pio test -e native).
 *
 * Only the sliver of the API that the pure-logic headers under test actually
 * touch: String's read/append surface for TextUtil.h, and the integer types.
 * Anything that needs real hardware (SD, SPI, WiFi) is deliberately absent —
 * if a test starts needing it, the code under test is not pure logic and the
 * test belongs on the device instead.
 */
#ifndef NOCT_TEST_ARDUINO_SHIM_H
#define NOCT_TEST_ARDUINO_SHIM_H

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

class String {
public:
  String() {}
  String(const char *s) : s_(s ? s : "") {}
  String(const std::string &s) : s_(s) {}

  int length() const { return (int)s_.size(); }
  const char *c_str() const { return s_.c_str(); }
  void reserve(size_t n) { s_.reserve(n); }

  char operator[](int i) const { return s_[(size_t)i]; }

  String &operator+=(char c) {
    s_.push_back(c);
    return *this;
  }
  String &operator+=(const char *c) {
    s_ += (c ? c : "");
    return *this;
  }
  String &operator+=(const String &o) {
    s_ += o.s_;
    return *this;
  }

  bool operator==(const char *o) const { return s_ == (o ? o : ""); }
  bool operator!=(const char *o) const { return !(*this == o); }

private:
  std::string s_;
};

#endif
