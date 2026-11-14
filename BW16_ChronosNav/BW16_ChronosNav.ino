/*
  BW16 (RTL8720DN) + ILI9341 TFT: chi duong Google Maps tu app Chronos (Android) + dong ho dong bo tu dien thoai.

  - Chua ket noi Chronos : man hinh cho, ngay gio hien dd : mm : yyyy / hh : mm : ss
  - Da ket noi           : nhan ngay gio tu dien thoai, man hinh cho chay dong ho
  - Dang dan duong       : dong tren cung hien ngay gio (chu nho), phan con lai la chi duong

  Day noi TFT: CS=D9(PA15)  SCK=D10  MOSI=D12  MISO=D11  DC=D5  RST=D4  VCC/LED=3V3
  Giao thuc Chronos: https://github.com/fbiego/chronos-esp32 (MIT)
*/
#include "BLEDevice.h"
#include <BW16_ILI9341.h>
#include "ChronosNav.h"
#include "ClockSync.h"
#include "ui_icons.h"

#define DEVICE_NAME            "Chronos BW16"
#define UART_SERVICE_UUID      "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// 1: mat ket noi van giu dong ho chay tiep (khuyen nghi). 0: mat ket noi quay lai dd : mm : yyyy
#define KEEP_CLOCK_AFTER_DISCONNECT 1
// Ky tu ngan cach ngay/thang/nam tren dong gio phia tren man hinh chi duong
#define BAR_DATE_SEP ":"

#define TFT_CS  9
#define TFT_DC  5
#define TFT_RST 4

#define COL_BG    TFT_BLACK
#define COL_MAIN  TFT_WHITE
#define COL_DIM   TFT_GREY
#define COL_ACC   TFT_GREEN
#define COL_INFO  TFT_CYAN

BW16_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

BLEService UartService(UART_SERVICE_UUID);
BLECharacteristic Rx(CHARACTERISTIC_UUID_RX);
BLECharacteristic Tx(CHARACTERISTIC_UUID_TX);
BLEAdvertData advdata;
BLEAdvertData scndata;

ChronosNavParser chronos;
PhoneClock phoneClock;

volatile bool notifyOn = false;
volatile bool infoPending = false;
volatile uint32_t infoDueAt = 0;

// ---------------------------------------------------------------- BLE callbacks (task BLE: khong ve man hinh o day)
void rxWriteCB(BLECharacteristic* chr, uint8_t connID) {
  (void)connID;
  uint16_t len = chr->getDataLen();
  if (len > 0) chronos.feed(chr->getDataBuff(), len);
}

void txCccdCB(BLECharacteristic* chr, uint8_t connID, uint16_t cccd) {
  (void)chr; (void)connID;
  if (cccd & GATT_CLIENT_CHAR_CONFIG_NOTIFY) {
    notifyOn = true;
    infoDueAt = millis() + 3000;
    infoPending = true;
  } else {
    notifyOn = false;
  }
}

static void sendPacket(const uint8_t* data, uint8_t len) {
  Tx.setData((uint8_t*)data, len);
  Tx.notify(0);
}

static void sendInfoAndBattery() {
  uint8_t info[20] = {0xAB, 0x00, 0x11, 0xFF, 0x92, 0xC0, 1, 91, 0x00, 0xFB, 0x1E, 0x40,
                      0xC0, 0x0E, 0x32, 0x28, 0x00, 0xE2, 0x81, 0x80};
  sendPacket(info, sizeof(info));
  delay(30);
  uint8_t bat[8] = {0xAB, 0x00, 0x05, 0xFF, 0x91, 0x80, 0x01, 100};
  sendPacket(bat, sizeof(bat));
}

// ---------------------------------------------------------------- Bo cuc man hinh (landscape 320x240)
enum UiMode { UI_IDLE, UI_NAV };
UiMode ui = UI_IDLE;

// --- man hinh chi duong
#define BAR_H      16                       // dong ngay gio phia tren
#define ICON_X     8
#define ICON_Y     20
#define ICON_SCALE 3                        // 48*3 = 144 px
#define PANEL_X    164
#define PANEL_W    156
#define TITLE_Y    20
#define DIR_Y      172
static const int16_t LABEL_Y[4] = {58, 84, 110, 136};

NavInfo shown;
bool shownValid = false;
uint32_t drawnIconCrc = 0xFFFFFFFE;         // 0xFFFFFFFE = chua ve icon
bool lastConn = false;
uint16_t lastVer = 0;
uint8_t iconBuf[CN_ICON_SIZE];

// cache chuoi dang hien de chi ve lai khi doi
char lastBar[32] = "";
char lastDate[20] = "";
char lastTime[20] = "";
int8_t lastPinFrame = -1;

static void putFit(int16_t x, int16_t y, int16_t w, uint16_t color, const char* s) {
  tft.fillRect(x, y, w, 16, COL_BG);
  size_t n = strlen(s);
  uint8_t size = (n * 12 <= (size_t)w) ? 2 : 1;
  int16_t maxc = w / (6 * size);
  tft.setTextSize(size);
  tft.setTextColor(color, COL_BG);
  tft.setCursor(x, y + (size == 1 ? 4 : 0));
  for (int16_t i = 0; i < maxc && s[i]; i++) tft.write(s[i]);
}

static void putLabel(int16_t y, const char* label, const char* value, uint16_t color) {
  tft.fillRect(PANEL_X, y, PANEL_W, 8, COL_BG);
  tft.setTextSize(1);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.setCursor(PANEL_X, y);
  tft.print(label);
  putFit(PANEL_X, y + 9, PANEL_W, color, value);
}

static void putTitle(const char* s) {
  size_t n = strlen(s);
  uint8_t size = (n <= 6) ? 4 : (n <= 8) ? 3 : 2;
  tft.fillRect(PANEL_X, TITLE_Y, PANEL_W, 34, COL_BG);
  tft.setTextSize(size);
  tft.setTextColor(COL_ACC, COL_BG);
  tft.setCursor(PANEL_X, TITLE_Y + (size == 4 ? 0 : size == 3 ? 6 : 10));
  int16_t maxc = PANEL_W / (6 * size);
  for (int16_t i = 0; i < maxc && s[i]; i++) tft.write(s[i]);
}

static void drawDirections(const char* s) {
  const int cols = 25, maxLines = 4;
  tft.fillRect(0, DIR_Y - 4, 320, 240 - (DIR_Y - 4), COL_BG);
  tft.setTextSize(2);
  tft.setTextColor(COL_MAIN, COL_BG);
  char line[cols + 1];
  int len = 0, lineNo = 0;
  const char* p = s;
  while (*p && lineNo < maxLines) {
    while (*p == ' ') p++;
    const char* w = p;
    while (*p && *p != ' ') p++;
    int wl = p - w;
    if (wl == 0) break;
    if (wl > cols) wl = cols;
    if (len + (len ? 1 : 0) + wl > cols) {
      line[len] = 0;
      tft.setCursor(4, DIR_Y + lineNo * 16);
      tft.print(line);
      lineNo++; len = 0;
      if (lineNo >= maxLines) return;
    }
    if (len) line[len++] = ' ';
    memcpy(line + len, w, wl); len += wl;
  }
  if (len && lineNo < maxLines) {
    line[len] = 0;
    tft.setCursor(4, DIR_Y + lineNo * 16);
    tft.print(line);
  }
}

// --- dong ngay gio nho phia tren man hinh chi duong
/*
static void drawTopBar(bool valid, const ClockDT& d, bool force) {
  char buf[32];
  if (valid) snprintf(buf, sizeof(buf), "%02u" BAR_DATE_SEP "%02u" BAR_DATE_SEP "%04u  %02u:%02u:%02u",
                      d.day, d.month, d.year, d.hour, d.minute, d.second);
  else       snprintf(buf, sizeof(buf), "dd" BAR_DATE_SEP "mm" BAR_DATE_SEP "yyyy  hh:mm:ss");
  if (!force && strcmp(buf, lastBar) == 0) return;
  if (force) {
    tft.fillRect(0, 0, 320, BAR_H, COL_BG);
    tft.fillRect(0, BAR_H - 1, 320, 1, COL_DIM);
    tft.drawBitmap1bpp(300, 0, ICON_BT, 14, 16, COL_INFO, COL_BG);     // dang ket noi Chronos
  }
  tft.setTextSize(1);
  tft.setTextColor(valid ? COL_MAIN : COL_DIM, COL_BG);
  tft.setCursor(6, 4);
  tft.print(buf);
  strcpy(lastBar, buf);
}
*/

static void drawTopBar(bool valid, const ClockDT& d, bool force) {
  char buf[32];
  if (valid) snprintf(buf, sizeof(buf), "%02u" BAR_DATE_SEP "%02u" BAR_DATE_SEP "%04u  %02u:%02u",
                      d.day, d.month, d.year, d.hour, d.minute);
  else       snprintf(buf, sizeof(buf), "dd" BAR_DATE_SEP "mm" BAR_DATE_SEP "yyyy  hh:mm:ss");
  if (!force && strcmp(buf, lastBar) == 0) return;
  if (force) {
    tft.fillRect(0, 0, 320, BAR_H, COL_BG);
    tft.drawBitmap1bpp(280, 0, ICON_BT, 13, 15, COL_INFO, COL_BG);     // dang ket noi Chronos
    tft.fillRect(0, BAR_H, 320, 1, COL_DIM);
  }
  tft.setTextSize(1);
  tft.setTextColor(valid ? COL_MAIN : COL_DIM, COL_BG);
  tft.setCursor(6, 4);
  tft.print(buf);
  strcpy(lastBar, buf);
}

static void clearIconArea() {
  tft.fillRect(ICON_X, ICON_Y, CN_ICON_W * ICON_SCALE, CN_ICON_H * ICON_SCALE, COL_BG);
  drawnIconCrc = 0xFFFFFFFE;
}

static void drawIconNow() {
  uint32_t crc;
  chronos.snapshotIcon(iconBuf, crc);
  tft.drawBitmap1bpp(ICON_X, ICON_Y, iconBuf, CN_ICON_W, CN_ICON_H, COL_MAIN, COL_BG, ICON_SCALE);
  drawnIconCrc = crc;
}

static void renderNav(const NavInfo& n, bool full) {
  if (full) tft.fillScreen(COL_BG);
  if (!shownValid || full || strcmp(n.title, shown.title)) putTitle(n.title);
  if (!shownValid || full || strcmp(n.distance, shown.distance)) putLabel(LABEL_Y[0], "CON LAI",    n.distance, COL_INFO);
  if (!shownValid || full || strcmp(n.duration, shown.duration)) putLabel(LABEL_Y[1], "THOI GIAN",  n.duration, COL_INFO);
  if (!shownValid || full || strcmp(n.eta, shown.eta))           putLabel(LABEL_Y[2], "DEN NOI LUC", n.eta,     COL_INFO);
  if (!shownValid || full || strcmp(n.speed, shown.speed))       putLabel(LABEL_Y[3], "TOC DO",     n.speed,    COL_INFO);
  if (!shownValid || full || strcmp(n.directions, shown.directions)) drawDirections(n.directions);
  shown = n;
  shownValid = true;
}

// --- man hinh cho (theo thiet ke Lopaka)
static void drawIdleStatusText(bool connected) {
  tft.fillRect(0, 176, 320, 64, COL_BG);
  tft.setTextSize(2);
  tft.setTextColor(connected ? COL_INFO : TFT_ORANGE, COL_BG);
  const char* a = connected ? "Da ket noi Chronos" : "Cho ket noi BLE...";
  tft.setCursor((320 - strlen(a) * 12) / 2, 187);
  tft.print(a);
  tft.setTextSize(1);
  tft.setTextColor(COL_MAIN, COL_BG);
  const char* b = connected ? "Mo Google Maps de bat dau chi duong" : "Ghep doi trong app Chronos: " DEVICE_NAME;
  tft.setCursor((320 - strlen(b) * 6) / 2, 215);
  tft.print(b);
}

/*
static void drawIdleClock(bool valid, const ClockDT& d) {
  char date[20], tm[20];
  if (valid) {
    snprintf(date, sizeof(date), "%02u : %02u : %04u", d.day, d.month, d.year);
    snprintf(tm, sizeof(tm), "%02u : %02u : %02u", d.hour, d.minute, d.second);
  } else {
    strcpy(date, "dd : mm : yyyy");
    strcpy(tm, "hh : mm : ss");
  }
  tft.setTextSize(2);
  if (strcmp(date, lastDate)) {
    tft.setTextColor(valid ? COL_INFO : COL_DIM, COL_BG);
    tft.setCursor(92, 109);
    tft.print(date);
    strcpy(lastDate, date);
  }
  if (strcmp(tm, lastTime)) {
    tft.setTextColor(valid ? COL_MAIN : COL_DIM, COL_BG);
    tft.setCursor(92, 137);
    tft.print(tm);
    strcpy(lastTime, tm);
  }
}
*/

static void drawIdleClock(bool valid, const ClockDT& d) {
  char date[20], tm[20];
  if (valid) {
    snprintf(date, sizeof(date), "%02u : %02u : %04u", d.day, d.month, d.year);
    snprintf(tm, sizeof(tm), "%02u : %02u", d.hour, d.minute);
  } else {
    strcpy(date, "dd : mm : yyyy");
    strcpy(tm, "hh : mm");
  }
  tft.setTextSize(2);
  if (strcmp(date, lastDate)) {
    tft.setTextColor(valid ? COL_INFO : COL_DIM, COL_BG);
    tft.setCursor(92, 109);
    tft.print(date);
    strcpy(lastDate, date);
  }
  if (strcmp(tm, lastTime)) {
    tft.setTextColor(valid ? COL_MAIN : COL_DIM, COL_BG);
    tft.setCursor(92, 137);
    tft.print(tm);
    strcpy(lastTime, tm);
  }
}

/*
static void drawPin(uint32_t now) {
  static const uint8_t bob[4] = {0, 1, 2, 1};
  int8_t f = (now / 140) % 4;
  if (f == lastPinFrame) return;
  lastPinFrame = f;
  uint8_t buf[32];
  memset(buf, 0, sizeof(buf));
  uint8_t off = bob[f];
  for (uint8_t r = 0; r + off < 16; r++) { buf[(r + off) * 2] = ICON_PIN[r * 2]; buf[(r + off) * 2 + 1] = ICON_PIN[r * 2 + 1]; }
  tft.drawBitmap1bpp(48, 117, buf, 16, 16, TFT_RED, COL_BG, 2);
}
*/

static void showIdle(bool connected) {
  ui = UI_IDLE;
  tft.fillScreen(COL_BG);
  // thanh trang thai (chi bieu tuong Bluetooth phan anh trang thai that, cac bieu tuong khac la trang tri)
  // tft.drawBitmap1bpp(7, 5, ICON_TOWER, 15, 16, COL_MAIN, COL_BG);
  tft.drawBitmap1bpp(62, 120, ICON_TOWER, 15, 16, COL_MAIN, COL_BG);
  // tft.drawBitmap1bpp(180, 6, ICON_PHONE, 14, 16, COL_MAIN, COL_BG);
  tft.drawBitmap1bpp(260, 6, ICON_BT, 14, 16, connected ? COL_INFO : COL_DIM, COL_BG);
  // tft.drawBitmap1bpp(218, 6, ICON_MUTE, 18, 16, COL_MAIN, COL_BG);
  // tft.drawBitmap1bpp(241, 6, ICON_SIGNAL, 15, 16, COL_MAIN, COL_BG);
  // tft.drawBitmap1bpp(261, 6, ICON_WIFI, 19, 16, COL_MAIN, COL_BG);
  tft.drawBitmap1bpp(290, 6, ICON_BATT, 24, 16, COL_MAIN, COL_BG);
  tft.setTextSize(2);
  tft.setTextColor(COL_MAIN, COL_BG);
  tft.setCursor(71, 37);  tft.print("Maps Navigation");
  tft.setCursor(89, 61);  tft.print(DEVICE_NAME);
  tft.drawRect(54, 100, 215, 63, COL_MAIN);
  drawIdleStatusText(connected);
  lastDate[0] = 0; lastTime[0] = 0; lastPinFrame = -1;
  drawnIconCrc = 0xFFFFFFFE;
  shownValid = false;
}

// ---------------------------------------------------------------- setup / loop
void setup() {
  Serial.begin(115200);
  tft.begin();
  tft.setRotation(1);
  showIdle(false);

  advdata.addFlags(GAP_ADTYPE_FLAGS_LIMITED | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED);
  advdata.addCompleteServices(BLEUUID(UART_SERVICE_UUID));
  scndata.addCompleteName(DEVICE_NAME);

  Rx.setWriteProperty(true);
  Rx.setWriteNRProperty(true);
  Rx.setWritePermissions(GATT_PERM_WRITE);
  Rx.setBufferLen(CHAR_VALUE_MAX_LEN);
  Rx.setWriteCallback(rxWriteCB);

  Tx.setReadProperty(true);
  Tx.setReadPermissions(GATT_PERM_READ);
  Tx.setNotifyProperty(true);
  Tx.setBufferLen(20);
  Tx.setCCCDCallback(txCccdCB);

  UartService.addCharacteristic(Rx);
  UartService.addCharacteristic(Tx);

  BLE.init();
  BLE.setDeviceName(DEVICE_NAME);
  BLE.configAdvert()->setAdvData(advdata);
  BLE.configAdvert()->setScanRspData(scndata);
  BLE.configServer(1);
  BLE.addService(UartService);
  BLE.beginPeripheral();

  Serial.println("BLE san sang, tim thiet bi: " DEVICE_NAME);
}

void loop() {
  uint32_t now = millis();
  bool conn = BLE.connected(0);

  // dong bo ngay gio tu dien thoai
  PhoneTime pt;
  if (chronos.takeTime(pt)) {
    phoneClock.set(pt.year, pt.month, pt.day, pt.hour, pt.minute, pt.second, now);
    Serial.print("Dong bo gio: ");
    Serial.print(pt.day); Serial.print("/"); Serial.print(pt.month); Serial.print("/"); Serial.print(pt.year);
    Serial.print(" "); Serial.print(pt.hour); Serial.print(":"); Serial.print(pt.minute); Serial.print(":"); Serial.println(pt.second);
  }

  if (conn != lastConn) {
    lastConn = conn;
    Serial.println(conn ? "Da ket noi" : "Mat ket noi");
    if (!conn) {
      notifyOn = false; infoPending = false; chronos.reset(); lastVer = 0;
#if !KEEP_CLOCK_AFTER_DISCONNECT
      phoneClock.invalidate();
#endif
    }
    showIdle(conn);
  }

  if (infoPending && notifyOn && (int32_t)(now - infoDueAt) >= 0) {
    infoPending = false;
    sendInfoAndBattery();
    Serial.println("Da gui info/pin cho app");
  }

  bool iconDone = chronos.takeIconDone();
  uint16_t ver = chronos.navVersion();
  bool navChanged = (ver != lastVer);

  if (navChanged || iconDone) {
    NavInfo n;
    chronos.snapshotNav(n);
    lastVer = ver;

    if (!n.active) {
      if (ui == UI_NAV) showIdle(conn);
    } else {
      bool full = (ui != UI_NAV);
      ClockDT d0; bool v0 = phoneClock.get(now, d0);
      renderNav(n, full);
      ui = UI_NAV;
      if (full) { drawnIconCrc = 0xFFFFFFFE; drawTopBar(v0, d0, true); }
      if (n.hasIcon) {
        uint8_t tmp[CN_ICON_SIZE]; uint32_t storedCrc;
        chronos.snapshotIcon(tmp, storedCrc);
        if (iconDone || storedCrc != drawnIconCrc) drawIconNow();
      } else if (drawnIconCrc != 0xFFFFFFFE) {
        clearIconArea();
      }
      Serial.print(n.title); Serial.print(" | "); Serial.println(n.directions);
    }
  }

  // dong ho + hoat anh
  ClockDT d;
  bool valid = phoneClock.get(now, d);
  if (ui == UI_IDLE) {
    drawIdleClock(valid, d);
    // drawPin(now);
  } else {
    drawTopBar(valid, d, false);
  }
  delay(10);
}
