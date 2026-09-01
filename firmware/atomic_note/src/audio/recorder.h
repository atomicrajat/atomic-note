// Voice capture to a WAV file on the SD card.
//
// Driven by the caller rather than owning a loop: start(), then pump()
// repeatedly for as long as the button is held, then stop(). That keeps the
// button state machine and the display alive during a recording, which a
// self-contained blocking loop would not.
//
// The codec captures stereo because that is how the I2S slot is configured,
// but both channels carry the same mono microphone. Only the left channel is
// kept — storing both would double the file for no information.
#pragma once

#include <Arduino.h>

namespace audio {
namespace recorder {

// Below this, a recording is treated as a mis-press and discarded rather than
// left on the card as a fraction of a second of nothing.
constexpr uint32_t kMinDurationMs = 500;

bool begin();

// Opens `path` and writes a placeholder header. The real header cannot be
// written until the length is known, so stop() patches it afterwards.
bool start(const char* path);

bool active();

// Read one chunk from the codec and append it. Returns false on a write
// failure, which means the card is full or gone — the caller should stop.
bool pump();

// Pump only if a recording is in progress. Safe to call from anywhere,
// including the display driver's busy-wait — a panel refresh blocks for around
// half a second, which is long enough to overrun the capture DMA and leave a
// gap in the audio.
void pumpIfActive();

uint32_t elapsedMs();
uint32_t recordedBytes();

// Finalises the header and closes. Returns false if the recording was too
// short to keep, in which case the file is removed.
bool stop();

// Stop and delete regardless of length.
void abort();

}  // namespace recorder
}  // namespace audio
