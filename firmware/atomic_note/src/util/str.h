// Small string helpers shared across the UI.
#pragma once

#include <Arduino.h>

namespace util {

// The GFX fonts we ship cover printable ASCII only. Transliterate the accented
// Latin characters and typographic punctuation that transcripts and pasted
// config text tend to carry, drop anything else, and squeeze runs of spaces.
String displayable(const String& in);

// Copy `src` into `dst` uppercased, always NUL-terminating within `dstSize`.
void upperCopy(char* dst, const char* src, size_t dstSize);

}  // namespace util
