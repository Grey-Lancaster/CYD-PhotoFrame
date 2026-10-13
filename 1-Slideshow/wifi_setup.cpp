// On-device WiFi setup: TouchWifiProvisioner (LVGL picker + keyboard) bridged onto
// the TFT_eSPI display and the CYD's resistive touch. LVGL is only used here, before
// the frame is online; the rest of the firmware draws straight through TFT_eSPI.

#include <WiFi.h>
#include <Preferences.h>
#include <TouchWifiProvisioner.h>
#include <lvgl.h>
#include "app.h"

// 480 is the widest panel (3.5" board); 10 rows keep the static buffer small enough for
// the classic ESP32 (the picker is not animation heavy).
static lv_disp_draw_buf_t drawBuf;
static lv_color_t buf1[480 * 10];

static void dispFlush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorP) {
  const uint32_t w = area->x2 - area->x1 + 1;
  const uint32_t h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&colorP->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

static void touchpadRead(lv_indev_drv_t *, lv_indev_data_t *data) {
  static bool wasDown = false;
  int x, y;
  if (touchReadXY(x, y)) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
    if (!wasDown) Serial.printf("Picker touch -> %d,%d\n", x, y);
    wasDown = true;
  } else {
    data->state = LV_INDEV_STATE_REL;
    wasDown = false;
  }
}

static std::function<void(const String &)> connectedCb;
static bool firedConnected = false;

// Frames updated from 4.2 or older keep their WiFi login in the ESP32's own WiFi storage
// (left there by WiFiManager). Copy it into the picker's list once, so updating over the
// air does not make everyone retype the password. A flag keeps this from running again
// after the user later forgets the network.
static void importLegacyWifi() {
  Preferences mig;
  mig.begin("pfmig", false);
  bool done = mig.getBool("wifi", false);
  if (!done) mig.putBool("wifi", true);
  mig.end();
  if (done || !TouchWifiProvisioner::savedNetworks().empty()) return;

  String ssid = WiFi.SSID();
  if (ssid.isEmpty()) return;
  Preferences p;
  p.begin("wifiprov", false);
  p.putString("ssid", ssid);  // the library's single-network layout, which it reads itself
  p.putString("password", WiFi.psk());
  p.end();
  Serial.println("WiFi: kept the saved network \"" + ssid + "\" from the previous firmware");
}

void wifiSetupBegin(const char *hostname, std::function<void(const String &ip)> onConnected) {
  connectedCb = onConnected;
  firedConnected = false;

  importLegacyWifi();

  lv_init();
  lv_disp_draw_buf_init(&drawBuf, buf1, nullptr, 480 * 10);
  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = tft.width();
  dispDrv.ver_res = tft.height();
  dispDrv.flush_cb = dispFlush;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

  static lv_indev_drv_t indevDrv;
  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchpadRead;
  lv_indev_drv_register(&indevDrv);

  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);

  TouchWifiProvisioner::begin(lv_scr_act(), hostname, [](const String &ip) {
    // also called again after later reconnects; only the first call ends setup
    if (firedConnected) return;
    firedConnected = true;
    if (connectedCb) connectedCb(ip);
  });
}

void wifiSetupLoop() {
  lv_timer_handler();
  TouchWifiProvisioner::loop();
}

void wifiSetupReset() {
  TouchWifiProvisioner::reset();
}

bool wifiHasSavedNetworks() {
  return !TouchWifiProvisioner::savedNetworks().empty();
}
