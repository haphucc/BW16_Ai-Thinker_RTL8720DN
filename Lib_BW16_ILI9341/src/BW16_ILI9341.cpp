#include "BW16_ILI9341.h"
#include "bw16_font5x7.h"

#define ILI_SWRESET 0x01
#define ILI_SLPOUT  0x11
#define ILI_INVOFF  0x20
#define ILI_INVON   0x21
#define ILI_DISPON  0x29
#define ILI_CASET   0x2A
#define ILI_PASET   0x2B
#define ILI_RAMWR   0x2C
#define ILI_MADCTL  0x36
#define ILI_PIXFMT  0x3A

#define MADCTL_MY  0x80
#define MADCTL_MX  0x40
#define MADCTL_MV  0x20
#define MADCTL_BGR 0x08

static inline int16_t _abs16(int16_t v) { return v < 0 ? -v : v; }
static inline void _swap16(int16_t &a, int16_t &b) { int16_t t = a; a = b; b = t; }

BW16_ILI9341::BW16_ILI9341(int8_t cs, int8_t dc, int8_t rst)
  : _cs(cs), _dc(dc), _rst(rst), _w(TFT_W), _h(TFT_H), _cx(0), _cy(0),
    _fg(TFT_WHITE), _bg(TFT_WHITE), _ts(1), _rot(0) {}

void BW16_ILI9341::cmd(uint8_t c) {
  digitalWrite(_dc, LOW);
  SPI.transfer(c);
}

void BW16_ILI9341::data(uint8_t d) {
  digitalWrite(_dc, HIGH);
  SPI.transfer(d);
}

void BW16_ILI9341::begin() {
  pinMode(_dc, OUTPUT);
  digitalWrite(_dc, HIGH);
  if (_rst >= 0) {
    pinMode(_rst, OUTPUT);
    digitalWrite(_rst, HIGH);
  }

  SPI.begin();
#ifdef BW16_TFT_SPI_HZ
  SPI.setDefaultFrequency(BW16_TFT_SPI_HZ);
#endif

  if (_rst >= 0) {
    digitalWrite(_rst, HIGH); delay(10);
    digitalWrite(_rst, LOW);  delay(20);
    digitalWrite(_rst, HIGH); delay(150);
  } else {
    cmd(ILI_SWRESET); delay(150);
  }

  cmd(0xEF); data(0x03); data(0x80); data(0x02);
  cmd(0xCF); data(0x00); data(0xC1); data(0x30);
  cmd(0xED); data(0x64); data(0x03); data(0x12); data(0x81);
  cmd(0xE8); data(0x85); data(0x00); data(0x78);
  cmd(0xCB); data(0x39); data(0x2C); data(0x00); data(0x34); data(0x02);
  cmd(0xF7); data(0x20);
  cmd(0xEA); data(0x00); data(0x00);
  cmd(0xC0); data(0x23);                    // power ctrl 1
  cmd(0xC1); data(0x10);                    // power ctrl 2
  cmd(0xC5); data(0x3E); data(0x28);        // VCOM 1
  cmd(0xC7); data(0x86);                    // VCOM 2
  cmd(ILI_MADCTL); data(0x48);
  cmd(ILI_PIXFMT); data(0x55);              // 16 bit
  cmd(0xB1); data(0x00); data(0x18);        // frame rate
  cmd(0xB6); data(0x08); data(0x82); data(0x27);
  cmd(0xF2); data(0x00);                    // 3-gamma off
  cmd(0x26); data(0x01);                    // gamma curve
  cmd(0xE0);
  { const uint8_t g[] = {0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,0x37,0x07,0x10,0x03,0x0E,0x09,0x00};
    for (uint8_t i = 0; i < 15; i++) data(g[i]); }
  cmd(0xE1);
  { const uint8_t g[] = {0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F};
    for (uint8_t i = 0; i < 15; i++) data(g[i]); }
  cmd(ILI_SLPOUT); delay(120);
  cmd(ILI_DISPON); delay(20);

  setRotation(0);
}

void BW16_ILI9341::setRotation(uint8_t r) {
  _rot = r & 3;
  cmd(ILI_MADCTL);
  switch (_rot) {
    case 0: data(MADCTL_MX | MADCTL_BGR);                         _w = TFT_W; _h = TFT_H; break;
    case 1: data(MADCTL_MV | MADCTL_BGR);                         _w = TFT_H; _h = TFT_W; break;
    case 2: data(MADCTL_MY | MADCTL_BGR);                         _w = TFT_W; _h = TFT_H; break;
    case 3: data(MADCTL_MX | MADCTL_MY | MADCTL_MV | MADCTL_BGR); _w = TFT_H; _h = TFT_W; break;
  }
}

void BW16_ILI9341::invertDisplay(bool i) { cmd(i ? ILI_INVON : ILI_INVOFF); }

void BW16_ILI9341::setWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  cmd(ILI_CASET);
  data(x0 >> 8); data(x0 & 0xFF); data(x1 >> 8); data(x1 & 0xFF);
  cmd(ILI_PASET);
  data(y0 >> 8); data(y0 & 0xFF); data(y1 >> 8); data(y1 & 0xFF);
  cmd(ILI_RAMWR);
}

void BW16_ILI9341::pushColor(uint16_t c, uint32_t count) {
  uint8_t hi = c >> 8, lo = c & 0xFF;
  digitalWrite(_dc, HIGH);
  while (count--) {
    SPI.transfer(hi);
    SPI.transfer(lo);
  }
}

void BW16_ILI9341::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
  if (w <= 0 || h <= 0 || x >= _w || y >= _h) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > _w) w = _w - x;
  if (y + h > _h) h = _h - y;
  if (w <= 0 || h <= 0) return;
  setWindow(x, y, x + w - 1, y + h - 1);
  pushColor(c, (uint32_t)w * h);
}

void BW16_ILI9341::fillScreen(uint16_t c) { fillRect(0, 0, _w, _h, c); }

void BW16_ILI9341::drawPixel(int16_t x, int16_t y, uint16_t c) {
  if (x < 0 || y < 0 || x >= _w || y >= _h) return;
  setWindow(x, y, x, y);
  pushColor(c, 1);
}

void BW16_ILI9341::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c) {
  if (x0 == x1) { if (y0 > y1) _swap16(y0, y1); fillRect(x0, y0, 1, y1 - y0 + 1, c); return; }
  if (y0 == y1) { if (x0 > x1) _swap16(x0, x1); fillRect(x0, y0, x1 - x0 + 1, 1, c); return; }
  bool steep = _abs16(y1 - y0) > _abs16(x1 - x0);
  if (steep) { _swap16(x0, y0); _swap16(x1, y1); }
  if (x0 > x1) { _swap16(x0, x1); _swap16(y0, y1); }
  int16_t dx = x1 - x0, dy = _abs16(y1 - y0), err = dx / 2;
  int16_t ystep = (y0 < y1) ? 1 : -1;
  for (; x0 <= x1; x0++) {
    if (steep) drawPixel(y0, x0, c); else drawPixel(x0, y0, c);
    err -= dy;
    if (err < 0) { y0 += ystep; err += dx; }
  }
}

void BW16_ILI9341::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
  fillRect(x, y, w, 1, c);
  fillRect(x, y + h - 1, w, 1, c);
  fillRect(x, y, 1, h, c);
  fillRect(x + w - 1, y, 1, h, c);
}

void BW16_ILI9341::drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t c) {
  int16_t f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r;
  drawPixel(x0, y0 + r, c); drawPixel(x0, y0 - r, c);
  drawPixel(x0 + r, y0, c); drawPixel(x0 - r, y0, c);
  while (x < y) {
    if (f >= 0) { y--; ddy += 2; f += ddy; }
    x++; ddx += 2; f += ddx;
    drawPixel(x0 + x, y0 + y, c); drawPixel(x0 - x, y0 + y, c);
    drawPixel(x0 + x, y0 - y, c); drawPixel(x0 - x, y0 - y, c);
    drawPixel(x0 + y, y0 + x, c); drawPixel(x0 - y, y0 + x, c);
    drawPixel(x0 + y, y0 - x, c); drawPixel(x0 - y, y0 - x, c);
  }
}

void BW16_ILI9341::fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t c) {
  for (int16_t dy = -r; dy <= r; dy++) {
    int16_t dx = 0;
    while ((int32_t)dx * dx + (int32_t)dy * dy <= (int32_t)r * r) dx++;
    dx--;
    if (dx >= 0) fillRect(x0 - dx, y0 + dy, 2 * dx + 1, 1, c);
  }
}

void BW16_ILI9341::drawChar(int16_t x, int16_t y, unsigned char ch, uint16_t fg, uint16_t bg, uint8_t size) {
  if (x >= _w || y >= _h || x + 6 * size <= 0 || y + 8 * size <= 0) return;
  for (int8_t i = 0; i < 6; i++) {
    uint8_t line = (i < 5) ? bw16_font5x7[ch * 5 + i] : 0;
    for (int8_t j = 0; j < 8; j++, line >>= 1) {
      if (line & 1) {
        if (size == 1) drawPixel(x + i, y + j, fg);
        else fillRect(x + i * size, y + j * size, size, size, fg);
      } else if (bg != fg) {
        if (size == 1) drawPixel(x + i, y + j, bg);
        else fillRect(x + i * size, y + j * size, size, size, bg);
      }
    }
  }
}

size_t BW16_ILI9341::write(uint8_t ch) {
  if (ch == '\n') { _cx = 0; _cy += 8 * _ts; }
  else if (ch == '\r') { }
  else {
    if (_cx + 6 * _ts > _w) { _cx = 0; _cy += 8 * _ts; }
    drawChar(_cx, _cy, ch, _fg, _bg, _ts);
    _cx += 6 * _ts;
  }
  return 1;
}
