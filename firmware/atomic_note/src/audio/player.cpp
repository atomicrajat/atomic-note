#include "player.h"

#include <SD_MMC.h>
#include <esp_heap_caps.h>

#include "bus.h"
#include "es8311.h"

namespace audio {
namespace player {
namespace {

constexpr size_t kMonoChunkBytes = 2048;
constexpr int kHeaderBytes = 44;

File file;
int16_t* monoBuf = nullptr;
int16_t* stereoBuf = nullptr;
bool playing = false;
uint32_t startedMs = 0;
uint32_t totalMs = 0;

void freeBuffers() {
  if (monoBuf) { heap_caps_free(monoBuf); monoBuf = nullptr; }
  if (stereoBuf) { heap_caps_free(stereoBuf); stereoBuf = nullptr; }
}

}  // namespace

bool start(const char* path) {
  if (playing) stop();
  if (!bus::ready()) return false;

  file = SD_MMC.open(path, FILE_READ);
  if (!file) {
    Serial.printf("[play] cannot open %s\n", path);
    return false;
  }
  if (file.size() <= kHeaderBytes) {
    Serial.println("[play] file has no audio");
    file.close();
    return false;
  }

  monoBuf = (int16_t*)heap_caps_malloc(kMonoChunkBytes, MALLOC_CAP_8BIT);
  stereoBuf = (int16_t*)heap_caps_malloc(kMonoChunkBytes * 2, MALLOC_CAP_8BIT);
  if (!monoBuf || !stereoBuf) {
    freeBuffers();
    file.close();
    return false;
  }

  // Skip the header. Our own recordings are always canonical 44-byte PCM, so
  // there is no chunk walking to do.
  const uint32_t dataBytes = file.size() - kHeaderBytes;
  totalMs = (dataBytes / 2) * 1000 / bus::kSampleRate;
  file.seek(kHeaderBytes);

  es8311::setAmplifier(true);
  delay(2);
  startedMs = millis();
  playing = true;
  return true;
}

bool active() { return playing; }

bool pump() {
  if (!playing) return false;

  const int read = file.read((uint8_t*)monoBuf, kMonoChunkBytes);
  if (read <= 0) return false;

  // The codec's slot is stereo, so mono samples are duplicated to both.
  const int samples = read / (int)sizeof(int16_t);
  for (int i = 0; i < samples; i++) {
    stereoBuf[i * 2] = monoBuf[i];
    stereoBuf[i * 2 + 1] = monoBuf[i];
  }
  bus::write(stereoBuf, (size_t)samples * 2 * sizeof(int16_t));
  return true;
}

void stop() {
  if (!playing) return;
  playing = false;
  file.close();
  freeBuffers();
  es8311::setAmplifier(false);
}

uint32_t elapsedMs() { return playing ? millis() - startedMs : 0; }
uint32_t durationMs() { return totalMs; }

}  // namespace player
}  // namespace audio
