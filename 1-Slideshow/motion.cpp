// Wi-Fi motion sensing (CSI) for sleep/wake. No extra hardware: the frame pings the router
// ~20 times a second and watches how the Wi-Fi channel response (CSI) of the replies and
// beacons fluctuates. A person moving in the room disturbs it. Same family of idea as
// ESPectre (espectre.dev): per packet, how far the subcarrier amplitude pattern is from its
// running average; averaged over a ~1 s window and compared with a slowly learned quiet-room level.
#include <WiFi.h>
#include <math.h>
#include <string.h>
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "app.h"

#define WIN 40            // samples in the variance window
#define EVAL_MS 250
#define PING_MS 25
#define HITS_ON 4         // consecutive over-threshold evaluations to start motion
#define HITS_OFF 3        // consecutive quiet evaluations to end it

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static float ring[WIN];
static volatile uint32_t ringCount = 0;   // total samples ever written
static uint8_t apMac[6];
static volatile bool macValid = false;

static volatile bool s_motion = false;
static volatile bool s_ready = false;
static volatile float s_score = 0;

bool motionDetected() { return s_motion; }
static volatile uint32_t s_readyAt = 0;
// True while sensing works; a short gap in the packets does not count as a failure.
bool motionReady() { return s_readyAt && millis() - s_readyAt < 15000; }
float motionScore() { return s_score; }

static void csiCb(void *, wifi_csi_info_t *info) {
  if (!macValid || !info || info->len < 16 || memcmp(info->mac, apMac, 6) != 0) return;
  if (info->rx_ctrl.sig_mode != 1) return;  // only HT frames (our ping replies): one frame type keeps the baseline steady
  const int8_t *b = info->buf;
  int n = info->len / 2;  // [imag, real] pairs
  if (n > 64) n = 64;
  float amp[64];
  float sum = 0;
  int used = 0;
  for (int i = 0; i < n; i++) {
    float v = sqrtf((float)b[2 * i] * b[2 * i] + (float)b[2 * i + 1] * b[2 * i + 1]);
    amp[i] = v;
    if (v >= 1) { sum += v; used++; }  // null / guard subcarriers read ~0
  }
  if (used < 8) return;
  float mean = sum / used;
  // How far this packet's (gain-normalised) amplitude pattern is from its running average.
  // Still room: small, steady. Someone moving changes the multipath, so the pattern shifts.
  static float ema[64];
  static bool haveEma = false;
  float d = 0;
  int cnt = 0;
  for (int i = 0; i < n; i++) {
    if (amp[i] < 1) continue;
    float v = amp[i] / mean;
    if (!haveEma) ema[i] = v;
    d += fabsf(v - ema[i]);
    ema[i] += 0.05f * (v - ema[i]);
    cnt++;
  }
  haveEma = true;
  float turb = d / cnt;
  portENTER_CRITICAL(&mux);
  ring[ringCount % WIN] = turb;
  ringCount++;
  portEXIT_CRITICAL(&mux);
}

// ---- ICMP echo to the router, to make it answer (CSI needs frames from the AP)
static uint16_t icmpSum(const uint8_t *p, int len) {
  uint32_t s = 0;
  for (int i = 0; i + 1 < len; i += 2) s += (p[i] << 8) | p[i + 1];
  while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
  return ~s;
}

static int openIcmp() {
  int s = lwip_socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (s >= 0) lwip_fcntl(s, F_SETFL, lwip_fcntl(s, F_GETFL, 0) | O_NONBLOCK);
  return s;
}

static void motionTask(void *) {
  int sock = -1;
  bool csiOn = false;
  uint16_t seq = 0;
  uint32_t lastPing = 0, lastEval = 0, lastCount = 0, readySince = 0, lastFresh = 0;
  float baseline = 0;
  int hitsOn = 0, hitsOff = 0;
  for (;;) {
    uint32_t now = millis();
    bool want = settings.motionEnabled && WiFi.status() == WL_CONNECTED;
    if (want != csiOn) {
      if (want) {
        const uint8_t *bssid = WiFi.BSSID();
        if (bssid) {
          memcpy(apMac, bssid, 6);
          macValid = true;
          wifi_csi_config_t cfg = {};
          cfg.lltf_en = true;
          cfg.htltf_en = false;
          cfg.stbc_htltf2_en = false;
          cfg.ltf_merge_en = true;
          cfg.channel_filter_en = false;
          cfg.manu_scale = false;
          esp_wifi_set_csi_config(&cfg);
          esp_wifi_set_csi_rx_cb(csiCb, nullptr);
          if (esp_wifi_set_csi(true) == ESP_OK) {
            csiOn = true;
            WiFi.setSleep(false);  // modem sleep delays replies
            Serial.println("Motion: CSI on");
          } else {
            Serial.println("Motion: CSI not available in this build");
          }
        }
      } else {
        esp_wifi_set_csi(false);
        csiOn = false;
        s_ready = false;
        s_readyAt = 0;
        s_motion = false;
        baseline = 0;
        hitsOn = hitsOff = 0;
        if (sock >= 0) { lwip_close(sock); sock = -1; }
      }
    }
    if (!csiOn) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    if (now - lastPing >= PING_MS) {
      lastPing = now;
      if (sock < 0) sock = openIcmp();
      if (sock >= 0) {
        uint8_t pkt[40] = {0};
        pkt[0] = 8;  // echo request
        pkt[4] = 0x50; pkt[5] = 0x46;
        pkt[6] = seq >> 8; pkt[7] = seq & 0xFF;
        seq++;
        uint16_t c = icmpSum(pkt, sizeof(pkt));
        pkt[2] = c >> 8; pkt[3] = c & 0xFF;
        struct sockaddr_in to = {};
        to.sin_len = sizeof(to);
        to.sin_family = AF_INET;
        to.sin_addr.s_addr = (uint32_t)WiFi.gatewayIP();
        lwip_sendto(sock, pkt, sizeof(pkt), 0, (struct sockaddr *)&to, sizeof(to));
        uint8_t rx[96];
        while (lwip_recv(sock, rx, sizeof(rx), 0) > 0) {}  // drain replies
      }
    }

    if (now - lastEval >= EVAL_MS) {
      lastEval = now;
      float w[WIN];
      uint32_t cnt;
      portENTER_CRITICAL(&mux);
      memcpy(w, ring, sizeof(w));
      cnt = ringCount;
      portEXIT_CRITICAL(&mux);
      uint32_t fresh = cnt - lastCount;  // samples since the last evaluation
      lastCount = cnt;
      if (fresh) lastFresh = now;
      if (cnt >= WIN && now - lastFresh < 1500) {  // tolerate short gaps in the packet flow
        if (!readySince) readySince = now;
      } else {
        readySince = 0;
        s_ready = false;
      }
      if (readySince && now - readySince > 2000) s_ready = true;
      if (s_ready) s_readyAt = now;
      if (cnt >= WIN) {
        float m = 0;
        for (int i = 0; i < WIN; i++) m += w[i];
        m /= WIN;
        if (baseline <= 0 && s_ready) baseline = m > 1e-4f ? m : 1e-4f;
        float sens = constrain((int)settings.motionSens, 1, 10);
        float k = 0.9f - 0.08f * sens;  // rise over the quiet level that counts as motion: sens 5 -> +50%, 10 -> +10%
        float score = baseline > 0 ? (m / baseline - 1.0f) / k : 0;  // >= 1 means motion
        s_score = score;
        if (s_ready) {
          if (score >= 1) { hitsOn++; hitsOff = 0; }
          else { hitsOff++; hitsOn = 0; }
          if (!s_motion && hitsOn >= HITS_ON) s_motion = true;
          if (s_motion && hitsOff >= HITS_OFF) s_motion = false;
          if (!s_motion) baseline += (m - baseline) * 0.02f;  // learn the quiet level
          if (baseline < 1e-4f) baseline = 1e-4f;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void motionBegin() {
  xTaskCreatePinnedToCore(motionTask, "motion", 4096, nullptr, 1, nullptr, 0);
}
