// The recorded notes index.
//
// Audio lives in /atomic/notes/note_NNN.wav; this is the catalogue that gives
// each one a tag and a timestamp, so the list can be browsed and filtered
// without opening every file.
#pragma once

#include <Arduino.h>
#include <time.h>

namespace services {
namespace notes {

constexpr int kMaxNotes = 200;
constexpr int kMaxTagLen = 20;

struct Note {
  int number;
  char tag[kMaxTagLen];
  time_t createdUtc;
  uint32_t durationMs;
  bool hasText;  // a transcript has been fetched and stored
};

void begin();

int count();
const Note* at(int index);

// Next free note number. Numbers are never reused, so a deleted note does not
// resurrect under an old recording's name.
int nextNumber();

// Path for a note's audio, whether or not it exists yet.
String wavPath(int number);

// Path for a note's transcript.
String textPath(int number);

// Store a transcript and mark the note. Returns false if the card is absent.
bool setTranscript(int index, const String& text);

// Read a transcript back. Empty if there is none.
String transcript(int index);

// How many notes are still waiting for one.
int countWithoutText();

// Index of the Nth note that has no transcript, or -1.
int indexOfUntranscribed(int position);

bool add(int number, const char* tag, uint32_t durationMs);
bool setTag(int index, const char* tag);
bool removeAt(int index);

// ── Filtering ─────────────────────────────────────────────────────────────
// A null tag means "everything". Notes are presented newest first, because a
// voice note is nearly always about something that just happened.
int countWithTag(const char* tag);
int indexOfFiltered(const char* tag, int visiblePosition);

bool save();

}  // namespace notes
}  // namespace services
