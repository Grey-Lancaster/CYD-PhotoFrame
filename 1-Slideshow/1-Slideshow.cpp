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
#include <XPT2046_Bitbang.h>
#include <time.h>
#include "SPIFFS.h"
#include "app.h"

volatile bool g_paused = false;
volatile bool g_night = false;
volatile bool g_otaActive = false;
String g_ip;

static QueueHandle_t cmdQueue;
static XPT2046_Bitbang ts(XPT2046_MOSI, XPT2046_MISO, XPT2046_CLK, XPT2046_CS);
static volatile bool buttonPressed = false;
static bool servicesStarted = false;

enum ScreenState { SCREEN_NONE, SCREEN_PHOTO, SCREEN_NO_SD, SCREEN_EMPTY, SCREEN_INFO };
static ScreenState screen = SCREEN_NONE;
static String shownName;
static uint32_t lastChange = 0;
static uint32_t infoUntil = 0;

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

static void showCurrentPhoto() {
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
      webNotifyPhoto();
      return;
    }
    playlistStep(1, name);  // skip a photo we cannot decode
  }
  displayShowMessage("Cannot show photos", "Unsupported or corrupt JPEG", "(progressive JPEGs don't work)", TFT_RED);
  screen = SCREEN_NONE;
  shownName = "";
  lastChange = millis();
}

static void stepAndShow(int delta) {
  String name;
  if (playlistStep(delta, name)) showCurrentPhoto();
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
    ts.begin();
  }
  if (now - audioEnded < 1000) return;

  TouchPoint p = ts.getTouch();
  if (p.zRaw < 400 || now - lastAction < 500) return;  // real touches read ~1000-2000
  lastAction = now;
  Serial.printf("Touch x=%u y=%u z=%u\n", p.x, p.y, p.zRaw);

  int x = p.x;
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

  cmdQueue = xQueueCreate(8, sizeof(Cmd));
  pinMode(0, INPUT);
  bool setupRequested = digitalRead(0) == LOW;  // BOOT button held at power-on
  attachInterrupt(0, buttonInt, FALLING);
  // CYD RGB LED off
  pinMode(4, OUTPUT); digitalWrite(4, HIGH);
  pinMode(16, OUTPUT); digitalWrite(16, HIGH);
  pinMode(17, OUTPUT); digitalWrite(17, HIGH);

  settingsLoad();
  displayBegin();
  if (!SPIFFS.begin(true)) Serial.println("SPIFFS mount failed");
  displayShowSplash();
  uint32_t splashStart = millis();

  ts.begin();
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

  if (online) {
    startNetworkServices();
    playlistCount() ? showInfoScreen() : showEmptyScreen();
  } else {
    Serial.println("Offline - slideshow only, will keep trying to reconnect");
    if (playlistCount()) showCurrentPhoto();
    else showEmptyScreen();
  }
  lastChange = millis();
}

void loop() {
  static bool wasOta = false;
  static uint32_t lastMountTry = 0, lastWifiTry = 0;
  uint32_t now = millis();

  if (servicesStarted) webLoop();

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
