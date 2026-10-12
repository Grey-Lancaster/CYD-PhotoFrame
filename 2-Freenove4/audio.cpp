// Sound playback (music.wav or music.mp3 from the SD card) through the board's
// ES8311 codec and speaker amplifier.
//
// WAV/MP3 decoding is ESP8266Audio's. The output stage is local: the I2S bus runs
// as master with MCLK on its own pin (the codec needs it), and the codec is
// configured over the shared I2C bus for the file's own sample rate.

#include <SD_MMC.h>
#include <atomic>
#include <driver/i2s.h>
#include <driver/i2c.h>
#include "AudioFileSource.h"
#include "AudioGeneratorWAV.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutput.h"
#include "AudioLogger.h"
#include "es8311.h"
#include "app.h"

#define AUDIO_I2S_PORT I2S_NUM_0
#define CODEC_VOLUME 85  // ES8311 DAC volume, 0-100 (user volume is applied as software gain)

// ---------------------------------------------------------------- SD source
// Reads the file in 4 KB blocks, so the SD card (and its mutex) is touched a few
// times per second instead of for every few samples.
class AudioFileSourceSdMmc : public AudioFileSource {
 public:
  bool open(const char *filename) override {
    if (!storageLock(3000)) return false;
    file = SD_MMC.open(filename, FILE_READ);
    opened = (bool)file;
    if (opened) size = file.size();
    storageUnlock();
    pos = 0;
    bufStart = bufLen = 0;
    return opened;
  }

  uint32_t read(void *data, uint32_t len) override {
    if (!opened) return 0;
    uint8_t *out = (uint8_t *)data;
    uint32_t total = 0;
    while (len > 0 && pos < size) {
      if (pos >= bufStart && pos < bufStart + bufLen) {
        uint32_t n = min(len, bufStart + bufLen - pos);
        memcpy(out, buf + (pos - bufStart), n);
        out += n;
        pos += n;
        len -= n;
        total += n;
      } else if (!fill()) {
        break;
      }
    }
    return total;
  }

  bool seek(int32_t offset, int dir) override {
    if (!opened) return false;
    int64_t target = dir == SEEK_SET ? offset : dir == SEEK_CUR ? (int64_t)pos + offset : (int64_t)size + offset;
    if (target < 0 || target > (int64_t)size) return false;
    pos = target;
    return true;
  }

  bool close() override {
    if (opened) {
      storageLock(1000);
      file.close();
      storageUnlock();
      opened = false;
    }
    return true;
  }
  bool isOpen() override { return opened; }
  uint32_t getSize() override { return size; }
  uint32_t getPos() override { return pos; }

 private:
  bool fill() {
    if (!storageLock(1000)) return false;
    bool ok = false;
    if ((uint32_t)file.position() == pos || file.seek(pos)) {
      int n = file.read(buf, sizeof(buf));
      if (n > 0) {
        bufStart = pos;
        bufLen = n;
        ok = true;
      }
    }
    storageUnlock();
    return ok;
  }

  File file;
  bool opened = false;
  uint32_t size = 0;
  uint32_t pos = 0;
  uint32_t bufStart = 0;
  uint32_t bufLen = 0;
  uint8_t buf[4096];
};

// ---------------------------------------------------------------- codec output
static es8311_handle_t codec = nullptr;

static bool codecConfigure(int rate) {
  if (!i2cLock(1000)) return false;
  if (!codec) codec = es8311_create(I2C_NUM_0, ES8311_ADDRESS_0);
  bool ok = false;
  if (codec) {
    es8311_clock_config_t clk;
    memset(&clk, 0, sizeof(clk));
    clk.mclk_from_mclk_pin = true;
    clk.mclk_frequency = rate * 256;
    clk.sample_frequency = rate;
    esp_err_t e = es8311_init(codec, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
    if (e == ESP_OK) e = es8311_voice_volume_set(codec, CODEC_VOLUME, nullptr);
    if (e == ESP_OK) e = es8311_voice_mute(codec, false);
    ok = e == ESP_OK;
    if (!ok) Serial.printf("ES8311 set-up failed for %d Hz (err %d)\n", rate, (int)e);
  }
  i2cUnlock();
  return ok;
}

class AudioOutputEs8311 : public AudioOutput {
 public:
  // The base class leaves hertz/bps/channels uninitialised. The MP3 decoder calls
  // begin() before it knows the sample rate, so start from sane defaults.
  AudioOutputEs8311() {
    hertz = 44100;
    bps = 16;
    channels = 2;
  }
  ~AudioOutputEs8311() override { stop(); }

  bool begin() override {
    pinMode(AUDIO_AMP_EN, OUTPUT);
    digitalWrite(AUDIO_AMP_EN, LOW);  // as in Freenove's samples
    if (!installI2s(hertz)) return false;
    bool cok = codecConfigure(hertz);
    Serial.printf("[audio] begin: %d Hz, %d ch, %d bit, codec %s\n", (int)hertz, (int)channels, (int)bps, cok ? "ok" : "FAILED");
    running = true;
    return true;
  }

  bool SetChannels(int c) override {
    Serial.printf("[audio] SetChannels %d\n", c);
    channels = c;
    return true;
  }

  bool SetRate(int hz) override {
    bool changed = hz != hertz;
    Serial.printf("[audio] SetRate %d (was %d)\n", hz, (int)hertz);
    hertz = hz;
    if (running && changed) {
      // MCLK is fixed at rate*256 when the driver is installed, so a new rate needs a reinstall
      flush();
      i2s_driver_uninstall(AUDIO_I2S_PORT);
      running = installI2s(hz);
      if (running) codecConfigure(hz);
    }
    return true;
  }

  bool ConsumeSample(int16_t sample[2]) override {
    int16_t s[2] = {sample[0], sample[1]};
    MakeSampleStereo16(s);
    buf[fill++] = Amplify(s[0]);
    buf[fill++] = Amplify(s[1]);
    consumed++;
    int16_t a = abs(buf[fill - 2]);
    if (a > peak) peak = a;
    if (fill >= BUF_SAMPLES) flush();
    return true;
  }

  void flush() override {
    if (!running || fill == 0) return;
    size_t written = 0;
    i2s_write(AUDIO_I2S_PORT, buf, fill * sizeof(int16_t), &written, portMAX_DELAY);
    fill = 0;
  }

  bool stop() override {
    if (!running) return true;
    Serial.printf("[audio] stop: %u samples, peak %d, gain %d/64\n", (unsigned)consumed, (int)peak, (int)gainF2P6);
    flush();
    i2s_zero_dma_buffer(AUDIO_I2S_PORT);
    i2s_driver_uninstall(AUDIO_I2S_PORT);
    running = false;
    return true;
  }

 private:
  static const int BUF_SAMPLES = 512;  // interleaved L/R

  bool installI2s(int rate) {
    i2s_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = rate;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 8;
    cfg.dma_buf_len = 256;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = true;
    cfg.fixed_mclk = rate * 256;
    if (i2s_driver_install(AUDIO_I2S_PORT, &cfg, 0, nullptr) != ESP_OK) {
      Serial.println("I2S driver install failed");
      return false;
    }
    i2s_pin_config_t pins;
    memset(&pins, 0, sizeof(pins));
    pins.mck_io_num = AUDIO_MCLK;
    pins.bck_io_num = AUDIO_BCLK;
    pins.ws_io_num = AUDIO_WS;
    pins.data_out_num = AUDIO_DOUT;
    pins.data_in_num = AUDIO_DIN;
    if (i2s_set_pin(AUDIO_I2S_PORT, &pins) != ESP_OK) {
      Serial.println("I2S pin set-up failed");
      i2s_driver_uninstall(AUDIO_I2S_PORT);
      return false;
    }
    return true;
  }

  int16_t buf[BUF_SAMPLES];
  int fill = 0;
  bool running = false;
  uint32_t consumed = 0;
  int16_t peak = 0;
};

// ---------------------------------------------------------------- playback task
static std::atomic<bool> busy{false};

bool audioBusy() {
  return busy.load();
}

static void audioTask(void *param) {
  audioLogger = &Serial;  // decoder diagnostics
  String path = storageAudioFile();
  AudioFileSourceSdMmc *src = new AudioFileSourceSdMmc();
  AudioOutputEs8311 *out = nullptr;
  AudioGenerator *gen = nullptr;

  if (path.length() && src->open(path.c_str())) {
    {
      uint8_t head[16] = {0};
      uint32_t n = src->read(head, sizeof(head));
      src->seek(0, SEEK_SET);
      Serial.printf("[audio] %s: %u bytes, first %u bytes:", path.c_str(), (unsigned)src->getSize(), (unsigned)n);
      for (uint32_t i = 0; i < n; i++) Serial.printf(" %02X", head[i]);
      Serial.printf("  \"%c%c%c%c\"\n", head[0] >= 32 && head[0] < 127 ? head[0] : '.', head[1] >= 32 && head[1] < 127 ? head[1] : '.',
                    head[2] >= 32 && head[2] < 127 ? head[2] : '.', head[3] >= 32 && head[3] < 127 ? head[3] : '.');
    }
    out = new AudioOutputEs8311();
    out->SetGain(settings.volume / 100.0f);
    if (path.endsWith(".mp3")) gen = new AudioGeneratorMP3();
    else gen = new AudioGeneratorWAV();
    if (gen->begin(src, out)) {
      Serial.printf("Playing %s\n", path.c_str());
      while (gen->isRunning()) {
        if (!gen->loop()) gen->stop();
        delay(1);
      }
      Serial.println("Playback finished");
    } else {
      Serial.println("Could not start playback");
    }
  }

  if (gen) {
    gen->stop();
    delete gen;
  }
  if (out) {
    out->stop();
    delete out;
  }
  src->close();
  delete src;
  busy = false;
  vTaskDelete(nullptr);
}

bool audioPlay() {
  if (storageAudioFile().length() == 0) return false;
  if (busy.exchange(true)) return false;
  if (xTaskCreatePinnedToCore(audioTask, "audio", 12288, nullptr, 2, nullptr, 1) != pdPASS) {
    busy = false;
    return false;
  }
  return true;
}
