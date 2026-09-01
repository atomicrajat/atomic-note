// Sending recordings to the companion service and storing what comes back.
//
// The device holds no credentials and knows nothing about the backend: it
// POSTs a WAV and receives plain text. Whether that text came from a model on
// a laptop or from a cloud API is the companion's business, and changing it
// needs no reflash.
//
// What comes back is no longer the raw transcript. The companion structures
// each note — title, summary, actions — files it in a knowledge base and
// returns a short digest of that instead. A transcript is a wall of
// unpunctuated speech, which on a 200x200 panel is close to unreadable; a
// title and two lines is what you actually want when glancing at something you
// said last week. The full transcript is kept on the laptop, in the note.
//
// The device sends its tag along with the audio, because the tag is what tells
// the far end how to read the note. That contract lives in one place: the
// headers set in transcribe.cpp.
//
// Plain text rather than JSON on purpose — the firmware would otherwise carry
// a parser to extract one string.
#pragma once

#include <Arduino.h>

namespace services {
namespace transcribe {

// Is the companion reachable? Cheap GET, used before starting a batch so the
// failure is reported once rather than once per note.
bool reachable();

// Upload one note's audio and store the transcript against it.
// Returns false and sets `error` on any failure.
bool one(int noteIndex, String& error);

}  // namespace transcribe
}  // namespace services
