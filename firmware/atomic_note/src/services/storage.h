// SD card access.
//
// The card is OPTIONAL. Nothing above this layer may assume it is present —
// the device has to stay useful with an empty slot, so every call reports
// failure rather than trapping, and callers degrade instead of refusing to run.
//
// Writes go through a temp file and a rename. A half-written task list is
// worse than no task list, and losing power mid-save is a normal event on a
// device with a power button.
#pragma once

#include <Arduino.h>

namespace services {
namespace storage {

bool begin();

// Whether a card actually mounted. False is a normal state, not an error.
bool available();

uint64_t totalBytes();
uint64_t usedBytes();

// Ensure a directory exists. Returns false if the card is absent or the
// directory could not be created.
bool ensureDir(const char* path);

// Read a whole file into `out`, capped at `maxLen`. Returns false if missing.
bool readFile(const char* path, String& out, size_t maxLen = 8192);

// Write via temp-and-rename, so the destination is never partially written.
bool writeFileAtomic(const char* path, const String& content);

bool exists(const char* path);
bool remove(const char* path);

}  // namespace storage
}  // namespace services
