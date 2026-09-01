// WAV playback from the SD card.
//
// Pumped by the caller, like the recorder, so a long note does not block the
// UI or the button state machine for its whole duration.
#pragma once

#include <Arduino.h>

namespace audio {
namespace player {

bool start(const char* path);
bool active();

// Push one chunk to the codec. Returns false at end of file or on error.
bool pump();

void stop();

uint32_t elapsedMs();
uint32_t durationMs();

}  // namespace player
}  // namespace audio
