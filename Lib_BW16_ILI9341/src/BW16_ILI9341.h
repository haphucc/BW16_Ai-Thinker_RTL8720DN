#ifndef BW16_ILI9341_H
#define BW16_ILI9341_H

#include <Arduino.h>
#include <SPI.h>
#include <Print.h>

// Optional: define before including to set SPI clock (Hz), e.g. 10000000.
// Uses SPI.setDefaultFrequency(), which exists in the Ameba SPI library.
// #define BW16_TFT_SPI_HZ 10000000

#define TFT_W 240
#define TFT_H 320

#define TFT_BLACK   0x0000
#define TFT_BLUE    0x001F
#define TFT_RED     0xF800
#define TFT_GREEN   0x07E0
#define TFT_CYAN    0x07FF
#define TFT_MAGENTA 0xF81F
#define TFT_YELLOW  0xFFE0
#define TFT_WHITE   0xFFFF
#define TFT_ORANGE  0xFD20
#define TFT_GREY    0x7BEF

class BW16_ILI9341 : public Print {
public:
  // cs: informational only. Ameba hardware SPI drives SS (D9 / PA15) by itself,
  // so the TFT CS wire must go to D9.
  BW16_ILI9341(int8_t cs, int8_t dc, int8_t rst);

  void begin();
  void setRotation(uint8_t r);
  void invertDisplay(bool i);

  void fillScreen(uint16_t c);
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c);
  void drawPixel(int16_t x, int16_t y, uint16_t c);
  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c);
  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c);
  void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t c);
  void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t c);

  void setCursor(int16_t x, int16_t y) { _cx = x; _cy = y; }
  void setTextColor(uint16_t fg) { _fg = fg; _bg = fg; }          // transparent bg
  void setTextColor(uint16_t fg, uint16_t bg) { _fg = fg; _bg = bg; }
  void setTextSize(uint8_t s) { _ts = s ? s : 1; }
  void drawChar(int16_t x, int16_t y, unsigned char ch, uint16_t fg, uint16_t bg, uint8_t size);

  int16_t width() const { return _w; }
  int16_t height() const { return _h; }

  virtual size_t write(uint8_t ch);

  static uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  }

private:
  void cmd(uint8_t c);
  void data(uint8_t d);
  void setWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
  void pushColor(uint16_t c, uint32_t count);

  int8_t _cs, _dc, _rst;
  int16_t _w, _h, _cx, _cy;
  uint16_t _fg, _bg;
  uint8_t _ts, _rot;
};

#endif
