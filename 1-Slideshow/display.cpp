// TFT output: backlight, status screens and JPEG drawing.

#include <SdFat.h>
#include <JPEGDEC.h>
#include "SPIFFS.h"
#include "qrcode.h"
#include "app.h"

#define BL_CHANNEL 0

TFT_eSPI tft = TFT_eSPI();
static JPEGDEC jpeg;
static uint8_t blPercent = 100;
static uint8_t blDuty = 0;

// ---------------------------------------------------------------- backlight
static void writeDuty(uint8_t duty) {
  blDuty = duty;
  ledcWrite(BL_CHANNEL, duty);
}

static uint8_t dutyFor(uint8_t percent) {
  return (uint8_t)((uint32_t)percent * 255 / 100);
}

void displaySetBrightness(uint8_t percent) {
  blPercent = percent;
  writeDuty(dutyFor(percent));
}

static void fadeTo(uint8_t target) {
  int from = blDuty;
  for (int step = 1; step <= 6; step++) {
    writeDuty(from + (target - from) * step / 6);
    delay(6);
  }
}

// ---------------------------------------------------------------- setup
void displayBegin() {
  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true);

#ifdef ENV_CYD2B
  // Gamma curve selection for the cyd2b panel
  tft.writecommand(0x26);
  tft.writedata(2);
  delay(120);
  tft.writecommand(0x26);
  tft.writedata(1);
#endif

  ledcSetup(BL_CHANNEL, 5000, 8);
  ledcAttachPin(TFT_BL, BL_CHANNEL);
  displaySetBrightness(settings.brightness);
}

// ---------------------------------------------------------------- JPEG
static SdBaseFile jpgFile;

static void *jpgOpen(const char *filename, int32_t *size) {
  if (!jpgFile.open(filename, O_RDONLY)) return nullptr;
  *size = jpgFile.fileSize();
  return &jpgFile;
}

static void jpgClose(void *handle) {
  if (jpgFile.isOpen()) jpgFile.close();
}

static int32_t jpgRead(JPEGFILE *handle, uint8_t *buffer, int32_t length) {
  int32_t n = jpgFile.read(buffer, length);
  if (n > 0) handle->iPos += n;
  return n;
}

static int32_t jpgSeek(JPEGFILE *handle, int32_t position) {
  handle->iPos = position;
  return jpgFile.seekSet(position);
}

static int jpegDraw(JPEGDRAW *pDraw) {
  tft.pushImage(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight, pDraw->pPixels);
  return 1;
}

// Decode an already opened image, scaled down (1/2, 1/4, 1/8) if it is bigger
// than the screen, and centered.
static bool decodeOpened() {
  int w = jpeg.getWidth();
  int h = jpeg.getHeight();
  int W = tft.width();
  int H = tft.height();
  int option = 0;
  int div = 1;
  if (w > W || h > H) {
    if (w / 2 <= W && h / 2 <= H) {
      option = JPEG_SCALE_HALF;
      div = 2;
    } else if (w / 4 <= W && h / 4 <= H) {
      option = JPEG_SCALE_QUARTER;
      div = 4;
    } else {
      option = JPEG_SCALE_EIGHTH;
      div = 8;
    }
  }
  int dw = w / div;
  int dh = h / div;
  if (dw < W || dh < H) tft.fillScreen(TFT_BLACK);
  tft.startWrite();
  int rc = jpeg.decode((W - dw) / 2, (H - dh) / 2, option);
  tft.endWrite();
  return rc == 1;
}

bool displayPhoto(const String &name) {
  String path = "/" + name;
  if (!storageLock(3000)) return false;
  bool ok = false;
  if (jpeg.open(path.c_str(), jpgOpen, jpgClose, jpgRead, jpgSeek, jpegDraw)) {
    if (blDuty > 0) fadeTo(0);
    ok = decodeOpened();
    jpeg.close();
    fadeTo(dutyFor(blPercent));
  } else {
    Serial.printf("Cannot decode %s (progressive or corrupt JPEG?)\n", path.c_str());
  }
  storageUnlock();
  return ok;
}

// ---------------------------------------------------------------- status screens
static void centered(const char *text, int y, int font, uint16_t color) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(text, tft.width() / 2, y, font);
}

void displayShowMessage(const char *title, const char *line1, const char *line2, uint16_t color) {
  tft.fillScreen(TFT_BLACK);
  centered(title, 70, 4, color);
  if (line1) centered(line1, 115, 2, TFT_WHITE);
  if (line2) centered(line2, 140, 2, TFT_WHITE);
}

void displayShowSplash() {
  tft.fillScreen(TFT_BLACK);
  bool drawn = false;
  if (SPIFFS.exists("/vanity.jpg")) {
    File f = SPIFFS.open("/vanity.jpg");
    size_t n = f ? f.size() : 0;
    uint8_t *buf = n ? (uint8_t *)malloc(n) : nullptr;
    if (buf && f.read(buf, n) == n && jpeg.openRAM(buf, n, jpegDraw)) {
      drawn = decodeOpened();
      jpeg.close();
    }
    free(buf);
    if (f) f.close();
  }
  if (!drawn) {
    centered("CYD PhotoFrame", 90, 4, TFT_CYAN);
    centered("v" FW_VERSION, 125, 2, TFT_WHITE);
  }
}

void displayShowApInstructions() {
  tft.fillScreen(TFT_BLACK);
  centered("WiFi setup", 12, 4, TFT_CYAN);
  centered("1. Connect your phone or PC", 60, 2, TFT_WHITE);
  centered("to the WiFi network", 82, 2, TFT_WHITE);
  centered("ESP32_AP", 108, 4, TFT_GREEN);
  centered("2. Open your browser at", 150, 2, TFT_WHITE);
  centered("192.168.4.1", 174, 4, TFT_GREEN);
  centered("Skips to the slideshow after 3 min", 218, 2, TFT_DARKGREY);
}

void displayShowInfo(const String &ip) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("PhotoFrame", 8, 8, 4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Open in a browser:", 8, 56, 2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString(ip.length() ? ip : String("(not connected)"), 8, 78, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("or", 8, 104, 2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString(HOSTNAME ".local", 8, 126, 2);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("Touch left/right: prev/next", 8, 190, 2);
  tft.drawString("Touch middle: this screen", 8, 210, 2);

  if (ip.length() == 0) return;
  String url = "http://" + ip;
  QRCode qr;
  uint8_t qrData[qrcode_getBufferSize(4)];
  qrcode_initText(&qr, qrData, 4, ECC_MEDIUM, url.c_str());
  const int block = 4;
  const int quiet = 4;
  int side = qr.size * block + 2 * quiet;
  int x0 = tft.width() - side - 4;
  int y0 = 56;
  tft.fillRect(x0, y0, side, side, TFT_WHITE);
  for (int y = 0; y < qr.size; y++)
    for (int x = 0; x < qr.size; x++)
      if (qrcode_getModule(&qr, x, y)) tft.fillRect(x0 + quiet + x * block, y0 + quiet + y * block, block, block, TFT_BLACK);
}

void displayShowOta(const char *line, int percent) {
  static int lastPercent = -1;
  if (percent < 0 || lastPercent < 0 || percent < lastPercent) {
    tft.fillScreen(TFT_BLACK);
    centered("Firmware update", 60, 4, TFT_CYAN);
    centered("Do not power off", 150, 2, TFT_WHITE);
    tft.drawRect(30, 100, 260, 24, TFT_WHITE);
  }
  if (percent >= 0) {
    tft.fillRect(32, 102, (256 * percent) / 100, 20, TFT_GREEN);
  }
  if (line) {
    tft.fillRect(0, 190, tft.width(), 30, TFT_BLACK);
    centered(line, 195, 2, TFT_YELLOW);
  }
  lastPercent = percent;
}
