// SD card access, photo list / playlist, uploads and deletes.
//
// Everything that touches SdFat or the photo list goes through sdMutex, so the
// slideshow loop and the async web server never use the card at the same time.

#include <SPI.h>
#include <SdFat.h>
#include <algorithm>
#include <numeric>
#include <vector>
#include "app.h"

#ifndef RESPONSE_TRY_AGAIN
#define RESPONSE_TRY_AGAIN 0xFFFFFFFF
#endif

#define UPLOAD_TMP "/_upload.tmp"

static SPIClass sdSpi(VSPI);
static SdFat sd;
static SdBaseFile sdRoot;
static SemaphoreHandle_t sdMutex = nullptr;
static bool sdOk = false;

static std::vector<ImageEntry> images;  // sorted by name
static std::vector<uint16_t> order;     // play order (indexes into images)
static int pos = 0;                     // index into order
static String audioCache;

// ---------------------------------------------------------------- locking
bool storageLock(uint32_t timeoutMs) {
  if (!sdMutex) return false;
  return xSemaphoreTake(sdMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void storageUnlock() {
  xSemaphoreGive(sdMutex);
}

bool storageReady() {
  return sdOk;
}

// ---------------------------------------------------------------- helpers
static bool isImageName(const char *name) {
  if (!name || name[0] == '.') return false;
  const char *dot = strrchr(name, '.');
  if (!dot) return false;
  return strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0;
}

static String sanitizeName(String raw) {
  int slash = max(raw.lastIndexOf('/'), raw.lastIndexOf('\\'));
  if (slash >= 0) raw = raw.substring(slash + 1);
  String out;
  for (size_t i = 0; i < raw.length(); i++) {
    unsigned char c = raw[i];
    if (isalnum(c) || c == '.' || c == '-' || c == '_' || c == ' ' || c == '(' || c == ')') out += (char)c;
    else out += '_';
  }
  while (out.startsWith(".") || out.startsWith(" ")) out.remove(0, 1);
  out.trim();
  if (out.length() > 80) {
    int dot = out.lastIndexOf('.');
    String ext = dot > 0 ? out.substring(dot) : "";
    if (ext.length() > 8) ext = "";
    out = out.substring(0, 80 - ext.length()) + ext;
  }
  return out;
}

static void refreshAudioCache_l() {
  if (sd.exists("/music.wav")) audioCache = "/music.wav";
  else if (sd.exists("/music.mp3")) audioCache = "/music.mp3";
  else audioCache = "";
}

// Play order. With shuffle the order is random, otherwise alphabetical.
// If `keep` names a photo, the order is arranged so that photo stays current.
static void buildOrder_l(const String &keep, int fallbackPos) {
  size_t n = images.size();
  order.resize(n);
  std::iota(order.begin(), order.end(), 0);
  int keepIdx = -1;
  for (size_t i = 0; i < n; i++) {
    if (keep.length() && images[i].name == keep) {
      keepIdx = i;
      break;
    }
  }
  if (settings.shuffle) {
    for (size_t i = n; i > 1; i--) std::swap(order[i - 1], order[esp_random() % i]);
    pos = 0;
    if (keepIdx >= 0) {
      for (size_t i = 0; i < n; i++)
        if (order[i] == keepIdx) {
          std::swap(order[0], order[i]);
          break;
        }
    }
  } else {
    pos = keepIdx >= 0 ? keepIdx : constrain(fallbackPos, 0, (int)n - 1);
    if (n == 0) pos = 0;
  }
}

static void currentName_l(String &name) {
  name = "";
  if (!order.empty() && pos >= 0 && pos < (int)order.size()) name = images[order[pos]].name;
}

static bool scanDir_l(std::vector<ImageEntry> &found) {
  found.clear();
  sdRoot.rewind();
  SdBaseFile entry;
  char name[128];
  while (entry.openNext(&sdRoot, O_RDONLY)) {
    if (!entry.isSubDir()) {
      name[0] = 0;
      entry.getName(name, sizeof(name));
      if (isImageName(name)) found.push_back({String(name), (uint32_t)entry.fileSize()});
    }
    entry.close();
  }
  std::sort(found.begin(), found.end(), [](const ImageEntry &a, const ImageEntry &b) {
    return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
  });
  return true;
}

static void rescan_l() {
  String keep;
  currentName_l(keep);
  int oldPos = pos;
  scanDir_l(images);
  buildOrder_l(keep, oldPos);
  refreshAudioCache_l();
}

// ---------------------------------------------------------------- mount
bool storageBegin() {
  if (!sdMutex) sdMutex = xSemaphoreCreateMutex();
  storageLock(5000);
  sdRoot.close();
  sdOk = sd.begin(SdSpiConfig(SD_CS, SHARED_SPI, SD_SCK_MHZ(20), &sdSpi));
  if (!sdOk) sdOk = sd.begin(SdSpiConfig(SD_CS, SHARED_SPI, SD_SCK_MHZ(10), &sdSpi));
  if (sdOk) {
    sdRoot = sd.open("/");
    if (!sdRoot.isOpen()) sdOk = false;
  }
  if (sdOk) {
    rescan_l();
    Serial.printf("SD mounted, %u photos\n", (unsigned)images.size());
  } else {
    images.clear();
    order.clear();
    Serial.println("SD mount failed");
  }
  storageUnlock();
  return sdOk;
}

// ---------------------------------------------------------------- playlist
uint16_t playlistCount() {
  return images.size();
}

int playlistPosition() {
  return images.empty() ? 0 : pos + 1;
}

bool playlistCurrent(String &name) {
  if (!storageLock(2000)) return false;
  currentName_l(name);
  storageUnlock();
  return name.length() > 0;
}

bool playlistStep(int delta, String &name) {
  if (!storageLock(2000)) return false;
  int n = order.size();
  if (n > 0) {
    int last = order[pos];
    pos += delta;
    if (pos >= n) {
      pos = 0;
      if (settings.shuffle) {
        for (int i = n; i > 1; i--) std::swap(order[i - 1], order[esp_random() % i]);
        if (n > 1 && order[0] == last) std::swap(order[0], order[1]);
      }
    } else if (pos < 0) {
      pos = n - 1;
    }
  }
  currentName_l(name);
  storageUnlock();
  return name.length() > 0;
}

bool playlistSelect(const String &name, String &out) {
  if (!storageLock(2000)) return false;
  bool found = false;
  for (size_t i = 0; i < order.size() && !found; i++) {
    if (images[order[i]].name == name) {
      pos = i;
      found = true;
    }
  }
  if (found) currentName_l(out);
  storageUnlock();
  return found;
}

bool playlistHas(const String &name, uint32_t *size) {
  if (!storageLock(2000)) return false;
  bool found = false;
  for (const auto &e : images) {
    if (e.name == name) {
      found = true;
      if (size) *size = e.size;
      break;
    }
  }
  storageUnlock();
  return found;
}

void playlistRescan() {
  if (!storageLock(5000)) return;
  if (sdOk) rescan_l();
  storageUnlock();
}

static void appendJsonString(String &out, const String &s) {
  out += '"';
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if ((unsigned char)c < 0x20) {
      out += ' ';
    } else {
      out += c;
    }
  }
  out += '"';
}

String playlistJson() {
  String out = "[";
  if (storageLock(2000)) {
    bool first = true;
    for (const auto &e : images) {
      if (!first) out += ',';
      first = false;
      out += "{\"n\":";
      appendJsonString(out, e.name);
      out += ",\"s\":";
      out += e.size;
      out += '}';
    }
    storageUnlock();
  }
  out += ']';
  return out;
}

// ---------------------------------------------------------------- serving files to the web server
static SdBaseFile serveFile;
static String serveName;
static bool serveOpen = false;

static void closeServe_l() {
  if (serveOpen) serveFile.close();
  serveOpen = false;
  serveName = "";
}

size_t storageServeRead(const String &name, size_t offset, uint8_t *buf, size_t len) {
  if (!storageLock(0)) return RESPONSE_TRY_AGAIN;
  size_t got = 0;
  if (sdOk) {
    if (!serveOpen || serveName != name) {
      closeServe_l();
      String path = "/" + name;
      if (serveFile.open(path.c_str(), O_RDONLY)) {
        serveOpen = true;
        serveName = name;
      }
    }
    if (serveOpen) {
      if (serveFile.curPosition() != offset) serveFile.seekSet(offset);
      int n = serveFile.read(buf, len);
      got = n > 0 ? n : 0;
      if (got == 0 || offset + got >= serveFile.fileSize()) closeServe_l();
    }
  }
  storageUnlock();
  return got;
}

// ---------------------------------------------------------------- delete
bool storageDeleteImage(const String &name) {
  if (!storageLock(4000)) return false;
  bool ok = false;
  for (size_t i = 0; i < images.size(); i++) {
    if (images[i].name != name) continue;
    closeServe_l();
    String path = "/" + name;
    if (sd.remove(path.c_str())) {
      String keep;
      currentName_l(keep);
      int oldPos = pos;
      images.erase(images.begin() + i);
      buildOrder_l(keep, oldPos);
      ok = true;
    }
    break;
  }
  storageUnlock();
  return ok;
}

bool storageDeleteAudio() {
  if (!storageLock(4000)) return false;
  bool ok = false;
  if (sdOk) {
    closeServe_l();
    sd.remove("/music.wav");
    sd.remove("/music.mp3");
    refreshAudioCache_l();
    ok = audioCache.length() == 0;
  }
  storageUnlock();
  return ok;
}

String storageAudioFile() {
  return audioCache;
}

// ---------------------------------------------------------------- upload
static struct {
  SdBaseFile f;
  bool open = false;
  String dest;
  UploadKind kind = UPLOAD_NONE;
} up;

bool uploadStart(const String &rawName, String &err) {
  String name = sanitizeName(rawName);
  String lower = name;
  lower.toLowerCase();
  if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) {
    up.kind = UPLOAD_IMAGE;
    up.dest = "/" + name;
  } else if (lower.endsWith(".wav")) {
    up.kind = UPLOAD_AUDIO;
    up.dest = "/music.wav";
  } else if (lower.endsWith(".mp3")) {
    up.kind = UPLOAD_AUDIO;
    up.dest = "/music.mp3";
  } else {
    err = "Unsupported file type (use JPG, WAV or MP3)";
    return false;
  }
  if (name.length() < 5 && up.kind == UPLOAD_IMAGE) {
    err = "File name too short";
    return false;
  }
  if (!storageLock(4000)) {
    err = "SD card busy";
    return false;
  }
  bool ok = false;
  if (!sdOk) {
    err = "No SD card";
  } else {
    closeServe_l();
    if (up.open) up.f.close();
    up.open = up.f.open(UPLOAD_TMP, O_WRITE | O_CREAT | O_TRUNC);
    ok = up.open;
    if (!ok) err = "Could not create file on SD card";
  }
  storageUnlock();
  return ok;
}

bool uploadWrite(const uint8_t *data, size_t len) {
  if (!storageLock(4000)) return false;
  bool ok = up.open && up.f.write(data, len) == (int)len;
  storageUnlock();
  return ok;
}

bool uploadFinish(String &finalName, UploadKind &kind, String &err) {
  if (!storageLock(4000)) {
    err = "SD card busy";
    return false;
  }
  bool ok = false;
  if (!up.open) {
    err = "Upload was not started";
  } else {
    up.f.close();
    up.open = false;
    if (sd.exists(up.dest.c_str())) sd.remove(up.dest.c_str());
    if (up.kind == UPLOAD_AUDIO) {
      // only one sound file at a time
      sd.remove(up.dest.endsWith(".wav") ? "/music.mp3" : "/music.wav");
    }
    if (sd.rename(UPLOAD_TMP, up.dest.c_str())) {
      ok = true;
      kind = up.kind;
      finalName = up.dest.substring(1);
      rescan_l();
    } else {
      err = "Could not save file";
      sd.remove(UPLOAD_TMP);
    }
  }
  storageUnlock();
  return ok;
}

void uploadAbort() {
  if (!storageLock(2000)) return;
  if (up.open) {
    up.f.close();
    up.open = false;
    sd.remove(UPLOAD_TMP);
  }
  storageUnlock();
}
