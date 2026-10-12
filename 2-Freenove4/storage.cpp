// SD card access (4-bit SDMMC), photo list / playlist, uploads and deletes.
//
// Everything that touches the card or the photo list goes through sdMutex, so the
// slideshow loop and the async web server never use the card at the same time.

#include <SD_MMC.h>
#include <algorithm>
#include <numeric>
#include <vector>
#include "app.h"

#ifndef RESPONSE_TRY_AGAIN
#define RESPONSE_TRY_AGAIN 0xFFFFFFFF
#endif

#define UPLOAD_TMP "/_upload.tmp"

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
  if (SD_MMC.exists("/music.wav")) audioCache = "/music.wav";
  else if (SD_MMC.exists("/music.mp3")) audioCache = "/music.mp3";
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
  File root = SD_MMC.open("/");
  if (!root) return false;
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    if (!f.isDirectory() && isImageName(f.name())) found.push_back({String(f.name()), (uint32_t)f.size()});
    f.close();
  }
  root.close();
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
  if (sdOk) SD_MMC.end();
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
  sdOk = SD_MMC.begin("/sdcard", false, false, SD_FREQ_KHZ, 5) && SD_MMC.cardType() != CARD_NONE;
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
static File serveFile;
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
      serveFile = SD_MMC.open(path.c_str(), FILE_READ);
      if (serveFile) {
        serveOpen = true;
        serveName = name;
      }
    }
    if (serveOpen) {
      if ((size_t)serveFile.position() != offset) serveFile.seek(offset);
      int n = serveFile.read(buf, len);
      got = n > 0 ? n : 0;
      if (got == 0 || offset + got >= (size_t)serveFile.size()) closeServe_l();
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
    if (SD_MMC.remove(path.c_str())) {
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
    SD_MMC.remove("/music.wav");
    SD_MMC.remove("/music.mp3");
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
// Identify a sound file by its content, not its name. Returns "/music.wav" or
// "/music.mp3", or an empty string with `why` set. Messages avoid quotes: they go
// into a JSON error reply.
static String sniffAudio_l(const char *path, String &why) {
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) {
    why = "Could not re-read the uploaded file";
    return "";
  }
  uint8_t h[12] = {0};
  size_t n = f.read(h, sizeof(h));
  f.close();
  if (n < sizeof(h)) {
    why = "File is too small to be a sound file";
    return "";
  }
  if (!memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WAVE", 4)) return "/music.wav";
  if (!memcmp(h, "ID3", 3) || (h[0] == 0xFF && (h[1] & 0xE0) == 0xE0)) return "/music.mp3";
  if (!memcmp(h + 4, "ftyp", 4)) why = "This is an M4A/MP4 file (AAC audio) with the wrong extension, not a real MP3. Convert it to MP3 or WAV first";
  else if (!memcmp(h, "OggS", 4)) why = "This is an Ogg audio file, not an MP3 or WAV. Convert it to MP3 or WAV first";
  else if (!memcmp(h, "fLaC", 4)) why = "This is a FLAC file, not an MP3 or WAV. Convert it to MP3 or WAV first";
  else why = "Not a valid MP3 or WAV file (unrecognized format). Convert it to MP3 or WAV first";
  return "";
}

static struct {
  File f;
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
    up.f = SD_MMC.open(UPLOAD_TMP, FILE_WRITE);  // creates or truncates
    up.open = (bool)up.f;
    ok = up.open;
    if (!ok) err = "Could not create file on SD card";
  }
  storageUnlock();
  return ok;
}

bool uploadWrite(const uint8_t *data, size_t len) {
  if (!storageLock(4000)) return false;
  bool ok = up.open && up.f.write(data, len) == len;
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
    if (up.kind == UPLOAD_AUDIO) {
      String real = sniffAudio_l(UPLOAD_TMP, err);
      if (real.length() == 0) {
        SD_MMC.remove(UPLOAD_TMP);
        storageUnlock();
        return false;
      }
      up.dest = real;  // the content decides, so a WAV named .mp3 is stored as music.wav
    }
    if (SD_MMC.exists(up.dest.c_str())) SD_MMC.remove(up.dest.c_str());
    if (up.kind == UPLOAD_AUDIO) {
      // only one sound file at a time
      SD_MMC.remove(up.dest.endsWith(".wav") ? "/music.mp3" : "/music.wav");
    }
    if (SD_MMC.rename(UPLOAD_TMP, up.dest.c_str())) {
      ok = true;
      kind = up.kind;
      finalName = up.dest.substring(1);
      rescan_l();
    } else {
      err = "Could not save file";
      SD_MMC.remove(UPLOAD_TMP);
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
    SD_MMC.remove(UPLOAD_TMP);
  }
  storageUnlock();
}
