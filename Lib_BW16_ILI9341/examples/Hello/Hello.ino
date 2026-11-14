#include <BW16_ILI9341.h>

// Day noi (BW16 -> TFT):
//   D9  (PA15) -> CS   (do SPI phan cung tu dieu khien)
//   D10 (PA14) -> SCK
//   D12 (PA12) -> SDI/MOSI
//   D11 (PA13) -> SDO/MISO (co the bo)
//   D5  (PB2)  -> DC
//   D4  (PB1)  -> RESET
//   3V3 -> VCC va LED, GND -> GND
#define TFT_CS  9
#define TFT_DC  5
#define TFT_RST 4

BW16_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

void setup() {
  Serial.begin(115200);
  Serial.println("TFT start");
  tft.begin();
  tft.setRotation(1);

  tft.fillScreen(TFT_RED);   delay(400);
  tft.fillScreen(TFT_GREEN); delay(400);
  tft.fillScreen(TFT_BLUE);  delay(400);
  tft.fillScreen(TFT_BLACK);

  tft.setCursor(10, 10);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(4);
  tft.println("Kupalka");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.println("BW16 + ILI9341");
  tft.drawRect(0, 0, tft.width(), tft.height(), TFT_ORANGE);
  tft.fillCircle(160, 160, 30, TFT_CYAN);
  Serial.println("TFT done");
}

void loop() {}
