#include <Preferences.h>
#include "app.h"

Settings settings;

static Preferences prefs;

void settingsLoad() {
  prefs.begin("frame", true);
  settings.speedSec = constrain(prefs.getUShort("speed", settings.speedSec), 2, 3600);
  settings.shuffle = prefs.getBool("shuffle", settings.shuffle);
  settings.brightness = constrain(prefs.getUChar("bright", settings.brightness), 5, 100);
  settings.volume = constrain(prefs.getUChar("volume", settings.volume), 0, 100);
  settings.soundOnUpload = prefs.getBool("sndUp", settings.soundOnUpload);
  settings.swapRB = prefs.getBool("swapRB", settings.swapRB);
  settings.invert = prefs.getBool("invert", settings.invert);
  settings.rotation = prefs.getUChar("rot", settings.rotation) & 3;
  settings.nightEnabled = prefs.getBool("night", settings.nightEnabled);
  settings.nightStart = prefs.getUChar("nStart", settings.nightStart) % 24;
  settings.nightEnd = prefs.getUChar("nEnd", settings.nightEnd) % 24;
  String tz = prefs.getString("tz", settings.tz);
  prefs.end();
  if (tz.length() > 0 && tz.length() < sizeof(settings.tz)) tz.toCharArray(settings.tz, sizeof(settings.tz));
}

void settingsSave() {
  prefs.begin("frame", false);
  prefs.putUShort("speed", settings.speedSec);
  prefs.putBool("shuffle", settings.shuffle);
  prefs.putUChar("bright", settings.brightness);
  prefs.putUChar("volume", settings.volume);
  prefs.putBool("sndUp", settings.soundOnUpload);
  prefs.putBool("swapRB", settings.swapRB);
  prefs.putBool("invert", settings.invert);
  prefs.putUChar("rot", settings.rotation);
  prefs.putBool("night", settings.nightEnabled);
  prefs.putUChar("nStart", settings.nightStart);
  prefs.putUChar("nEnd", settings.nightEnd);
  prefs.putString("tz", settings.tz);
  prefs.end();
}

// The touch calibration depends on the screen rotation, so each rotation keeps its own.
// Rotation 0 uses the original key, so frames calibrated before rotation existed keep theirs.
static String calKey(uint8_t rotation) {
  return rotation ? String("tcal") + rotation : String("tcal");
}

bool settingsLoadTouchCal(uint8_t rotation, uint16_t cal[5]) {
  prefs.begin("frame", true);
  size_t n = prefs.getBytes(calKey(rotation).c_str(), cal, 5 * sizeof(uint16_t));
  prefs.end();
  return n == 5 * sizeof(uint16_t);
}

void settingsSaveTouchCal(uint8_t rotation, const uint16_t cal[5]) {
  prefs.begin("frame", false);
  prefs.putBytes(calKey(rotation).c_str(), cal, 5 * sizeof(uint16_t));
  prefs.end();
}

bool settingsLoadTouchAff(float m[6]) {
  prefs.begin("frame", true);
  size_t n = prefs.getBytes("taff", m, 6 * sizeof(float));
  prefs.end();
  return n == 6 * sizeof(float);
}

void settingsSaveTouchAff(const float m[6]) {
  prefs.begin("frame", false);
  prefs.putBytes("taff", m, 6 * sizeof(float));
  prefs.end();
}

void settingsClearTouchCal() {
  prefs.begin("frame", false);
  for (uint8_t r = 0; r < 4; r++) prefs.remove(calKey(r).c_str());
  prefs.remove("taff");
  prefs.end();
}
