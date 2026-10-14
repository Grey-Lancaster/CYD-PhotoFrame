// Freenove 4.0" PhotoFrame - shared declarations
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <functional>

#define FW_VERSION "4.4-beta4"
#define HOSTNAME "photoframe"

// ---------------------------------------------------------------- pins (FNK0104S 4.0" ESP32-S3 display)
// Display pins are set in platformio.ini (TFT_eSPI build flags).
#define I2C_SDA 16      // FT6336U capacitive touch
#define I2C_SCL 15
#define TOUCH_RST 18
#define TOUCH_INT 17

#define SD_CLK 38       // 4-bit SDMMC
#define SD_CMD 40
#define SD_D0 39
#define SD_D1 41
#define SD_D2 48
#define SD_D3 47
#define SD_FREQ_KHZ 20000

#define AUDIO_MCLK 4    // ES8311 codec: I2S + amplifier enable (codec control shares the touch I2C bus)
#define AUDIO_BCLK 5
#define AUDIO_DIN 6
#define AUDIO_WS 7
#define AUDIO_DOUT 8
#define AUDIO_AMP_EN 1

// ---------------------------------------------------------------- timing
#define VANITY_MIN_MS 3000   // minimum time the splash is shown
#define INFO_SCREEN_MS 8000  // IP / QR screen after boot or on touch

// ---------------------------------------------------------------- settings
struct Settings {
  uint16_t speedSec = 10;      // seconds per photo
  bool shuffle = false;
  uint8_t brightness = 100;    // percent
  uint8_t volume = 50;         // percent
  bool soundOnUpload = true;
  bool swapRB = false;         // swap red/blue (panel colour order)
  bool invert = false;         // flip the build's colour inversion (photos look like a negative)
  uint8_t rotation = 0;        // 0..3 = 0/90/180/270 degrees; 0 is landscape with USB on the left, as before
  bool nightEnabled = false;
  uint8_t nightStart = 22;     // hour (local) the screen turns off
  uint8_t nightEnd = 7;        // hour (local) the screen turns on
  char tz[48] = "EST5EDT,M3.2.0,M11.1.0";  // POSIX TZ string
};
extern Settings settings;
void settingsLoad();
void settingsSave();
bool settingsLoadTouchCal(uint16_t cal[5]);        // unused on this board (capacitive touch), kept for settings.cpp
void settingsSaveTouchCal(const uint16_t cal[5]);
void settingsClearTouchCal();

// ---------------------------------------------------------------- storage (SD card + playlist)
// All SD access, the image list and the playlist position are protected by
// one mutex. Decoding a photo holds it for the whole decode.
struct ImageEntry {
  String name;
  uint32_t size;
};

bool storageBegin();                   // mount the card and scan it
bool storageReady();
bool storageLock(uint32_t timeoutMs);  // 0 = try once
void storageUnlock();

uint16_t playlistCount();
int playlistPosition();                           // 1-based, 0 when empty
bool playlistCurrent(String &name);
bool playlistStep(int delta, String &name);       // next/previous (wraps, reshuffles)
bool playlistSelect(const String &name, String &out);
bool playlistHas(const String &name, uint32_t *size = nullptr);
void playlistRescan();                            // after upload/delete; keeps current photo if it still exists
String playlistJson();

bool storageDeleteImage(const String &name);
bool storageDeleteAudio();
String storageAudioFile();                        // "/music.wav", "/music.mp3" or ""

// Streaming read for the web server. Returns bytes read, 0 at error,
// RESPONSE_TRY_AGAIN (0xFFFFFFFF) when the card is busy.
size_t storageServeRead(const String &name, size_t offset, uint8_t *buf, size_t len);

// Upload (one at a time)
enum UploadKind { UPLOAD_NONE, UPLOAD_IMAGE, UPLOAD_AUDIO };
bool uploadStart(const String &rawName, String &err);
bool uploadWrite(const uint8_t *data, size_t len);
bool uploadFinish(String &finalName, UploadKind &kind, String &err);
void uploadAbort();

// ---------------------------------------------------------------- display
extern TFT_eSPI tft;
void displayBegin();
void displayApplyInversion();                  // applies settings.invert
void displayApplyColorOrder();                 // applies settings.swapRB
void displayApplyRotation();                   // applies settings.rotation
uint8_t displayRotationSteps();                // 4: all four rotations
void displaySetBrightness(uint8_t percent);   // 0 turns the backlight off
void displayShowSplash();
void displayShowMessage(const char *title, const char *line1 = nullptr, const char *line2 = nullptr, uint16_t color = TFT_WHITE);
void displayShowInfo(const String &ip);
void displayShowOta(const char *line, int percent);
bool displayPhoto(const String &name);        // false if it could not be decoded

// ---------------------------------------------------------------- touch (FT6336U)
void touchBegin();
bool touchRead(int &x, int &y);               // screen pixels, in the current rotation
bool i2cLock(uint32_t timeoutMs);             // shared I2C bus (touch + audio codec)
void i2cUnlock();

// ---------------------------------------------------------------- WiFi setup (TouchWifiProvisioner + LVGL)
// LVGL is only used for the on-device network picker, before the app is online.
void wifiSetupBegin(const char *hostname, std::function<void(const String &ip)> onConnected);
void wifiSetupLoop();                         // call every loop() pass until connected
void wifiSetupReset();                        // forget the saved network(s) and show the picker
bool wifiHasSavedNetworks();

// ---------------------------------------------------------------- audio
bool audioPlay();                  // false if busy or no sound file
bool audioBusy();

// ---------------------------------------------------------------- web
void webBegin();
void webNotifyPhoto();             // tell browsers the photo changed
void webNotifyList();              // tell browsers the photo list changed
void webLoop();

// ---------------------------------------------------------------- frame commands (web -> loop)
enum CmdType : uint8_t { CMD_NEXT, CMD_PREV, CMD_SHOW, CMD_RESCAN, CMD_INFO };
struct Cmd {
  CmdType type;
  char name[96];
};
bool cmdSend(CmdType type, const char *name = nullptr);

extern volatile bool g_paused;
extern volatile bool g_night;
extern volatile bool g_otaActive;
extern volatile uint32_t g_restartAt;           // millis() deadline for a requested restart (0 = none)
extern String g_ip;
extern String g_resetReason;                  // why the frame last restarted (power on, crash, watchdog...)
void applyTimezone();
