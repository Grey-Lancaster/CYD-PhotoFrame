// Sound playback (music.wav or music.mp3 from the SD card) through the
// internal DAC. The file is read through SdFat under the shared SD mutex, so
// no remounting of the card is needed.
//
// The CYD speaker is on GPIO26 (DAC2). GPIO25 (DAC1) is the touch controller's
// clock line, so only DAC2 is enabled (see audioTask).

#include <SdFat.h>
#include <atomic>
#include <driver/i2s.h>
#include "AudioFileSource.h"
#include "AudioGeneratorWAV.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include "app.h"

// Reads the file in 4 KB blocks, so the SD card (and its mutex) is touched
// a few times per second instead of for every few samples.
class AudioFileSourceSdFat : public AudioFileSource {
 public:
  bool open(const char *filename) override {
    if (!storageLock(3000)) return false;
    opened = file.open(filename, O_RDONLY);
    if (opened) size = file.fileSize();
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
    if (file.curPosition() == pos || file.seekSet(pos)) {
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

  SdBaseFile file;
  bool opened = false;
  uint32_t size = 0;
  uint32_t pos = 0;
  uint32_t bufStart = 0;
  uint32_t bufLen = 0;
  uint8_t buf[4096];
};

static std::atomic<bool> busy{false};

bool audioBusy() {
  return busy.load();
}

static void audioTask(void *param) {
  String path = storageAudioFile();
  AudioFileSourceSdFat *src = new AudioFileSourceSdFat();
  AudioOutputI2S *out = nullptr;
  AudioGenerator *gen = nullptr;

  if (path.length() && src->open(path.c_str())) {
    // deep DMA buffer (32 x 64 samples) so short stalls don't glitch the sound
    out = new AudioOutputI2S(0, AudioOutputI2S::INTERNAL_DAC, 32);
    out->SetOutputModeMono(true);
    out->SetGain(settings.volume / 100.0f);
    if (path.endsWith(".mp3")) gen = new AudioGeneratorMP3();
    else gen = new AudioGeneratorWAV();
    if (gen->begin(src, out)) {
      // speaker only: DAC2 / GPIO26 (left); keep DAC1 / GPIO25 free for touch
      i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN);
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
  delete out;
  src->close();
  delete src;
  busy = false;
  vTaskDelete(nullptr);
}

bool audioPlay() {
  if (storageAudioFile().length() == 0) return false;
  if (busy.exchange(true)) return false;
  if (xTaskCreatePinnedToCore(audioTask, "audio", 10240, nullptr, 2, nullptr, 1) != pdPASS) {
    busy = false;
    return false;
  }
  return true;
}
