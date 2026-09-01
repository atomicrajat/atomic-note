// A 1-bit-per-pixel drawing surface that owns its own framebuffer.
//
// The panel driver knows nothing about drawing; the canvas knows nothing about
// SPI. `Canvas::bits()` is the only seam between them.
//
// Bit layout matches what SSD1681 expects: row-major, 8 pixels per byte,
// most-significant bit leftmost, and a SET bit means WHITE.
#pragma once

// Arduino.h must precede gfxfont.h — the latter uses uint8_t/uint16_t without
// including <stdint.h> itself.
#include <Arduino.h>
#include <gfxfont.h>

#include "../util/str.h"

// gfxfont.h typedefs an anonymous struct, so GFXfont cannot be forward-declared
// — the header has to come in here. It is tiny and pulls in nothing else.

namespace gfx {

// kInvert flips whatever is already there. It is what lets a label stay
// readable across a partly-filled shape — black where the fill has not
// reached, white where it has — without the caller knowing the fill level.
enum Ink : uint8_t { kWhite = 0, kBlack = 1, kInvert = 2 };

// Font sizes, named by role rather than point size so screens don't hardcode
// typographic details.
enum class Font : uint8_t {
  kMicro,    // ~5px caps    — numbers inside gauges and badges
  kBody,     // 9pt regular  — list rows, paragraphs
  kLabel,    // 9pt bold     — headers, button hints
  kTitle,    // 12pt bold    — screen titles, stat values
  kDisplay,  // 18pt bold    — prominent numbers
  kJumbo,    // 24pt bold    — the clock, and nothing else
};

enum class Align : uint8_t { kLeft, kCenter, kRight };

class Canvas {
 public:
  Canvas(int width, int height);

  bool begin();  // allocates the framebuffer; false if out of memory

  int width() const { return w_; }
  int height() const { return h_; }
  int stride() const { return stride_; }
  const uint8_t* bits() const { return buf_; }

  void fill(Ink ink);
  void pixel(int x, int y, Ink ink);

  void rect(int x, int y, int w, int h, Ink ink);
  void rectOutline(int x, int y, int w, int h, int thickness, Ink ink);
  void roundRect(int x, int y, int w, int h, int radius, Ink ink);
  void roundRectOutline(int x, int y, int w, int h, int radius, int thickness,
                        Ink ink);
  void circle(int cx, int cy, int r, Ink ink);
  void circleOutline(int cx, int cy, int r, int thickness, Ink ink);
  void line(int x0, int y0, int x1, int y1, Ink ink);
  void lineThick(int x0, int y0, int x1, int y1, int thickness, Ink ink);
  void hLine(int x, int y, int len, Ink ink) { rect(x, y, len, 1, ink); }
  void vLine(int x, int y, int len, Ink ink) { rect(x, y, 1, len, ink); }

  // 1bpp bitmap, MSB-first, in PROGMEM. A set bit paints `ink`; clear bits are
  // left untouched so bitmaps composite onto whatever is already there.
  void bitmap(int x, int y, const uint8_t* bits, int w, int h, Ink ink);

  // ── Text ────────────────────────────────────────────────────────────────
  // `y` is the TOP of the line, not the baseline — screens think in boxes.
  // ── Scale ───────────────────────────────────────────────────────────────
  // Every text call takes an integer `scale` that multiplies the glyph pixels.
  // The bundled fonts jump from ~7px straight to 9pt (22px line), so scaling
  // the micro font is the only way to hit the sizes in between. At 2x a pixel
  // font reads as deliberately blocky rather than broken, which suits the
  // instrument look; do not scale the proportional fonts above 1x, they go
  // lumpy.
  int textWidth(const char* s, Font font, int scale = 1) const;

  // Distance from the top of a line to its baseline. This is the height of the
  // INK for capitals and digits.
  int capHeight(Font font, int scale = 1) const;

  // Full line advance, including descender space. Use this to stack lines —
  // NOT to centre text in a box. Centring by lineHeight reserves room for
  // descenders that digits and capitals do not have, which pushes the text
  // visibly high. Use capHeight() for that.
  int lineHeight(Font font) const;

  // Draws `s` vertically centred on the ink, in the box (x, y, w, h).
  void textInBox(int x, int y, int w, int h, const char* s, Font font, Ink ink,
                 Align align = Align::kCenter, int scale = 1);
  void text(int x, int y, const char* s, Font font, Ink ink,
            int scale = 1);
  void textAligned(int x, int y, int boxWidth, const char* s, Font font,
                   Ink ink, Align align);
  // Draws `s` truncated with a trailing ellipsis if it exceeds `maxWidth`.
  void textElided(int x, int y, int maxWidth, const char* s, Font font,
                  Ink ink);
  // Word-wraps into `maxLines`, starting at logical line `skipLines`.
  // Returns the total number of logical lines the text occupies, so callers
  // can paginate without laying it out twice.
  int textWrapped(int x, int y, int maxWidth, int maxLines, const char* s,
                  Font font, Ink ink, int skipLines = 0);

 private:
  const GFXfont* fontFor(Font font) const;
  int drawGlyph(int x, int baseline, char c, const GFXfont* f, Ink ink,
                int scale);
  int ascent(const GFXfont* f) const;

  int w_;
  int h_;
  int stride_;
  uint8_t* buf_ = nullptr;
};

}  // namespace gfx
