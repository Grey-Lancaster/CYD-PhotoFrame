// CYD PhotoFrame - shared declarations
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

#define FW_VERSION "3.0"
#define HOSTNAME "photoframe"

// ---------------------------------------------------------------- pins
#define SD_CS 5

#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// Flip if left/right touch zones feel reversed on your board.
// #define TOUCH_FLIP_X

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
  bool nightEnabled = false;
  uint8_t nightStart = 22;     // hour (local) the screen turns off
  uint8_t nightEnd = 7;        // hour (local) the screen turns on
  char tz[48] = "EST5EDT,M3.2.0,M11.1.0";  // POSIX TZ string
};
extern Settings settings;
void settingsLoad();
void settingsSave();

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
void displaySetBrightness(uint8_t percent);   // 0 turns the backlight off
void displayShowSplash();
void displayShowMessage(const char *title, const char *line1 = nullptr, const char *line2 = nullptr, uint16_t color = TFT_WHITE);
void displayShowApInstructions();
void displayShowInfo(const String &ip);
void displayShowOta(const char *line, int percent);
bool displayPhoto(const String &name);        // false if it could not be decoded

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
extern String g_ip;
void applyTimezone();
