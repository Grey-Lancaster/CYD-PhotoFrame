// Sound playback (music.wav or music.mp3 from the SD card) through the
// internal DAC. The file is read through SdFat under the shared SD mutex, so
// no remounting of the card is needed.

#include <SdFat.h>
#include <atomic>
#include "AudioFileSource.h"
#include "AudioGeneratorWAV.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include "app.h"

class AudioFileSourceSdFat : public AudioFileSource {
 public:
  bool open(const char *filename) override {
    if (!storageLock(3000)) return false;
    opened = file.open(filename, O_RDONLY);
    storageUnlock();
    return opened;
  }
  uint32_t read(void *data, uint32_t len) override {
    if (!opened) return 0;
    storageLock(1000);
    int n = file.read(data, len);
    storageUnlock();
    return n > 0 ? n : 0;
  }
  bool seek(int32_t pos, int dir) override {
    if (!opened) return false;
    storageLock(1000);
    bool ok;
    if (dir == SEEK_SET) ok = file.seekSet(pos);
    else if (dir == SEEK_CUR) ok = file.seekCur(pos);
    else ok = file.seekEnd(pos);
    storageUnlock();
    return ok;
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
  uint32_t getSize() override { return opened ? file.fileSize() : 0; }
  uint32_t getPos() override { return opened ? file.curPosition() : 0; }

 private:
  SdBaseFile file;
  bool opened = false;
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
    out = new AudioOutputI2S(0, AudioOutputI2S::INTERNAL_DAC);
    out->SetOutputModeMono(true);
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
