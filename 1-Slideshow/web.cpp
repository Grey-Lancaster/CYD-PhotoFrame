// Web server: single-page UI plus a small JSON API.

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <time.h>
#include "SPIFFS.h"
#include "app.h"
#include "web_assets.h"

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

// ---------------------------------------------------------------- helpers
static String param(AsyncWebServerRequest *r, const char *key) {
  if (r->hasParam(key, true)) return r->getParam(key, true)->value();
  if (r->hasParam(key)) return r->getParam(key)->value();
  return String();
}

static void sendJson(AsyncWebServerRequest *r, int code, const String &body) {
  AsyncWebServerResponse *resp = r->beginResponse(code, "application/json", body);
  resp->addHeader("Cache-Control", "no-store");
  r->send(resp);
}

static void sendError(AsyncWebServerRequest *r, int code, const char *msg) {
  sendJson(r, code, String("{\"ok\":false,\"error\":\"") + msg + "\"}");
}

static String jsonString(const String &s) {
  String out = "\"";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if ((unsigned char)c >= 0x20) {
      out += c;
    }
  }
  return out + "\"";
}

static void sendPhoto(AsyncWebServerRequest *r, const String &name, bool cacheable) {
  uint32_t size = 0;
  if (name.length() == 0 || !playlistHas(name, &size)) {
    r->send(404, "text/plain", "Not found");
    return;
  }
  AsyncWebServerResponse *resp = r->beginResponse("image/jpeg", size, [name](uint8_t *buf, size_t maxLen, size_t index) -> size_t {
    return storageServeRead(name, index, buf, maxLen);
  });
  resp->addHeader("Cache-Control", cacheable ? "max-age=86400" : "no-store");
  r->send(resp);
}

static String statusJson() {
  String cur;
  playlistCurrent(cur);
  String time;
  struct tm ti;
  if (getLocalTime(&ti, 0)) {
    char buf[32];
    char day[8];
    strftime(day, sizeof(day), "%a", &ti);
    int h12 = ti.tm_hour % 12 == 0 ? 12 : ti.tm_hour % 12;
    snprintf(buf, sizeof(buf), "%s %d:%02d %s", day, h12, ti.tm_min, ti.tm_hour < 12 ? "AM" : "PM");
    time = buf;
  }
  String out = "{\"v\":\"" FW_VERSION "\",\"host\":\"" HOSTNAME "\",\"ip\":" + jsonString(g_ip);
  out += ",\"count\":" + String(playlistCount());
  out += ",\"pos\":" + String(playlistPosition());
  out += ",\"cur\":" + jsonString(cur);
  out += String(",\"paused\":") + (g_paused ? "true" : "false");
  out += String(",\"night\":") + (g_night ? "true" : "false");
  out += ",\"audio\":" + jsonString(storageAudioFile().substring(1));
  out += String(",\"audioBusy\":") + (audioBusy() ? "true" : "false");
  out += String(",\"sd\":") + (storageReady() ? "true" : "false");
  out += ",\"heap\":" + String(ESP.getFreeHeap());
  out += ",\"rssi\":" + String(WiFi.RSSI());
  out += ",\"up\":" + String((uint32_t)(millis() / 1000));
  out += ",\"reset\":" + jsonString(g_resetReason);
  out += ",\"time\":" + jsonString(time);
  out += ",\"s\":{";
  out += "\"speed\":" + String(settings.speedSec);
  out += ",\"bright\":" + String(settings.brightness);
  out += ",\"volume\":" + String(settings.volume);
  out += String(",\"shuffle\":") + (settings.shuffle ? "true" : "false");
  out += String(",\"swaprb\":") + (settings.swapRB ? "true" : "false");
  out += String(",\"invert\":") + (settings.invert ? "true" : "false");
#ifdef TOUCH_CS
  out += ",\"touchcal\":true";
#endif
  out += String(",\"sndup\":") + (settings.soundOnUpload ? "true" : "false");
  out += String(",\"night\":") + (settings.nightEnabled ? "true" : "false");
  out += ",\"nstart\":" + String(settings.nightStart);
  out += ",\"nend\":" + String(settings.nightEnd);
  out += ",\"tz\":" + jsonString(settings.tz);
  out += "}}";
  return out;
}

// ---------------------------------------------------------------- upload state
static AsyncWebServerRequest *upOwner = nullptr;
static String upErr;
static String upName;
static UploadKind upKind = UPLOAD_NONE;

static void onUploadData(AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (index == 0) {
    uploadAbort();  // drop any half-finished earlier upload
    upOwner = request;
    upErr = "";
    upName = "";
    upKind = UPLOAD_NONE;
    if (!uploadStart(filename, upErr)) return;
  } else if (upOwner != request) {
    return;
  }
  if (upErr.length()) return;
  if (len && !uploadWrite(data, len)) upErr = "Write to SD card failed";
  if (final) {
    if (upErr.length()) uploadAbort();
    else uploadFinish(upName, upKind, upErr);
  }
}

static void onUploadDone(AsyncWebServerRequest *request) {
  if (upOwner != request) {
    sendError(request, 400, "No file received");
    return;
  }
  upOwner = nullptr;
  if (upErr.length()) {
    uploadAbort();
    sendError(request, 400, upErr.c_str());
    return;
  }
  sendJson(request, 200, "{\"ok\":true,\"name\":" + jsonString(upName) + "}");
  if (upKind == UPLOAD_IMAGE) cmdSend(CMD_SHOW, upName.c_str());
  else cmdSend(CMD_RESCAN);
  if (settings.soundOnUpload) audioPlay();
  webNotifyList();
}

// ---------------------------------------------------------------- routes
static void setupRoutes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(200, "text/html", (const uint8_t *)INDEX_HTML, sizeof(INDEX_HTML) - 1);
  });
  server.on("/slideshow", HTTP_GET, [](AsyncWebServerRequest *r) { r->redirect("/"); });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *r) { sendJson(r, 200, statusJson()); });
  server.on("/api/images", HTTP_GET, [](AsyncWebServerRequest *r) { sendJson(r, 200, playlistJson()); });

  server.on("/img", HTTP_GET, [](AsyncWebServerRequest *r) { sendPhoto(r, param(r, "n"), true); });
  server.on("/current_image", HTTP_GET, [](AsyncWebServerRequest *r) {
    String cur;
    playlistCurrent(cur);
    sendPhoto(r, cur, false);
  });

  server.on("/api/cmd", HTTP_POST, [](AsyncWebServerRequest *r) {
    String c = param(r, "c");
    if (c == "next") cmdSend(CMD_NEXT);
    else if (c == "prev") cmdSend(CMD_PREV);
    else if (c == "info") cmdSend(CMD_INFO);
    else if (c == "pause") g_paused = true;
    else if (c == "resume") g_paused = false;
    else if (c == "show") {
      String n = param(r, "n");
      if (!playlistHas(n)) return sendError(r, 404, "No such photo");
      cmdSend(CMD_SHOW, n.c_str());
    } else {
      return sendError(r, 400, "Unknown command");
    }
    sendJson(r, 200, "{\"ok\":true}");
  });

  server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *r) {
    bool shuffleBefore = settings.shuffle;
    String tzBefore = settings.tz;
    String v;
    if ((v = param(r, "speed")).length()) settings.speedSec = constrain(v.toInt(), 2, 3600);
    if ((v = param(r, "bright")).length()) settings.brightness = constrain(v.toInt(), 5, 100);
    if ((v = param(r, "volume")).length()) settings.volume = constrain(v.toInt(), 0, 100);
    if ((v = param(r, "shuffle")).length()) settings.shuffle = v.toInt() != 0;
    if ((v = param(r, "sndup")).length()) settings.soundOnUpload = v.toInt() != 0;
    bool swapBefore = settings.swapRB;
    bool invertBefore = settings.invert;
    if ((v = param(r, "invert")).length()) settings.invert = v.toInt() != 0;
    if ((v = param(r, "swaprb")).length()) settings.swapRB = v.toInt() != 0;
    if ((v = param(r, "night")).length()) settings.nightEnabled = v.toInt() != 0;
    if ((v = param(r, "nstart")).length()) settings.nightStart = constrain(v.toInt(), 0, 23);
    if ((v = param(r, "nend")).length()) settings.nightEnd = constrain(v.toInt(), 0, 23);
    if ((v = param(r, "tz")).length() && v.length() < sizeof(settings.tz)) v.toCharArray(settings.tz, sizeof(settings.tz));
    settingsSave();
    if (!g_night) displaySetBrightness(settings.brightness);
    if (tzBefore != settings.tz) applyTimezone();
    if (invertBefore != settings.invert) displayApplyInversion();
    if (swapBefore != settings.swapRB) {
      displayApplyColorOrder();
      String cur;
      if (playlistCurrent(cur)) cmdSend(CMD_SHOW, cur.c_str());  // redraw with the new colour order
    }
    if (shuffleBefore != settings.shuffle) playlistRescan();
    sendJson(r, 200, "{\"ok\":true}");
  });

  server.on("/api/delete", HTTP_POST, [](AsyncWebServerRequest *r) {
    bool ok = true;
    bool any = false;
    for (size_t i = 0; i < r->params(); i++) {
      const AsyncWebParameter *p = r->getParam(i);
      if (!p->isPost() || p->name() != "n") continue;
      any = true;
      if (p->value() == "music.wav" || p->value() == "music.mp3") ok &= storageDeleteAudio();
      else ok &= storageDeleteImage(p->value());
    }
    if (!any) return sendError(r, 400, "Nothing to delete");
    cmdSend(CMD_RESCAN);
    webNotifyList();
    if (ok) sendJson(r, 200, "{\"ok\":true}");
    else sendError(r, 500, "Some files could not be deleted");
  });

#ifdef TOUCH_CS
  server.on("/api/recal", HTTP_POST, [](AsyncWebServerRequest *r) {
    settingsClearTouchCal();  // the frame asks for the four corners again after the restart
    g_restartAt = millis() + 600;
    sendJson(r, 200, "{\"ok\":true}");
  });
#endif

  server.on("/api/play", HTTP_POST, [](AsyncWebServerRequest *r) {
    if (audioPlay()) sendJson(r, 200, "{\"ok\":true}");
    else sendError(r, 409, audioBusy() ? "Already playing" : "No sound file on the SD card");
  });

  server.on("/api/upload", HTTP_POST, onUploadDone, onUploadData);

  ws.onEvent([](AsyncWebSocket *s, AsyncWebSocketClient *c, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) c->text("update");
  });
  server.addHandler(&ws);

  server.serveStatic("/favicon.ico", SPIFFS, "/favicon.ico").setCacheControl("max-age=86400");
  server.onNotFound([](AsyncWebServerRequest *r) { r->send(404, "text/plain", "Not found"); });
}

void webBegin() {
  setupRoutes();

  ElegantOTA.begin(&server);
  ElegantOTA.onStart([]() {
    g_otaActive = true;
    displayShowOta("Receiving firmware...", 0);
  });
  ElegantOTA.onProgress([](size_t current, size_t total) {
    static int last = -1;
    int pct = total ? (int)(current * 100 / total) : 0;
    if (pct != last && pct % 5 == 0) {
      last = pct;
      displayShowOta(nullptr, pct);
    }
  });
  ElegantOTA.onEnd([](bool success) {
    displayShowOta(success ? "Done - restarting" : "Update failed", success ? 100 : -1);
    if (!success) {
      delay(2000);
      g_otaActive = false;
    }
  });

  server.begin();
  Serial.println("Web server started");
}

void webNotifyPhoto() {
  ws.textAll("update");
}

void webNotifyList() {
  ws.textAll("list");
}

void webLoop() {
  ElegantOTA.loop();
  ws.cleanupClients();
}
