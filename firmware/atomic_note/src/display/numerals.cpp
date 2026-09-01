#include "numerals.h"

#include <Arduino.h>

namespace gfx {
namespace numerals {
namespace {

// Segment layout, in the conventional order:
//
//      --A--
//     |     |
//     F     B
//     |     |
//      --G--
//     |     |
//     E     C
//     |     |
//      --D--
//
// Bit 0 = A, 1 = B, 2 = C, 3 = D, 4 = E, 5 = F, 6 = G.
constexpr uint8_t A = 1 << 0, B = 1 << 1, C = 1 << 2, D = 1 << 3,
                  E = 1 << 4, F = 1 << 5, G = 1 << 6;

constexpr uint8_t kDigits[10] = {
    (uint8_t)(A | B | C | D | E | F),      // 0
    (uint8_t)(B | C),                      // 1
    (uint8_t)(A | B | G | E | D),          // 2
    (uint8_t)(A | B | G | C | D),          // 3
    (uint8_t)(F | G | B | C),              // 4
    (uint8_t)(A | F | G | C | D),          // 5
    (uint8_t)(A | F | G | E | C | D),      // 6
    (uint8_t)(A | B | C),                  // 7
    (uint8_t)(A | B | C | D | E | F | G),  // 8
    (uint8_t)(A | B | C | D | F | G),      // 9
};

// Segments carry a small fillet rather than round caps — square enough to stay
// rectangular, softened just enough not to look like a calculator.
int filletFor(int stroke) { return stroke >= 6 ? stroke / 3 : 0; }

void drawDigit(Canvas& canvas, int x, int y, const Metrics& m, uint8_t segs,
               Ink ink) {
  const int w = m.digitWidth;
  const int h = m.digitHeight;
  const int t = m.stroke;
  const int r = filletFor(t);
  const int half = h / 2;

  // Vertical segments stop just short of the horizontals so the joins read as
  // distinct strokes instead of a solid block.
  const int vTop = y + t + 1;
  const int vLen = half - t - 2;
  const int vBottomY = y + half + t / 2 + 1;

  if (segs & A) canvas.roundRect(x + t, y, w - 2 * t, t, r, ink);
  if (segs & G) canvas.roundRect(x + t, y + half - t / 2, w - 2 * t, t, r, ink);
  if (segs & D) canvas.roundRect(x + t, y + h - t, w - 2 * t, t, r, ink);

  if (segs & F) canvas.roundRect(x, vTop, t, vLen, r, ink);
  if (segs & B) canvas.roundRect(x + w - t, vTop, t, vLen, r, ink);
  if (segs & E) canvas.roundRect(x, vBottomY, t, vLen, r, ink);
  if (segs & C) canvas.roundRect(x + w - t, vBottomY, t, vLen, r, ink);
}

void drawColon(Canvas& canvas, int x, int y, const Metrics& m, Ink ink) {
  const int t = m.stroke;
  const int r = filletFor(t);
  const int cx = x + (m.colonWidth - t) / 2;
  canvas.roundRect(cx, y + m.digitHeight / 3 - t / 2, t, t, r, ink);
  canvas.roundRect(cx, y + 2 * m.digitHeight / 3 - t / 2, t, t, r, ink);
}

int advanceFor(char c, const Metrics& m) {
  if (c == ':') return m.colonWidth;
  if (c == ' ') return m.digitWidth / 2;
  if (c >= '0' && c <= '9') return m.digitWidth;
  return 0;
}

}  // namespace

Metrics metricsFor(int height) {
  Metrics m;
  m.digitHeight = height;
  // Proportions tuned by eye at 56 px, which is the size the clock uses.
  m.digitWidth = (height * 55) / 100;
  m.stroke = (height * 13) / 100;
  m.gap = (height * 9) / 100;
  m.colonWidth = (height * 16) / 100;
  return m;
}

int widthOf(const char* s, const Metrics& m) {
  if (!s || !*s) return 0;
  int total = 0;
  bool first = true;
  for (const char* p = s; *p; p++) {
    const int adv = advanceFor(*p, m);
    if (adv == 0) continue;
    if (!first) total += m.gap;
    total += adv;
    first = false;
  }
  return total;
}

void draw(Canvas& canvas, int x, int y, const char* s, const Metrics& m,
          Ink ink) {
  if (!s) return;
  int cursor = x;
  bool first = true;
  for (const char* p = s; *p; p++) {
    const int adv = advanceFor(*p, m);
    if (adv == 0) continue;
    if (!first) cursor += m.gap;
    first = false;

    if (*p == ':') {
      drawColon(canvas, cursor, y, m, ink);
    } else if (*p >= '0' && *p <= '9') {
      drawDigit(canvas, cursor, y, m, kDigits[*p - '0'], ink);
    }
    cursor += adv;
  }
}

}  // namespace numerals
}  // namespace gfx
