/*
  PhotoFrame for the Freenove FNK0104S 4.0" ESP32-S3 display

    - slideshow of JPEGs from the SD card (small photos are enlarged, large ones scaled down)
    - web interface: upload (with in-browser resize), delete, settings, live preview
    - touch: left/right = previous/next, middle = show IP + QR code
    - on-device WiFi setup (TouchWifiProvisioner), mDNS (photoframe4.local), OTA updates, night mode

  Ported from CYD-PhotoFrame. Created by Grey Lancaster with help from ChatGPT, Claude
  and the open-source community.
*/

#include <WiFi.h>
#include <ESPmDNS.h>
#include <time.h>
#include "SPIFFS.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "app.h"

volatile bool g_paused = false;
volatile bool g_night = false;
volatile bool g_asleep = false;
volatile bool g_otaActive = false;
String g_ip;
String g_resetReason;

static QueueHandle_t cmdQueue;
volatile uint32_t g_restartAt = 0;
static volatile bool buttonPressed = false;
static volatile bool wifiConnected = false;
static bool servicesStarted = false;

enum ScreenState { SCREEN_NONE, SCREEN_PHOTO, SCREEN_NO_SD, SCREEN_EMPTY, SCREEN_INFO };
static ScreenState screen = SCREEN_NONE;
static String shownName;
static uint32_t lastChange = 0;
static uint32_t lastActivity = 0;  // last motion / touch, for the motion sleep
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

// The info screen's WiFi button: the on-screen picker with no timeout and a Cancel row; the
// saved networks stay. Joining a network restarts the frame so everything rebinds to it.
static void showInfoScreen();
static void wifiPickerModal() {
  wifiSetupOpenPicker();
  while (wifiSetupPickerOpen()) {
    esp_task_wdt_reset();
    wifiSetupLoop();
    delay(5);
  }
  if (wifiSetupJoined()) g_restartAt = millis() + 500;
  showInfoScreen();
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

// ---------------------------------------------------------------- motion sleep
static void wakeScreen(uint8_t cause) {
  if (!g_asleep) return;
  g_asleep = false;
  motionLogEvent(false, cause);
  if (!g_night) displaySetBrightness(settings.brightness);
  lastChange = millis();
  lastActivity = millis();
  Serial.println("Screen wake");
}

// With motion sensing on, the screen sleeps after motionMin minutes without motion or
// touch and wakes on either. If sensing is not running (offline, still warming up) the
// frame counts that as activity, so it never sleeps blind.
static void updateSleep() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 250) return;
  last = now;
  if (!settings.motionEnabled) {
    wakeScreen(4);
    return;
  }
  if (!motionReady() || motionDetected()) {
    lastActivity = now;
    wakeScreen(motionDetected() ? 1 : 5);
    return;
  }
  if (!g_asleep && now - lastActivity >= (uint32_t)settings.motionMin * 60000UL) {
    g_asleep = true;
    motionLogEvent(true, 0);
    displaySetBrightness(0);
    Serial.println("Screen sleep (no motion)");
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
    lastActivity = now;
    if (g_asleep) {
      wakeScreen(3);
      return;
    }
    if (now - lastAction > 300) {
      lastAction = now;
      stepAndShow(1);
    }
    return;
  }

  // One action per touch: act when the finger goes down, then require the
  // finger to be up for 250 ms before another touch counts (a photo change
  // takes a few hundred ms).
  static bool touching = false;
  static uint32_t lastDown = 0;
  int x = 0, y = 0;
  if (!touchRead(x, y)) {
    if (touching && now - lastDown > 250) touching = false;
    return;
  }
  lastDown = now;
  lastActivity = now;
  if (touching || now - lastAction < 500) return;
  touching = true;
  if (g_asleep) {  // the first touch only wakes the screen
    wakeScreen(2);
    lastAction = millis();
    return;
  }
  Serial.printf("[%lu] Touch x=%d y=%d\n", (unsigned long)now, x, y);

  int bx, by, bw, bh;
  displayInfoWifiButton(bx, by, bw, bh);
  if (screen == SCREEN_INFO && x >= bx && x < bx + bw && y >= by && y < by + bh) {
    wifiPickerModal();
  } else if (x < tft.width() / 3) {
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
    displaySetBrightness(night || g_asleep ? 0 : settings.brightness);
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
  Serial.println("\nPhotoFrame (Freenove 4\" S3) " FW_VERSION);
  g_resetReason = resetReasonText(esp_reset_reason());
  Serial.println(String("Reset reason: ") + g_resetReason);

  cmdQueue = xQueueCreate(8, sizeof(Cmd));
  pinMode(0, INPUT_PULLUP);
  bool setupRequested = digitalRead(0) == LOW;  // BOOT button held at power-on
  attachInterrupt(0, buttonInt, FALLING);

  settingsLoad();
  displayBegin();
  touchBegin();
  if (!SPIFFS.begin(true)) Serial.println("SPIFFS mount failed");
  displayShowSplash();
  uint32_t splashStart = millis();

  storageBegin();
  while (millis() - splashStart < VANITY_MIN_MS) delay(10);

  // WiFi. A frame that already knows a network does not wait forever for it (so it
  // keeps working when the router is down); hold BOOT while powering on to forget
  // the saved network(s) and open the on-device picker.
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  wifiSetupBegin(HOSTNAME, [](const String &ip) { wifiConnected = true; });
  if (setupRequested) wifiSetupReset();
  bool hadSaved = !setupRequested && wifiHasSavedNetworks();
  uint32_t wifiStart = millis();
  while (!wifiConnected) {
    wifiSetupLoop();
    delay(5);
    if (hadSaved && millis() - wifiStart > 30000) break;  // known network not reachable: run offline
  }
  bool online = wifiConnected;

  motionBegin();
  lastActivity = millis();
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
  // Only armed here, after set-up (the WiFi picker may wait for people).
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
  updateSleep();
  handleInput();
  handleCommands();

  // Input and commands can take a while (a photo change is a few hundred ms) and
  // reset lastChange, so read the clock again; a stale `now` would be older
  // than lastChange and the unsigned difference would wrap to "overdue".
  now = millis();
  if (screen == SCREEN_INFO) {
    if ((int32_t)(now - infoUntil) >= 0) showCurrentPhoto();
  } else if (!g_paused && !g_night && !g_asleep && !audioBusy() && now - lastChange >= (uint32_t)settings.speedSec * 1000UL) {
    uint16_t n = playlistCount();
    if (n > 1) stepAndShow(1);
    else if (n == 1 && shownName.length() == 0) showCurrentPhoto();
    lastChange = millis();
  }

  delay(10);
}
