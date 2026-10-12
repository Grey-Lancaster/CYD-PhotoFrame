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
  prefs.putBool("night", settings.nightEnabled);
  prefs.putUChar("nStart", settings.nightStart);
  prefs.putUChar("nEnd", settings.nightEnd);
  prefs.putString("tz", settings.tz);
  prefs.end();
}

bool settingsLoadTouchCal(uint16_t cal[5]) {
  prefs.begin("frame", true);
  size_t n = prefs.getBytes("tcal", cal, 5 * sizeof(uint16_t));
  prefs.end();
  return n == 5 * sizeof(uint16_t);
}

void settingsSaveTouchCal(const uint16_t cal[5]) {
  prefs.begin("frame", false);
  prefs.putBytes("tcal", cal, 5 * sizeof(uint16_t));
  prefs.end();
}

void settingsClearTouchCal() {
  prefs.begin("frame", false);
  prefs.remove("tcal");
  prefs.end();
}
