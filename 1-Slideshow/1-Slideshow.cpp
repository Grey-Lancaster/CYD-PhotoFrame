/*
  CYD PhotoFrame 4.0

  ESP32 "Cheap Yellow Display" photo frame:
    - slideshow of JPEGs from the SD card (any size, large photos are scaled down)
    - web interface: upload (with in-browser resize), delete, settings, live preview
    - optional sound (music.wav / music.mp3) played through the speaker
    - touch: left/right = previous/next, middle = show IP + QR code
    - WiFi set up on the frame's own touch screen, mDNS (photoframe.local), OTA updates, night mode

  Created by Grey Lancaster with help from ChatGPT, Claude and the open-source community.
  See README.md for credits and build instructions.
*/

#include <WiFi.h>
#include <ESPmDNS.h>
#ifndef TOUCH_CS
#endif
#include <time.h>
#include "SPIFFS.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "app.h"

volatile bool g_paused = false;
volatile bool g_night = false;
volatile bool g_otaActive = false;
String g_ip;
String g_resetReason;

static QueueHandle_t cmdQueue;
#ifndef TOUCH_CS
// Boards with their own touch pins (2.8" CYDs): bit-banged XPT2046 (see below).
// Boards where touch shares the display's SPI bus (3.5" CYD, TOUCH_CS set in the build)
// use TFT_eSPI's touch support instead: a bit-banged driver would take over the
// display's SPI pins and freeze the screen.
#endif
volatile uint32_t g_restartAt = 0;
static volatile bool buttonPressed = false;
static bool servicesStarted = false;

enum ScreenState { SCREEN_NONE, SCREEN_PHOTO, SCREEN_NO_SD, SCREEN_EMPTY, SCREEN_INFO };
static ScreenState screen = SCREEN_NONE;
static String shownName;
static uint32_t lastChange = 0;
static uint32_t infoUntil = 0;

static const char *resetReasonText(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "Power on";
    case ESP_RST_EXT: return "Reset button";
    case ESP_RST_SW: return "Restart (software)";
    case ESP_RST_PANIC: return "Crash (panic)";
    case ESP_RST_INT_WDT: return "Crash (interrupt watchdog)";
    case ESP_RST_TASK_WDT: return "Hang (watchdog restarted it)";
    case ESP_RST_WDT: return "Hang (watchdog)";
    case ESP_RST_BROWNOUT: return "Brownout (power dip)";
    case ESP_RST_DEEPSLEEP: return "Deep sleep wake";
    default: return "Unknown";
  }
}

void IRAM_ATTR buttonInt() {
  buttonPressed = true;
}

bool cmdSend(CmdType type, const char *name) {
  Cmd c;
  memset(&c, 0, sizeof(c));
  c.type = type;
  if (name) strlcpy(c.name, name, sizeof(c.name));
  return xQueueSend(cmdQueue, &c, 0) == pdTRUE;
}

void applyTimezone() {
  if (WiFi.status() == WL_CONNECTED) configTzTime(settings.tz, "pool.ntp.org", "time.nist.gov");
}

// ---------------------------------------------------------------- showing things
static void showEmptyScreen() {
  if (!storageReady()) {
    displayShowMessage("No SD card", "Insert a card with .jpg photos", nullptr, TFT_RED);
    screen = SCREEN_NO_SD;
  } else {
    displayShowMessage("No photos yet", g_ip.length() ? "Open this address to upload:" : "Add .jpg files to the SD card", g_ip.length() ? g_ip.c_str() : nullptr, TFT_CYAN);
    screen = SCREEN_EMPTY;
  }
  shownName = "";
}

// skipDir: which way to move on if a photo cannot be decoded
static void showCurrentPhoto(int skipDir = 1) {
  int n = playlistCount();
  if (n == 0) {
    showEmptyScreen();
    return;
  }
  for (int tries = 0; tries < n; tries++) {
    String name;
    if (!playlistCurrent(name)) break;
    if (displayPhoto(name)) {
      shownName = name;
      screen = SCREEN_PHOTO;
      lastChange = millis();
      Serial.printf("[%lu] Showing %d/%d %s\n", (unsigned long)millis(), playlistPosition(), n, name.c_str());
      webNotifyPhoto();
      return;
    }
    Serial.printf("[%lu] Cannot decode %s, skipping\n", (unsigned long)millis(), name.c_str());
    playlistStep(skipDir, name);  // skip a photo we cannot decode
  }
  displayShowMessage("Cannot show photos", "Unsupported or corrupt JPEG", "(progressive JPEGs don't work)", TFT_RED);
  screen = SCREEN_NONE;
  shownName = "";
  lastChange = millis();
}

static void stepAndShow(int delta) {
  String name;
  if (playlistStep(delta, name)) showCurrentPhoto(delta);
}

static void showInfoScreen() {
  displayShowInfo(g_ip);
  screen = SCREEN_INFO;
  infoUntil = millis() + INFO_SCREEN_MS;
}

static void handleCommands() {
  Cmd c;
  while (xQueueReceive(cmdQueue, &c, 0) == pdTRUE) {
    switch (c.type) {
      case CMD_NEXT:
        stepAndShow(1);
        break;
      case CMD_PREV:
        stepAndShow(-1);
        break;
      case CMD_SHOW: {
        String out;
        if (playlistSelect(c.name, out)) showCurrentPhoto();
        break;
      }
      case CMD_RESCAN: {
        if (playlistCount() == 0) {
          if (storageReady() ? screen != SCREEN_EMPTY : screen != SCREEN_NO_SD) showEmptyScreen();
        } else {
          String cur;
          playlistCurrent(cur);
          if (cur != shownName || screen != SCREEN_PHOTO) showCurrentPhoto();
        }
        break;
      }
      case CMD_INFO:
        showInfoScreen();
        break;
    }
  }
}

// ---------------------------------------------------------------- touch hardware
// Screen position of the current touch, in pixels. Remembers when the screen was last
// touched (the WiFi setup waits for people while they are typing).
static uint32_t lastTouchMs = 0;

#ifndef TOUCH_CS
// The 2.8" boards' resistive panels differ between revisions (axes swapped, skewed,
// different ranges), so the first start asks for five touches and fits a transform from
// raw readings to screen pixels (least squares, same method as CYD_Weather). It is
// stored; the web page's "Recalibrate touch" button asks again.
static float touchAff[6];
static bool haveAff = false;

static bool solve3(const double A[3][3], const double b[3], double out[3]) {
  auto det = [](const double m[3][3]) {
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
  };
  double d = det(A);
  if (fabs(d) < 1e-6) return false;
  for (int c = 0; c < 3; c++) {
    double M[3][3];
    memcpy(M, A, sizeof(M));
    for (int r = 0; r < 3; r++) M[r][c] = b[r];
    out[c] = det(M) / d;
  }
  return true;
}

// Least-squares fit of sx = a*rx + b*ry + c and sy = d*rx + e*ry + f; returns the RMS error in pixels.
static bool fitAffine(const double *rx, const double *ry, const double *sx, const double *sy, int n, float out[6], double &rms) {
  double A[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, (double)n}};
  double bx[3] = {0, 0, 0}, by[3] = {0, 0, 0};
  for (int i = 0; i < n; i++) {
    A[0][0] += rx[i] * rx[i];
    A[0][1] += rx[i] * ry[i];
    A[0][2] += rx[i];
    A[1][1] += ry[i] * ry[i];
    A[1][2] += ry[i];
    bx[0] += rx[i] * sx[i]; bx[1] += ry[i] * sx[i]; bx[2] += sx[i];
    by[0] += rx[i] * sy[i]; by[1] += ry[i] * sy[i]; by[2] += sy[i];
  }
  A[1][0] = A[0][1];
  A[2][0] = A[0][2];
  A[2][1] = A[1][2];
  double px[3], py[3];
  if (!solve3(A, bx, px) || !solve3(A, by, py)) return false;
  double sum = 0;
  for (int i = 0; i < n; i++) {
    double ex = px[0] * rx[i] + px[1] * ry[i] + px[2] - sx[i];
    double ey = py[0] * rx[i] + py[1] * ry[i] + py[2] - sy[i];
    sum += ex * ex + ey * ey;
  }
  rms = sqrt(sum / n);
  for (int k = 0; k < 3; k++) {
    out[k] = (float)px[k];
    out[3 + k] = (float)py[k];
  }
  return true;
}

// XPT2046 read over bit-banged GPIO (its own pins, so it cannot disturb the display or SD
// card buses). The library this used to call read Y straight after X with no settling
// conversion, which made Y wrong by hundreds of counts on some panels; this version does a
// throw-away conversion first and takes the median of three samples per axis.
struct RawTouch {
  uint16_t xRaw, yRaw, zRaw;
};

static void xptWrite(uint8_t cmd) {
  for (int i = 7; i >= 0; i--) {
    digitalWrite(XPT2046_MOSI, cmd & (1 << i));
    digitalWrite(XPT2046_CLK, LOW);
    delayMicroseconds(5);
    digitalWrite(XPT2046_CLK, HIGH);
    delayMicroseconds(5);
  }
  digitalWrite(XPT2046_MOSI, LOW);
  digitalWrite(XPT2046_CLK, LOW);
}

static uint16_t xptConvert(uint8_t cmd) {
  xptWrite(cmd);
  uint16_t v = 0;
  for (int i = 15; i >= 0; i--) {
    digitalWrite(XPT2046_CLK, HIGH);
    delayMicroseconds(5);
    digitalWrite(XPT2046_CLK, LOW);
    delayMicroseconds(5);
    v |= digitalRead(XPT2046_MISO) << i;
  }
  return v >> 4;
}

static uint16_t median3(uint16_t a, uint16_t b, uint16_t c) {
  if (a > b) { uint16_t t = a; a = b; b = t; }
  if (b > c) { b = c; }
  return a > b ? a : b;
}

static void xptBegin() {
  pinMode(XPT2046_MOSI, OUTPUT);
  pinMode(XPT2046_MISO, INPUT);
  pinMode(XPT2046_CLK, OUTPUT);
  pinMode(XPT2046_CS, OUTPUT);
  digitalWrite(XPT2046_CS, HIGH);
  digitalWrite(XPT2046_CLK, LOW);
}

// True while the panel is pressed (pressure reads ~1000-2000 for a real touch).
static bool rawTouched(RawTouch &p) {
  digitalWrite(XPT2046_CS, LOW);
  int z = xptConvert(0xB1) + 4095 - xptConvert(0xC1);
  if (z < 400) {
    digitalWrite(XPT2046_CS, HIGH);
    return false;
  }
  xptConvert(0x91);  // first conversion after a channel change is unreliable
  xptConvert(0xD1);
  uint16_t x[3], y[3];
  for (int k = 0; k < 3; k++) {
    y[k] = xptConvert(0xD1);
    x[k] = xptConvert(0x91);
  }
  xptConvert(0xD0);  // back to power-down
  digitalWrite(XPT2046_CS, HIGH);
  p.xRaw = median3(x[0], x[1], x[2]);
  p.yRaw = median3(y[0], y[1], y[2]);
  p.zRaw = z;
  return true;
}

static void calibrateBitbang() {
  const int W = tft.width(), H = tft.height(), inset = 30;
  const int tx[5] = {inset, W - inset, W - inset, inset, W / 2};
  const int ty[5] = {inset, inset, H - inset, H - inset, H / 2};
  double rx[5], ry[5], sx[5], sy[5];
  float fit[6];
  double rms = 0;
  bool ok = false;

  displayShowMessage("Touch calibration", "Touch each crosshair as shown", nullptr, TFT_CYAN);
  delay(2000);
  RawTouch p;
  for (int attempt = 0; attempt < 3 && !ok; attempt++) {
    for (int i = 0; i < 5; i++) {
      tft.fillScreen(TFT_BLACK);
      tft.drawFastHLine(tx[i] - 14, ty[i], 29, TFT_WHITE);
      tft.drawFastVLine(tx[i], ty[i] - 14, 29, TFT_WHITE);
      tft.drawCircle(tx[i], ty[i], 8, TFT_GREEN);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString(String("Touch the crosshair ") + (i + 1) + "/5", W / 2, i == 4 ? H / 2 + 36 : H / 2, 2);
      while (rawTouched(p)) delay(10);  // let go of the previous target first
      delay(250);
      while (!rawTouched(p)) delay(10);
      double sumX = 0, sumY = 0;
      int n = 0;
      delay(60);  // skip the contact bounce
      for (int k = 0; k < 24 && rawTouched(p); k++) {
        sumX += p.xRaw;
        sumY += p.yRaw;
        n++;
        delay(8);
      }
      if (n == 0) { i--; continue; }  // lifted too fast: ask again
      rx[i] = sumX / n; ry[i] = sumY / n; sx[i] = tx[i]; sy[i] = ty[i];
      Serial.printf("Touch cal %d: screen %d,%d raw %.0f,%.0f\n", i + 1, tx[i], ty[i], rx[i], ry[i]);
    }
    ok = fitAffine(rx, ry, sx, sy, 5, fit, rms) && rms <= 12.0;
    Serial.printf("Touch cal fit: %s, rms %.1f px\n", ok ? "ok" : "inconsistent", rms);
    if (!ok) {
      displayShowMessage("Taps were not consistent", "Let's try again", nullptr, TFT_ORANGE);
      delay(1800);
    }
  }
  tft.fillScreen(TFT_BLACK);
  if (ok) {
    memcpy(touchAff, fit, sizeof(touchAff));
    haveAff = true;
    settingsSaveTouchAff(touchAff);
  }
}
#endif

bool touchReadXY(int &x, int &y) {
#ifdef TOUCH_CS
  uint16_t tx, ty;
  if (!tft.getTouch(&tx, &ty, 300)) return false;
  x = tx;
  y = ty;
#else
  RawTouch p;
  if (!rawTouched(p)) return false;
  if (haveAff) {
    x = lroundf(touchAff[0] * p.xRaw + touchAff[1] * p.yRaw + touchAff[2]);
    y = lroundf(touchAff[3] * p.xRaw + touchAff[4] * p.yRaw + touchAff[5]);
  } else {  // calibration failed: rough linear guess
    x = map(p.xRaw, 0, 4095, 0, tft.width() - 1);
    y = map(p.yRaw, 0, 4095, 0, tft.height() - 1);
  }
  x = constrain(x, 0, tft.width() - 1);
  y = constrain(y, 0, tft.height() - 1);
#endif
  lastTouchMs = millis();
  return true;
}

// True while the screen is touched; x is the horizontal position in screen pixels.
static bool readTouch(int &x) {
  int y;
  return touchReadXY(x, y);
}

// The bit-banged driver has to be re-initialised after sound (see handleInput).
static void touchReinit() {
#ifndef TOUCH_CS
  xptBegin();
#endif
}

// Both kinds of board need a calibration on first start (the web page has a button to do
// it again): the 3.5" board via TFT_eSPI's four-corner routine, the 2.8" boards with the
// five-point fit above.
static void touchBegin() {
#ifdef TOUCH_CS
  uint16_t cal[5];
  if (!settingsLoadTouchCal(settings.rotation, cal)) {
    displayShowMessage("Touch calibration", "Touch each corner as shown", nullptr, TFT_CYAN);
    delay(2000);
    tft.fillScreen(TFT_BLACK);
    tft.calibrateTouch(cal, TFT_MAGENTA, TFT_BLACK, 20);
    settingsSaveTouchCal(settings.rotation, cal);
  }
  tft.setTouch(cal);
#else
  xptBegin();
  haveAff = settingsLoadTouchAff(touchAff);
  if (!haveAff) calibrateBitbang();
#endif
}

// ---------------------------------------------------------------- input
static void handleInput() {
  static uint32_t lastPoll = 0;
  static uint32_t lastAction = 0;
  uint32_t now = millis();
  if (now - lastPoll < 40) return;
  lastPoll = now;

  if (buttonPressed) {
    buttonPressed = false;
    if (now - lastAction > 300) {
      lastAction = now;
      stepAndShow(1);
    }
    return;
  }

  // While sound plays the DAC shares a pin with the touch controller, so
  // touch readings are unreliable: ignore them, and re-init touch afterwards.
  static bool wasAudio = false;
  static uint32_t audioEnded = 0;
  if (audioBusy()) {
    wasAudio = true;
    return;
  }
  if (wasAudio) {
    wasAudio = false;
    audioEnded = now;
    touchReinit();
  }
  if (now - audioEnded < 1000) return;

  // One action per touch: act when the finger goes down, then require the
  // finger to be up for 250 ms before another touch counts (the pressure
  // reading flickers during a press, and a photo change takes ~0.4 s).
  static bool touching = false;
  static uint32_t lastDown = 0;
  int x = 0;
  if (!readTouch(x)) {
    if (touching && now - lastDown > 250) touching = false;
    return;
  }
  lastDown = now;
  if (touching || now - lastAction < 500) return;
  touching = true;
  Serial.println(String("[") + now + "] Touch x=" + x);

#ifdef TOUCH_FLIP_X
  x = tft.width() - x;
#endif
  if (x < tft.width() / 3) {
    stepAndShow(-1);
  } else if (x > tft.width() * 2 / 3) {
    stepAndShow(1);
  } else if (screen == SCREEN_INFO) {
    infoUntil = now;  // dismiss
  } else {
    showInfoScreen();
  }
  lastAction = millis();  // measured after the (slow) redraw
}

// ---------------------------------------------------------------- night mode
static bool inNightWindow() {
  if (!settings.nightEnabled) return false;
  struct tm t;
  if (!getLocalTime(&t, 0)) return false;  // clock not set yet
  int h = t.tm_hour, s = settings.nightStart, e = settings.nightEnd;
  if (s == e) return false;
  return s < e ? (h >= s && h < e) : (h >= s || h < e);
}

static void updateNight() {
  static uint32_t last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  bool night = inNightWindow();
  if (night != g_night) {
    g_night = night;
    displaySetBrightness(night ? 0 : settings.brightness);
    lastChange = millis();
  }
}

// ---------------------------------------------------------------- network
static void startNetworkServices() {
  if (servicesStarted) return;
  servicesStarted = true;
  g_ip = WiFi.localIP().toString();
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS: " HOSTNAME ".local");
  }
  applyTimezone();
  webBegin();
  Serial.printf("IP address: %s\n", g_ip.c_str());
}

// ---------------------------------------------------------------- setup / loop
void setup() {
  Serial.begin(115200);
  Serial.println("\nCYD PhotoFrame " FW_VERSION);
  g_resetReason = resetReasonText(esp_reset_reason());
  Serial.println(String("Reset reason: ") + g_resetReason);

  cmdQueue = xQueueCreate(8, sizeof(Cmd));
  pinMode(0, INPUT);
  bool setupRequested = digitalRead(0) == LOW;  // BOOT button held at power-on
  attachInterrupt(0, buttonInt, FALLING);
  // CYD RGB LED off
  pinMode(4, OUTPUT); digitalWrite(4, HIGH);
  pinMode(16, OUTPUT); digitalWrite(16, HIGH);
  pinMode(17, OUTPUT); digitalWrite(17, HIGH);
#ifdef BOARD_CYD35
  pinMode(22, OUTPUT); digitalWrite(22, HIGH);  // red LED on the 3.5" board
#endif

  settingsLoad();
  displayBegin();
  if (!SPIFFS.begin(true)) Serial.println("SPIFFS mount failed");
  displayShowSplash();
  uint32_t splashStart = millis();

  storageBegin();
  touchBegin();  // before WiFi: the on-screen picker needs it (the 3.5" board calibrates here on first start)
  while (millis() - splashStart < VANITY_MIN_MS) delay(10);

  // WiFi. A frame that already knows a network does not wait forever for it (so it
  // keeps working when the router is down); hold BOOT while powering on to forget
  // the saved network(s) and open the on-screen picker. A frame with no network at
  // all gives up after 3 minutes without a touch and runs as a plain slideshow.
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  static bool wifiConnected = false;
  wifiSetupBegin(HOSTNAME, [](const String &) { wifiConnected = true; });
  if (setupRequested) wifiSetupReset();
  bool hadSaved = !setupRequested && wifiHasSavedNetworks();
  uint32_t wifiStart = millis();
  while (!wifiConnected) {
    wifiSetupLoop();
    delay(5);
    uint32_t idleSince = max(wifiStart, lastTouchMs);
    if (hadSaved && millis() - wifiStart > 30000) break;  // known network not reachable: run offline
    if (!hadSaved && millis() - idleSince > 180000) break;  // nobody is setting it up
  }
  bool online = wifiConnected;

  if (online) startNetworkServices();
  if (online) {
    playlistCount() ? showInfoScreen() : showEmptyScreen();
  } else {
    Serial.println("Offline - slideshow only, will keep trying to reconnect");
    if (playlistCount()) showCurrentPhoto();
    else showEmptyScreen();
  }
  lastChange = millis();

  // Watchdog: if the main loop ever hangs for 30 s, restart instead of freezing.
  // Only armed here, after set-up (WiFi portal and touch calibration may wait for people).
  esp_task_wdt_init(30, true);
  esp_task_wdt_add(NULL);
}

void loop() {
  static bool wasOta = false;
  static uint32_t lastMountTry = 0, lastWifiTry = 0;
  uint32_t now = millis();
  esp_task_wdt_reset();

  if (servicesStarted) webLoop();
  if (g_restartAt && (int32_t)(now - g_restartAt) >= 0) ESP.restart();

  if (g_otaActive) {
    wasOta = true;
    delay(20);
    return;
  }
  if (wasOta) {  // OTA failed, bring the picture back
    wasOta = false;
    showCurrentPhoto();
  }

  // SD card missing at boot: keep trying (also covers inserting it later)
  if (!storageReady() && now - lastMountTry > 3000) {
    lastMountTry = now;
    if (storageBegin()) cmdSend(CMD_RESCAN);
    else if (screen != SCREEN_NO_SD) showEmptyScreen();
  }

  // Offline at boot: retry the saved network in the background
  if (!servicesStarted && now - lastWifiTry > 30000) {
    lastWifiTry = now;
    if (WiFi.status() == WL_CONNECTED) startNetworkServices();
    else WiFi.begin();
  }
  if (servicesStarted && now - lastWifiTry > 5000) {
    lastWifiTry = now;
    if (WiFi.status() == WL_CONNECTED) g_ip = WiFi.localIP().toString();
  }

  updateNight();
  handleInput();
  handleCommands();

  // Input and commands can take a while (a photo change is ~0.2-0.4 s) and
  // reset lastChange, so read the clock again; a stale `now` would be older
  // than lastChange and the unsigned difference would wrap to "overdue".
  now = millis();
  if (screen == SCREEN_INFO) {
    if ((int32_t)(now - infoUntil) >= 0) showCurrentPhoto();
  } else if (!g_paused && !g_night && !audioBusy() && now - lastChange >= (uint32_t)settings.speedSec * 1000UL) {
    uint16_t n = playlistCount();
    if (n > 1) stepAndShow(1);
    else if (n == 1 && shownName.length() == 0) showCurrentPhoto();
    lastChange = millis();
  }

  delay(10);
}
