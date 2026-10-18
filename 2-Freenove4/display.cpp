// TFT output: backlight, status screens and JPEG drawing.
//
// Photos are decoded whole into a PSRAM buffer and scaled to fit the screen
// (enlarging or shrinking) with bilinear sampling, so small photos fill the
// 480x320 panel as far as their shape allows.

#include <SD_MMC.h>
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
  displayApplyRotation();
  displayApplyColorOrder();
  displayApplyInversion();
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true);

  ledcSetup(BL_CHANNEL, 5000, 8);
  ledcAttachPin(TFT_BL, BL_CHANNEL);
  displaySetBrightness(settings.brightness);
}

uint8_t displayRotationSteps() {
  return 4;
}

// Setting 0 is the landscape orientation the frame always had (TFT_eSPI rotation 3, USB
// connector on the left); 1..3 turn it on in 90 degree steps.
void displayApplyRotation() {
  tft.setRotation((3 + settings.rotation) & 3);
}

// The rotation's MADCTL value as TFT_eSPI sets it for the ST7796, with the panel's red/blue
// order optionally flipped (a setting instead of a build option).
void displayApplyColorOrder() {
  static const uint8_t baseFor[4] = {TFT_MAD_MX, TFT_MAD_MV, TFT_MAD_MY, TFT_MAD_MX | TFT_MAD_MY | TFT_MAD_MV};
  const uint8_t base = baseFor[tft.getRotation() & 3];
  const uint8_t order = settings.swapRB ? (TFT_MAD_COLOR_ORDER ^ TFT_MAD_BGR) : TFT_MAD_COLOR_ORDER;
  tft.writecommand(TFT_MADCTL);
  tft.writedata(base | order);
}

// The build decides whether the panel is inverted (TFT_INVERSION_ON/OFF); the
// setting flips that, for panels that need the opposite (photos look like a negative).
void displayApplyInversion() {
#ifdef TFT_INVERSION_ON
  tft.invertDisplay(!settings.invert);
#else
  tft.invertDisplay(settings.invert);
#endif
}

// ---------------------------------------------------------------- JPEG
static File jpgFile;

static void *jpgOpen(const char *filename, int32_t *size) {
  jpgFile = SD_MMC.open(filename, FILE_READ);
  if (!jpgFile) return nullptr;
  *size = jpgFile.size();
  return &jpgFile;
}

static void jpgClose(void *handle) {
  if (jpgFile) jpgFile.close();
}

static int32_t jpgRead(JPEGFILE *handle, uint8_t *buffer, int32_t length) {
  int32_t n = jpgFile.read(buffer, length);
  if (n > 0) handle->iPos += n;
  return n;
}

static int32_t jpgSeek(JPEGFILE *handle, int32_t position) {
  handle->iPos = position;
  return jpgFile.seek(position);
}

static struct {
  uint16_t *buf = nullptr;
  int w = 0, h = 0;  // decoded size
} img;

static int jpegCopy(JPEGDRAW *d) {
  for (int r = 0; r < d->iHeight; r++) {
    int y = d->y + r;
    if (y >= img.h) break;
    int n = min(d->iWidth, img.w - d->x);
    if (n > 0) memcpy(img.buf + (size_t)y * img.w + d->x, d->pPixels + r * d->iWidth, n * 2);
  }
  return 1;
}

static inline uint16_t lerp565(uint16_t a, uint16_t b, uint32_t t /*0..256*/) {
  uint32_t ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
  uint32_t br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
  uint32_t r = (ar * (256 - t) + br * t) >> 8;
  uint32_t g = (ag * (256 - t) + bg * t) >> 8;
  uint32_t bl = (ab * (256 - t) + bb * t) >> 8;
  return (r << 11) | (g << 5) | bl;
}

#define BAND_ROWS 8
static uint16_t bandBuf[480 * BAND_ROWS];
static uint16_t xi0[480], xi1[480];
static uint8_t xfr[480];

// Scale img to dstW x dstH and draw it at (ox, oy).
static void drawScaled(int ox, int oy, int dstW, int dstH) {
  const float sxStep = (float)img.w / dstW, syStep = (float)img.h / dstH;
  for (int dx = 0; dx < dstW; dx++) {
    float fx = (dx + 0.5f) * sxStep - 0.5f;
    if (fx < 0) fx = 0;
    int i0 = (int)fx;
    if (i0 > img.w - 1) i0 = img.w - 1;
    xi0[dx] = i0;
    xi1[dx] = min(i0 + 1, img.w - 1);
    xfr[dx] = (uint8_t)((fx - i0) * 255.0f + 0.5f);
  }
  tft.startWrite();
  for (int by = 0; by < dstH; by += BAND_ROWS) {
    int rows = min(BAND_ROWS, dstH - by);
    for (int r = 0; r < rows; r++) {
      float fy = (by + r + 0.5f) * syStep - 0.5f;
      if (fy < 0) fy = 0;
      int j0 = (int)fy;
      if (j0 > img.h - 1) j0 = img.h - 1;
      int j1 = min(j0 + 1, img.h - 1);
      uint32_t ty = (uint32_t)((fy - j0) * 256.0f);
      const uint16_t *row0 = img.buf + (size_t)j0 * img.w, *row1 = img.buf + (size_t)j1 * img.w;
      uint16_t *out = bandBuf + r * dstW;
      for (int dx = 0; dx < dstW; dx++) {
        uint32_t tx = xfr[dx];
        uint16_t top = lerp565(row0[xi0[dx]], row0[xi1[dx]], tx);
        uint16_t bot = lerp565(row1[xi0[dx]], row1[xi1[dx]], tx);
        out[dx] = lerp565(top, bot, ty);
      }
    }
    tft.pushImage(ox, oy + by, dstW, rows, bandBuf);
  }
  tft.endWrite();
}

// Decode an already opened image and show it as large as fits the screen, centered.
// JPEGDEC can pre-shrink by 1/2, 1/4 or 1/8 at decode time, used when the picture is
// much bigger than the screen so the PSRAM buffer stays small.
static bool decodeOpened() {
  int w = jpeg.getWidth(), h = jpeg.getHeight();
  int W = tft.width(), H = tft.height();
  float fit = min((float)W / w, (float)H / h);  // > 1 enlarges, < 1 shrinks
  int div = 1;
  while (div < 8 && 1.0f / (div * 2) >= fit) div *= 2;  // decoded size stays >= target
  int option = div == 2 ? JPEG_SCALE_HALF : div == 4 ? JPEG_SCALE_QUARTER : div == 8 ? JPEG_SCALE_EIGHTH : 0;
  img.w = (w + div - 1) / div;
  img.h = (h + div - 1) / div;
  size_t bytes = (size_t)img.w * img.h * 2;
  img.buf = (uint16_t *)(psramFound() ? ps_malloc(bytes) : malloc(bytes));
  if (!img.buf) {
    Serial.printf("Out of memory for %dx%d (%u bytes)\n", img.w, img.h, (unsigned)bytes);
    return false;
  }
  bool ok = jpeg.decode(0, 0, option) == 1;
  if (ok) {
    int dstW = constrain((int)(w * fit + 0.5f), 1, W);
    int dstH = constrain((int)(h * fit + 0.5f), 1, H);
    if (dstW < W || dstH < H) tft.fillScreen(TFT_BLACK);
    drawScaled((W - dstW) / 2, (H - dstH) / 2, dstW, dstH);
  }
  free(img.buf);
  img.buf = nullptr;
  return ok;
}

bool displayPhoto(const String &name) {
  String path = "/" + name;
  if (!storageLock(3000)) return false;
  bool ok = false;
  if (jpeg.open(path.c_str(), jpgOpen, jpgClose, jpgRead, jpgSeek, jpegCopy)) {
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
  centered(title, 100, 4, color);
  if (line1) centered(line1, 150, 2, TFT_WHITE);
  if (line2) centered(line2, 176, 2, TFT_WHITE);
}

void displayShowSplash() {
  tft.fillScreen(TFT_BLACK);
  bool drawn = false;
  if (SPIFFS.exists("/vanity.jpg")) {
    File f = SPIFFS.open("/vanity.jpg");
    size_t n = f ? f.size() : 0;
    uint8_t *buf = n ? (uint8_t *)malloc(n) : nullptr;
    if (buf && f.read(buf, n) == n && jpeg.openRAM(buf, n, jpegCopy)) {
      drawn = decodeOpened();
      jpeg.close();
    }
    free(buf);
    if (f) f.close();
  }
  if (!drawn) {
    centered("PhotoFrame", 120, 4, TFT_CYAN);
    centered("v" FW_VERSION, 160, 2, TFT_WHITE);
  }
}

void displayInfoWifiButton(int &x, int &y, int &w, int &h) {
  w = 100;
  h = 34;
  x = tft.width() - w - 8;
  y = tft.height() - h - 8;
}

static void drawInfoWifiButton() {
  int x, y, w, h;
  displayInfoWifiButton(x, y, w, h);
  tft.fillRoundRect(x, y, w, h, 6, TFT_BLUE);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("WiFi", x + w / 2, y + h / 2, 2);
  tft.setTextDatum(TL_DATUM);
}

void displayShowInfo(const String &ip) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("PhotoFrame", 16, 14, 4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Open in a browser:", 16, 72, 2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString(ip.length() ? ip : String("(not connected)"), 16, 96, 4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("or", 16, 140, 2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString(HOSTNAME ".local", 16, 162, 4);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  const bool portrait = tft.height() > tft.width();
  const int hintY = portrait ? 420 : 262;  // portrait: the QR code goes between the text and the hints
  tft.drawString("Touch left/right: prev/next", 16, hintY, 2);
  tft.drawString("Touch middle: this screen", 16, hintY + 22, 2);

  if (ip.length() == 0) {
    drawInfoWifiButton();
    return;
  }
  String url = "http://" + ip;
  QRCode qr;
  uint8_t qrData[qrcode_getBufferSize(4)];
  qrcode_initText(&qr, qrData, 4, ECC_MEDIUM, url.c_str());
  const int block = 5;
  const int quiet = 4;
  int side = qr.size * block + 2 * quiet;
  int x0 = portrait ? (tft.width() - side) / 2 : tft.width() - side - 12;
  int y0 = portrait ? 220 : 70;
  tft.fillRect(x0, y0, side, side, TFT_WHITE);
  for (int y = 0; y < qr.size; y++)
    for (int x = 0; x < qr.size; x++)
      if (qrcode_getModule(&qr, x, y)) tft.fillRect(x0 + quiet + x * block, y0 + quiet + y * block, block, block, TFT_BLACK);
  drawInfoWifiButton();
}

void displayShowOta(const char *line, int percent) {
  static int lastPercent = -1;
  if (percent < 0 || lastPercent < 0 || percent < lastPercent) {
    tft.fillScreen(TFT_BLACK);
    centered("Firmware update", 70, 4, TFT_CYAN);
    centered("Do not power off", 190, 2, TFT_WHITE);
    tft.drawRect(60, 130, tft.width() - 120, 28, TFT_WHITE);
  }
  if (percent >= 0) {
    tft.fillRect(62, 132, ((tft.width() - 124) * percent) / 100, 24, TFT_GREEN);
  }
  if (line) {
    tft.fillRect(0, 240, tft.width(), 30, TFT_BLACK);
    centered(line, 245, 2, TFT_YELLOW);
  }
  lastPercent = percent;
}
