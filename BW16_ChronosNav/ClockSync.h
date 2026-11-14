// Software clock: set from the phone's time, then advanced with millis(). Use from loop() only.
#ifndef CLOCK_SYNC_H
#define CLOCK_SYNC_H
#include <stdint.h>

struct ClockDT { uint16_t year; uint8_t month, day, hour, minute, second; };

class PhoneClock {
public:
  PhoneClock() : _valid(false), _baseEpoch(0), _baseMs(0) {}

  bool valid() const { return _valid; }
  void invalidate() { _valid = false; }

  void set(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s, uint32_t nowMs) {
    _baseEpoch = daysFromCivil(y, mo, d) * 86400LL + h * 3600 + mi * 60 + s;
    _baseMs = nowMs;
    _valid = true;
  }

  // Returns false (and leaves out untouched) until the first sync.
  bool get(uint32_t nowMs, ClockDT& out) {
    if (!_valid) return false;
    uint32_t sec = (uint32_t)(nowMs - _baseMs) / 1000;
    _baseEpoch += sec;                     // fold so millis() rollover never matters
    _baseMs += sec * 1000;
    int64_t e = _baseEpoch;
    int64_t days = e / 86400;
    uint32_t rem = (uint32_t)(e - days * 86400);
    civilFromDays(days, out);
    out.hour = rem / 3600;
    out.minute = (rem % 3600) / 60;
    out.second = rem % 60;
    return true;
  }

private:
  // Howard Hinnant's civil-date algorithms
  static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int)doe - 719468;
  }
  static void civilFromDays(int64_t z, ClockDT& o) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = (int)yoe + (int)era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;
    o.year = y + (m <= 2);
    o.month = m;
    o.day = d;
  }

  bool _valid;
  int64_t _baseEpoch;
  uint32_t _baseMs;
};
#endif
