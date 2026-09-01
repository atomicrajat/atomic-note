#include "str.h"

namespace util {
namespace {

struct Replacement {
  const char* from;
  const char* to;
};

// UTF-8 sequences we fold down to ASCII. Ordered longest-first is unnecessary
// here because none of these are prefixes of one another.
constexpr Replacement kFolds[] = {
    {"\xC3\xA4", "ae"}, {"\xC3\xB6", "oe"}, {"\xC3\xBC", "ue"},
    {"\xC3\x84", "Ae"}, {"\xC3\x96", "Oe"}, {"\xC3\x9C", "Ue"},
    {"\xC3\x9F", "ss"}, {"\xC3\xA9", "e"},  {"\xC3\xA8", "e"},
    {"\xC3\xAA", "e"},  {"\xC3\xA1", "a"},  {"\xC3\xA0", "a"},
    {"\xC3\xA2", "a"},  {"\xC3\xB3", "o"},  {"\xC3\xB2", "o"},
    {"\xC3\xB4", "o"},  {"\xC3\xAD", "i"},  {"\xC3\xAC", "i"},
    {"\xC3\xAE", "i"},  {"\xC3\xBA", "u"},  {"\xC3\xB9", "u"},
    {"\xC3\xBB", "u"},  {"\xC3\xB1", "n"},  {"\xC3\xA7", "c"},
    // Smart quotes, dashes, ellipsis.
    {"\xE2\x80\x9C", "\""}, {"\xE2\x80\x9D", "\""}, {"\xE2\x80\x9E", "\""},
    {"\xE2\x80\x98", "'"},  {"\xE2\x80\x99", "'"},  {"\xE2\x80\x93", "-"},
    {"\xE2\x80\x94", "-"},  {"\xE2\x80\xA6", "..."},
};

}  // namespace

String displayable(const String& in) {
  String s = in;
  for (const Replacement& r : kFolds) s.replace(r.from, r.to);

  String out;
  out.reserve(s.length());
  bool pendingSpace = false;
  for (size_t i = 0; i < s.length(); i++) {
    const uint8_t c = (uint8_t)s[i];
    const bool isSpace = (c == ' ' || c == '\n' || c == '\r' || c == '\t');
    if (isSpace) {
      // Collapse runs, and never emit a leading space.
      if (out.length() > 0) pendingSpace = true;
      continue;
    }
    if (c < 32 || c > 126) continue;  // unrenderable — drop it
    if (pendingSpace) {
      out += ' ';
      pendingSpace = false;
    }
    out += (char)c;
  }
  return out;
}

void upperCopy(char* dst, const char* src, size_t dstSize) {
  if (dstSize == 0) return;
  size_t i = 0;
  for (; i + 1 < dstSize && src[i]; i++) {
    const char c = src[i];
    dst[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
  }
  dst[i] = '\0';
}

}  // namespace util
