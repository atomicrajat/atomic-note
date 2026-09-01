#include "recorder.h"

#include <SD_MMC.h>
#include <esp_heap_caps.h>

#include "bus.h"
#include "es8311.h"

namespace audio {
namespace recorder {
namespace {

// 8 KB of stereo is 2048 frames, about 128 ms at 16 kHz. Big enough that the
// SD write amortises well, small enough that pump() returns often enough to
// keep the button state machine responsive.
constexpr size_t kStereoChunkBytes = 8192;
constexpr size_t kMonoChunkBytes = kStereoChunkBytes / 2;

constexpr int kHeaderBytes = 44;

File file;
int16_t* stereoBuf = nullptr;
int16_t* monoBuf = nullptr;
bool recording = false;
uint32_t startedMs = 0;
uint32_t monoBytes = 0;
String path_;

void freeBuffers() {
  if (stereoBuf) {
    heap_caps_free(stereoBuf);
    stereoBuf = nullptr;
  }
  if (monoBuf) {
    heap_caps_free(monoBuf);
    monoBuf = nullptr;
  }
}

void writeLE32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

void writeLE16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
}

// Canonical 44-byte PCM WAV header for mono 16-bit at the bus rate.
void buildHeader(uint8_t* h, uint32_t dataBytes) {
  memcpy(h + 0, "RIFF", 4);
  writeLE32(h + 4, dataBytes + 36);  // everything after this field
  memcpy(h + 8, "WAVE", 4);
  memcpy(h + 12, "fmt ", 4);
  writeLE32(h + 16, 16);            // fmt chunk size
  writeLE16(h + 20, 1);             // PCM, uncompressed
  writeLE16(h + 22, 1);             // mono
  writeLE32(h + 24, bus::kSampleRate);
  writeLE32(h + 28, bus::kSampleRate * 2);  // byte rate: rate * block align
  writeLE16(h + 32, 2);             // block align: 1 channel * 16 bits
  writeLE16(h + 34, 16);            // bits per sample
  memcpy(h + 36, "data", 4);
  writeLE32(h + 40, dataBytes);
}

}  // namespace

bool begin() { return bus::ready(); }

bool active() { return recording; }

bool start(const char* path) {
  if (recording) return false;
  if (!bus::ready()) {
    Serial.println("[rec] codec not ready");
    return false;
  }

  stereoBuf = (int16_t*)heap_caps_malloc(kStereoChunkBytes, MALLOC_CAP_8BIT);
  monoBuf = (int16_t*)heap_caps_malloc(kMonoChunkBytes, MALLOC_CAP_8BIT);
  if (!stereoBuf || !monoBuf) {
    Serial.println("[rec] out of memory for capture buffers");
    freeBuffers();
    return false;
  }

  file = SD_MMC.open(path, FILE_WRITE);
  if (!file) {
    Serial.printf("[rec] cannot open %s\n", path);
    freeBuffers();
    return false;
  }

  // Placeholder header. The real one needs the length, which is only known
  // when recording stops, so it is patched in place afterwards.
  uint8_t placeholder[kHeaderBytes] = {0};
  file.write(placeholder, kHeaderBytes);

  // Speaker off while capturing. The microphone and amplifier share a case a
  // few millimetres apart; leaving the PA live is an feedback loop, and any UI
  // sound would be recorded along with the voice.
  es8311::setAmplifier(false);

  path_ = path;
  monoBytes = 0;
  startedMs = millis();
  recording = true;
  Serial.printf("[rec] recording to %s\n", path);
  return true;
}

bool pump() {
  if (!recording) return false;

  // Use what was ACTUALLY read. A short read is normal, and converting the
  // full buffer regardless appends whatever the previous chunk left behind —
  // audible as a burst of stale audio, most obviously at the end of a take.
  const size_t got = bus::read(stereoBuf, kStereoChunkBytes);
  if (got == 0) return true;  // nothing ready yet; not an error

  // Keep the left channel only: both carry the same mono microphone.
  const int frames = (int)(got / (2 * sizeof(int16_t)));
  for (int i = 0; i < frames; i++) monoBuf[i] = stereoBuf[i * 2];

  const size_t written =
      file.write((const uint8_t*)monoBuf, (size_t)frames * sizeof(int16_t));
  if (written == 0) {
    // A zero-length write means the card is full or has gone away. Carrying on
    // would spin, so surface it and let the caller stop.
    Serial.println("[rec] write failed — card full or removed");
    return false;
  }
  monoBytes += written;
  return true;
}

void pumpIfActive() {
  if (recording) pump();
}

uint32_t elapsedMs() {
  if (!recording) return 0;
  return millis() - startedMs;
}

uint32_t recordedBytes() { return monoBytes; }

bool stop() {
  if (!recording) return false;
  recording = false;
  // Hand the amplifier back. It was switched off for capture; leaving it off
  // silences everything that follows.
  es8311::setAmplifier(true);

  const uint32_t duration = millis() - startedMs;

  uint8_t header[kHeaderBytes];
  buildHeader(header, monoBytes);
  file.seek(0);
  file.write(header, kHeaderBytes);
  file.close();
  freeBuffers();

  const bool longEnough = duration >= kMinDurationMs && monoBytes > 1000;
  if (!longEnough) {
    // A stray tap should not leave a fraction of a second of nothing on the
    // card for the user to find and delete later.
    SD_MMC.remove(path_.c_str());
    Serial.printf("[rec] discarded (%ums, %u bytes)\n", (unsigned)duration,
                  (unsigned)monoBytes);
    return false;
  }

  Serial.printf("[rec] saved %s (%ums, %u bytes)\n", path_.c_str(),
                (unsigned)duration, (unsigned)monoBytes);
  return true;
}

void abort() {
  if (!recording) return;
  recording = false;
  es8311::setAmplifier(true);
  file.close();
  freeBuffers();
  SD_MMC.remove(path_.c_str());
  Serial.println("[rec] aborted");
}

}  // namespace recorder
}  // namespace audio
