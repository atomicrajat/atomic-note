// The I2S link to the codec.
//
// One peripheral, shared. The ES8311 is a duplex codec: the microphone and the
// speaker run off the same clock, so they share a sample rate and cannot be
// reconfigured independently. Playback and capture therefore go through one
// owner rather than each opening their own I2S.
#pragma once

#include <Arduino.h>

namespace audio {
namespace bus {

constexpr uint32_t kSampleRate = 16000;

// Brings up the codec and the I2S peripheral in both directions.
bool begin();
bool ready();

// Interleaved stereo, 16-bit. Blocks until the DMA has taken it.
size_t write(const void* data, size_t bytes);

// Interleaved stereo, 16-bit, straight from the microphone. Blocks until the
// requested bytes are available.
size_t read(void* data, size_t bytes);

}  // namespace bus
}  // namespace audio
