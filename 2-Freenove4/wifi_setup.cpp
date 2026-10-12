// On-device WiFi setup: TouchWifiProvisioner (LVGL picker + keyboard) bridged onto
// the TFT_eSPI display and the FT6336U touch controller. LVGL is only used here,
// before the app is online; the rest of the frame draws straight through TFT_eSPI.

#include <TouchWifiProvisioner.h>
#include <lvgl.h>
#include "app.h"

static lv_disp_draw_buf_t drawBuf;
static lv_color_t buf1[480 * 20];

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
  int x, y;
  if (touchRead(x, y)) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

static std::function<void(const String &)> connectedCb;
static bool firedConnected = false;

void wifiSetupBegin(const char *hostname, std::function<void(const String &ip)> onConnected) {
  connectedCb = onConnected;
  firedConnected = false;

  lv_init();
  lv_disp_draw_buf_init(&drawBuf, buf1, nullptr, 480 * 20);
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
