/*
  CYD PhotoFrame 4.0

  ESP32 "Cheap Yellow Display" photo frame:
    - slideshow of JPEGs from the SD card (any size, large photos are scaled down)
    - web interface: upload (with in-browser resize), delete, settings, live preview
    - optional sound (music.wav / music.mp3) played through the speaker
    - touch: left/right = previous/next, middle = show IP + QR code
    - WiFi setup portal, mDNS (photoframe.local), OTA updates, night mode

  Created by Grey Lancaster with help from ChatGPT, Claude and the open-source community.
  See README.md for credits and build instructions.
*/

#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#ifndef TOUCH_CS
#include <XPT2046_Bitbang.h>
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
// Boards with their own touch pins (2.8" CYDs): bit-banged XPT2046.
// Boards where touch shares the display's SPI bus (3.5" CYD, TOUCH_CS set in the build)
// use TFT_eSPI's touch support instead: a bit-banged driver would take over the
// display's SPI pins and freeze the screen.
static XPT2046_Bitbang ts(XPT2046_MOSI, XPT2046_MISO, XPT2046_CLK, XPT2046_CS);
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
// True while the screen is touched; x is the horizontal position in screen pixels.
static bool readTouch(int &x) {
#ifdef TOUCH_CS
  uint16_t tx, ty;
  if (!tft.getTouch(&tx, &ty, 300)) return false;
  x = tx;
  return true;
#else
  TouchPoint p = ts.getTouch();
  if (p.zRaw < 400) return false;  // real touches read ~1000-2000
  x = p.x;
  return true;
#endif
}

// The bit-banged driver has to be re-initialised after sound (see handleInput).
static void touchReinit() {
#ifndef TOUCH_CS
  ts.begin();
#endif
}

// Boards using TFT_eSPI touch need a calibration (touch the four corners). It is
// done once and saved; the web page has a button to do it again.
static void touchBegin() {
#ifdef TOUCH_CS
  uint16_t cal[5];
  if (!settingsLoadTouchCal(cal)) {
    displayShowMessage("Touch calibration", "Touch each corner as shown", nullptr, TFT_CYAN);
    delay(2000);
    tft.fillScreen(TFT_BLACK);
    tft.calibrateTouch(cal, TFT_MAGENTA, TFT_BLACK, 20);
    settingsSaveTouchCal(cal);
  }
  tft.setTouch(cal);
#else
  ts.begin();
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

  // WiFi. A frame that already knows a network never opens the setup portal
  // (so it keeps working when the router is down); hold BOOT while powering on
  // to force the portal.
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  static WiFiManager wm;
  wm.setAPCallback([](WiFiManager *) { displayShowApInstructions(); });
  wm.setConfigPortalTimeout(180);
  wm.setConnectTimeout(20);
  if (setupRequested) wm.resetSettings();
  wm.setEnableConfigPortal(!wm.getWiFiIsSaved());
  bool online = wm.autoConnect("ESP32_AP");

  while (millis() - splashStart < VANITY_MIN_MS) delay(10);

  if (online) startNetworkServices();
  touchBegin();  // after the network is up: calibration (3.5" board) waits for touches, the web page keeps working
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
