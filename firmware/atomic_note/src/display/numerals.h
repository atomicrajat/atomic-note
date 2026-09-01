// Large drawn numerals.
//
// The biggest bundled GFX font tops out around 34 px of cap height, which is
// not enough for a clock that has to be readable across a room. These digits
// are drawn as geometry instead, so the size is a parameter rather than a
// property of a font file — and they cost no PROGMEM.
//
// Only the glyphs a clock needs: 0-9, colon, and space.
#pragma once

#include "canvas.h"

namespace gfx {
namespace numerals {

// Proportions are derived from the height, so callers pick one number and the
// stroke weight and spacing follow.
struct Metrics {
  int digitWidth;
  int digitHeight;
  int stroke;
  int gap;        // between glyphs
  int colonWidth;
};

Metrics metricsFor(int height);

// Width the string will occupy. Use it to centre, or to check it fits.
int widthOf(const char* s, const Metrics& m);

// Draws with (x, y) as the TOP-LEFT of the glyph box.
void draw(Canvas& canvas, int x, int y, const char* s, const Metrics& m,
          Ink ink);

}  // namespace numerals
}  // namespace gfx
