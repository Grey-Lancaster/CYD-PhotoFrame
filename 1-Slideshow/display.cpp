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
  displayApplyColorOrder();
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

  // Panel ID, handy when picking the right build: ILI9341 reports 0x9341, ST7789 0x8552.
  // Read after the display is fully set up, then restore the rotation/colour order the
  // read-back sequence may have touched.
  Serial.printf("Display ID: RDDID=0x%06X ID4=0x%06X\n", (unsigned)tft.readcommand32(0x04) >> 8, (unsigned)tft.readcommand32(0xD3) & 0xFFFFFF);
  tft.setRotation(3);
  displayApplyColorOrder();
}

// Rotation 3 as TFT_eSPI sets it, with the panel's red/blue order optionally flipped.
// Some panels of the same controller type are wired the other way round, so this is a
// setting instead of a build option.
void displayApplyColorOrder() {
#if defined(ST7789_DRIVER)
  const uint8_t base = TFT_MAD_MV | TFT_MAD_MY;
#else  // ILI9341 family
  const uint8_t base = TFT_MAD_MX | TFT_MAD_MY | TFT_MAD_MV;
#endif
  const uint8_t order = settings.swapRB ? (TFT_MAD_COLOR_ORDER ^ TFT_MAD_BGR) : TFT_MAD_COLOR_ORDER;
  tft.writecommand(TFT_MADCTL);
  tft.writedata(base | order);
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

// Fractional resize state. JPEGDEC can only scale by 1/2, 1/4 and 1/8, so the
// decoder runs at the largest of those that is still at least as big as the
// target and jpegDraw() resamples the remaining factor (0.5 .. 1.0).
static struct {
  bool active = false;
  int ox = 0, oy = 0;          // top-left of the picture on screen
  int dstW = 0, dstH = 0;      // picture size on screen
  int dw = 0, dh = 0;          // decoded (power-of-2 scaled) size
  uint32_t inv = 65536;        // 1/f in 16.16 fixed point
  float f = 1.0f;              // scale factor decoded -> screen
  bool smooth = false;         // average 2x2 source pixels
} rs;

static uint16_t rsBuf[4096];

static inline uint16_t avg4(uint16_t a, uint16_t b, uint16_t c, uint16_t d) {
  uint32_t r = ((a >> 11) & 31) + ((b >> 11) & 31) + ((c >> 11) & 31) + ((d >> 11) & 31);
  uint32_t g = ((a >> 5) & 63) + ((b >> 5) & 63) + ((c >> 5) & 63) + ((d >> 5) & 63);
  uint32_t bl = (a & 31) + (b & 31) + (c & 31) + (d & 31);
  return ((r >> 2) << 11) | ((g >> 2) << 5) | (bl >> 2);
}

static int jpegDraw(JPEGDRAW *pDraw) {
  if (!rs.active) {
    tft.pushImage(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight, pDraw->pPixels);
    return 1;
  }
  const int iw = pDraw->iWidth, ih = pDraw->iHeight;
  // destination rectangle covered by this block (ceil keeps neighbouring blocks seamless)
  int x0 = (int)ceilf(pDraw->x * rs.f), x1 = (int)ceilf((pDraw->x + iw) * rs.f);
  int y0 = (int)ceilf(pDraw->y * rs.f), y1 = (int)ceilf((pDraw->y + ih) * rs.f);
  if (x1 > rs.dstW) x1 = rs.dstW;
  if (y1 > rs.dstH) y1 = rs.dstH;
  int w = x1 - x0, h = y1 - y0;
  if (w <= 0 || h <= 0) return 1;
  if (w * h > (int)(sizeof(rsBuf) / sizeof(rsBuf[0]))) return 1;
  const uint16_t *src = pDraw->pPixels;
  uint16_t *out = rsBuf;
  for (int dy = y0; dy < y1; dy++) {
    int sy = (int)(((uint64_t)dy * rs.inv) >> 16) - pDraw->y;
    sy = constrain(sy, 0, ih - 1);
    int sy2 = min(sy + 1, ih - 1);
    for (int dx = x0; dx < x1; dx++) {
      int sx = (int)(((uint64_t)dx * rs.inv) >> 16) - pDraw->x;
      sx = constrain(sx, 0, iw - 1);
      if (rs.smooth) {
        int sx2 = min(sx + 1, iw - 1);
        *out++ = avg4(src[sy * iw + sx], src[sy * iw + sx2], src[sy2 * iw + sx], src[sy2 * iw + sx2]);
      } else {
        *out++ = src[sy * iw + sx];
      }
    }
  }
  tft.pushImage(rs.ox + x0, rs.oy + y0, w, h, rsBuf);
  return 1;
}

// Decode an already opened image and show it as large as fits the screen,
// centered. Images that are smaller than the screen are not enlarged.
static bool decodeOpened() {
  int w = jpeg.getWidth();
  int h = jpeg.getHeight();
  int W = tft.width();
  int H = tft.height();
  rs.active = false;
  int option = 0;
  int div = 1;
  if (w > W || h > H) {
    float f0 = min((float)W / w, (float)H / h);  // overall factor needed
    while (div < 8 && 1.0f / (div * 2) >= f0) div *= 2;
    option = div == 2 ? JPEG_SCALE_HALF : div == 4 ? JPEG_SCALE_QUARTER : div == 8 ? JPEG_SCALE_EIGHTH : 0;
    rs.dw = w / div;
    rs.dh = h / div;
    rs.f = f0 * div;
    rs.dstW = min(W, (int)(w * f0 + 0.5f));
    rs.dstH = min(H, (int)(h * f0 + 0.5f));
    if (rs.f < 0.999f) {
      rs.active = true;
      rs.inv = (uint32_t)(65536.0f / rs.f);
      rs.smooth = rs.f < 0.9f;
    } else {
      rs.dstW = min(W, rs.dw);
      rs.dstH = min(H, rs.dh);
    }
  } else {
    rs.dstW = w;
    rs.dstH = h;
  }
  rs.ox = (W - rs.dstW) / 2;
  rs.oy = (H - rs.dstH) / 2;
  if (rs.dstW < W || rs.dstH < H) tft.fillScreen(TFT_BLACK);
  tft.startWrite();
  // unscaled path draws at absolute coordinates, so pass the offset to the decoder
  int rc = jpeg.decode(rs.active ? 0 : rs.ox, rs.active ? 0 : rs.oy, option);
  tft.endWrite();
  rs.active = false;
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
