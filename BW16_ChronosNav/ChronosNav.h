// Parser for the Chronos app BLE protocol (navigation part). No Arduino/BLE dependency.
// Protocol reverse-engineered by fbiego (https://github.com/fbiego/chronos-esp32, MIT license).
//
// Packet: 0xAB, lenHi, lenLo, 0xFE|0xFF, cmd, ...   (total size = len + 3)
// First BLE write carries 20 bytes; further writes carry [seq] + 19 payload bytes.
//   cmd 0xEF : navigation text  -> [5]=state(0x00 off, 0xFF disabled, 0x80 data)
//              state 0x80: [6]=hasIcon [7]=isNavigation [8..11]=iconCRC then 6 NUL-terminated strings:
//              title(distance to next turn), duration, distance, eta, directions, speed
//   cmd 0xEE : navigation icon  -> [6]=part(0..2) [7..10]=crc [11..106]=96 bytes of 48x48 1bpp bitmap
#ifndef CHRONOS_NAV_H
#define CHRONOS_NAV_H

#include <stdint.h>
#include <string.h>
#include "vn_ascii.h"

#define CN_ASM_SIZE   512
#define CN_ICON_SIZE  288      // 48 x 48 / 8
#define CN_ICON_W     48
#define CN_ICON_H     48

struct PhoneTime {
  uint16_t year;
  uint8_t month, day, hour, minute, second;
};

struct NavInfo {
  bool active;
  bool hasIcon;
  bool isNav;
  uint32_t iconCrc;
  char title[24];
  char duration[24];
  char distance[24];
  char eta[40];
  char directions[100];
  char speed[24];
};

class ChronosNavParser {
public:
  ChronosNavParser() { reset(); }

  void reset() {
    memset(&_nav, 0, sizeof(_nav));
    memset(_icon, 0, sizeof(_icon));
    _iconCrc = 0xFFFFFFFF;
    _expected = 0;
    _navSeq = 0; _iconSeq = 0;
    _navVersion = 0;
    _iconDone = false;
    _appInfoSeen = false;
    _timeSeq = 0; _timeNew = false;
    memset(&_time, 0, sizeof(_time));
  }

  // Call with every write received on the RX characteristic.
  void feed(const uint8_t* p, uint16_t len) {
    if (len == 0) return;
    uint16_t got = 0;
    if (len >= 4 && (p[0] == 0xAB || p[0] == 0xEA) && (p[3] == 0xFE || p[3] == 0xFF)) {
      uint16_t expected = (uint16_t)p[1] * 256 + p[2] + 3;
      if (expected > CN_ASM_SIZE) { _expected = 0; return; }
      uint16_t n = len > CN_ASM_SIZE ? CN_ASM_SIZE : len;
      memcpy(_asm, p, n);
      _expected = expected;
      got = len;
    } else {
      if (_expected == 0) return;
      uint16_t pos = 20 + (uint16_t)p[0] * 19;
      uint16_t payload = len - 1;
      if (pos + payload > CN_ASM_SIZE) { _expected = 0; return; }
      memcpy(_asm + pos, p + 1, payload);
      got = pos + payload;
    }
    if (_expected && got >= _expected) {
      uint16_t total = _expected;
      _expected = 0;
      dispatch(_asm, total);
    }
  }

  // Consistent copies for the main loop (the BLE task writes concurrently).
  uint16_t navVersion() const { return _navVersion; }
  void snapshotNav(NavInfo& out) {
    uint16_t a, b;
    do { a = _navSeq; memcpy(&out, &_nav, sizeof(NavInfo)); b = _navSeq; } while (a != b || (a & 1));
  }
  void snapshotIcon(uint8_t* out, uint32_t& crc) {
    uint16_t a, b;
    do { a = _iconSeq; memcpy(out, _icon, CN_ICON_SIZE); crc = _iconCrc; b = _iconSeq; } while (a != b || (a & 1));
  }
  bool takeIconDone() { bool v = _iconDone; _iconDone = false; return v; }
  bool takeAppInfo()  { bool v = _appInfoSeen; _appInfoSeen = false; return v; }
  // True once after each time-sync packet from the phone (local time of the phone).
  bool takeTime(PhoneTime& out) {
    if (!_timeNew) return false;
    _timeNew = false;
    uint16_t a, b;
    do { a = _timeSeq; out = _time; b = _timeSeq; } while (a != b || (a & 1));
    return true;
  }

private:
  void dispatch(const uint8_t* d, uint16_t len) {
    if (d[0] != 0xAB) return;
    switch (d[4]) {
      case 0xEF: if (d[3] == 0xFE) parseNav(d, len); break;
      case 0xEE: if (d[3] == 0xFE) parseIcon(d, len); break;
      case 0xCA: if (d[3] == 0xFE) _appInfoSeen = true; break;
      case 0x93: parseTime(d, len); break;       // time sync: year(2) month day hour min sec at [7..13]
      default: break;
    }
  }

  void parseTime(const uint8_t* d, uint16_t len) {
    if (len < 14) return;
    uint16_t y = (uint16_t)d[7] * 256 + d[8];
    if (y < 2000 || y > 2099 || d[9] < 1 || d[9] > 12 || d[10] < 1 || d[10] > 31 ||
        d[11] > 23 || d[12] > 59 || d[13] > 59) return;
    _timeSeq++;
    _time.year = y; _time.month = d[9]; _time.day = d[10];
    _time.hour = d[11]; _time.minute = d[12]; _time.second = d[13];
    _timeSeq++;
    _timeNew = true;
  }

  static const uint8_t* readStr(const uint8_t* p, const uint8_t* end, char* out, size_t cap) {
    const uint8_t* s = p;
    while (p < end && *p != 0) p++;
    utf8ToAscii(s, (size_t)(p - s), out, cap);
    if (p < end) p++;     // skip NUL
    return p;
  }

  void parseNav(const uint8_t* d, uint16_t len) {
    if (len < 6) return;
    _navSeq++;                                  // odd = writing
    uint8_t st = d[5];
    if (st == 0x00) {
      memset(&_nav, 0, sizeof(_nav));
      _nav.iconCrc = 0xFFFFFFFF;
    } else if (st == 0xFF) {
      memset(&_nav, 0, sizeof(_nav));
      _nav.active = true;
      _nav.iconCrc = 0xFFFFFFFF;
      strcpy(_nav.title, "Chronos");
      strcpy(_nav.directions, "Navigation disabled in Chronos app settings");
    } else if (st == 0x80 && len >= 12) {
      _nav.active = true;
      _nav.hasIcon = d[6] == 1;
      _nav.isNav = d[7] == 1;
      _nav.iconCrc = ((uint32_t)d[8] << 24) | ((uint32_t)d[9] << 16) | ((uint32_t)d[10] << 8) | d[11];
      const uint8_t* p = d + 12;
      const uint8_t* end = d + len;
      p = readStr(p, end, _nav.title, sizeof(_nav.title));
      p = readStr(p, end, _nav.duration, sizeof(_nav.duration));
      p = readStr(p, end, _nav.distance, sizeof(_nav.distance));
      p = readStr(p, end, _nav.eta, sizeof(_nav.eta));
      p = readStr(p, end, _nav.directions, sizeof(_nav.directions));
      p = readStr(p, end, _nav.speed, sizeof(_nav.speed));
    }
    _navVersion++;
    _navSeq++;                                  // even = stable
  }

  void parseIcon(const uint8_t* d, uint16_t len) {
    if (len < 11 + 96) return;
    uint8_t part = d[6];
    if (part > 2) return;
    _iconSeq++;
    memcpy(_icon + part * 96, d + 11, 96);
    _iconCrc = ((uint32_t)d[7] << 24) | ((uint32_t)d[8] << 16) | ((uint32_t)d[9] << 8) | d[10];
    _iconSeq++;
    if (part == 2) _iconDone = true;
  }

  uint8_t _asm[CN_ASM_SIZE];
  uint16_t _expected;
  NavInfo _nav;
  uint8_t _icon[CN_ICON_SIZE];
  uint32_t _iconCrc;
  volatile uint16_t _navSeq, _iconSeq, _navVersion;
  volatile bool _iconDone, _appInfoSeen, _timeNew;
  volatile uint16_t _timeSeq;
  PhoneTime _time;
};

#endif
