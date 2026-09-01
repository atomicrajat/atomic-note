#include "sound.h"

#include <math.h>

#include "../services/settings.h"
#include "bus.h"
#include "es8311.h"

namespace audio {
namespace sound {
namespace {

constexpr uint32_t kSampleRate = bus::kSampleRate;

bool ready = false;

// Ask the codec whether the amplifier is live rather than remembering it here.
// The recorder and the player switch the same amplifier, so a local copy goes
// stale the moment either runs — which is exactly how UI sounds fell silent
// after a recording.
void ensureAmp() {
  if (!es8311::amplifierOn()) {
    es8311::setAmplifier(true);
    delay(2);  // let the PA settle, otherwise the attack is a pop
  }
}

// One tone, written straight to I2S.
//
// Shaped with a short attack and a decay to zero: a raw square or an abruptly
// truncated sine puts a step in the waveform, and a step through a speaker is
// a click. The envelope is what makes these read as "tick" rather than "pop".
void tone(float hz, int ms, float amplitude) {
  if (!ready) return;
  ensureAmp();

  const int frames = (int)((kSampleRate * (uint32_t)ms) / 1000);
  if (frames <= 0) return;

  constexpr int kChunkFrames = 128;
  int16_t chunk[kChunkFrames * 2];  // stereo interleaved

  const int attackFrames = min(frames / 4, (int)(kSampleRate / 500));
  const float step = 2.0f * (float)M_PI * hz / (float)kSampleRate;
  float phase = 0.0f;

  int written = 0;
  while (written < frames) {
    const int count = min(kChunkFrames, frames - written);
    for (int i = 0; i < count; i++) {
      const int index = written + i;

      float envelope = 1.0f;
      if (index < attackFrames) {
        envelope = (float)index / (float)attackFrames;
      } else {
        const float remaining =
            (float)(frames - index) / (float)(frames - attackFrames);
        envelope = remaining;
      }

      const float value = sinf(phase) * envelope * amplitude;
      phase += step;
      if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;

      const int16_t sample = (int16_t)(value * 26000.0f);
      chunk[i * 2] = sample;
      chunk[i * 2 + 1] = sample;
    }
    bus::write(chunk, (size_t)count * 2 * sizeof(int16_t));
    written += count;
  }
}

void gap(int ms) {
  if (!ready) return;
  // Silence through the same path, so the DMA stream stays continuous. A real
  // pause would let the buffer underrun and click.
  const int frames = (int)((kSampleRate * (uint32_t)ms) / 1000);
  constexpr int kChunkFrames = 128;
  int16_t chunk[kChunkFrames * 2] = {0};
  int written = 0;
  while (written < frames) {
    const int count = min(kChunkFrames, frames - written);
    bus::write(chunk, (size_t)count * 2 * sizeof(int16_t));
    written += count;
  }
}

}  // namespace

bool begin() {
  ready = false;
  if (!bus::begin()) return false;
  ready = true;
  es8311::setAmplifier(false);
  Serial.println("[sound] ready");
  return true;
}

bool available() { return ready; }

void setEnabled(bool on) { services::settings::setSoundEnabled(on); }

bool enabled() { return services::settings::soundEnabled(); }

void quiet() {
  if (!ready) return;
  es8311::setAmplifier(false);
}

// Each cue is a distinct shape, not just a distinct pitch — pitch alone is
// hard to tell apart through a small speaker in a noisy room.
void click() {
  if (!ready || !enabled()) return;
  tone(2000.0f, 12, 0.30f);
}

void select() {
  if (!ready || !enabled()) return;
  tone(1400.0f, 14, 0.45f);
  tone(2100.0f, 18, 0.40f);
}

void back() {
  if (!ready || !enabled()) return;
  tone(1500.0f, 14, 0.35f);
  tone(900.0f, 20, 0.35f);
}

void toggle() {
  if (!ready || !enabled()) return;
  tone(2400.0f, 16, 0.40f);
}

void success() {
  if (!ready || !enabled()) return;
  tone(1046.0f, 45, 0.40f);   // C6
  tone(1318.0f, 45, 0.40f);   // E6
  tone(1568.0f, 80, 0.42f);   // G6
}

void alert() {
  // Deliberately NOT gated on `enabled()`. Sounds are a preference; a reminder
  // firing is the one thing the user explicitly asked to be interrupted for,
  // and silently swallowing it would make the feature unreliable.
  if (!ready) return;
  for (int i = 0; i < 3; i++) {
    tone(1760.0f, 90, 0.55f);
    gap(70);
  }
}

}  // namespace sound
}  // namespace audio
