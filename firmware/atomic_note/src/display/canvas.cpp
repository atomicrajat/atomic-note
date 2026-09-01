#include "canvas.h"

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/Picopixel.h>
#include <esp_heap_caps.h>
#include <pgmspace.h>

namespace gfx {
namespace {

inline int clampLow(int v, int lo) { return v < lo ? lo : v; }

}  // namespace

Canvas::Canvas(int width, int height)
    : w_(width), h_(height), stride_((width + 7) / 8) {}

bool Canvas::begin() {
  if (buf_) return true;
  const size_t bytes = (size_t)stride_ * h_;
  // Prefer PSRAM — internal RAM is the scarce resource once WiFi and the audio
  // codec are live.
  buf_ = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf_) buf_ = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
  if (!buf_) return false;
  memset(buf_, 0xFF, bytes);
  return true;
}

void Canvas::fill(Ink ink) {
  if (!buf_) return;
  memset(buf_, ink == kBlack ? 0x00 : 0xFF, (size_t)stride_ * h_);
}

void Canvas::pixel(int x, int y, Ink ink) {
  if (!buf_ || (unsigned)x >= (unsigned)w_ || (unsigned)y >= (unsigned)h_)
    return;
  uint8_t& byte = buf_[y * stride_ + (x >> 3)];
  const uint8_t mask = 0x80 >> (x & 7);
  if (ink == kBlack) {
    byte &= ~mask;
  } else if (ink == kWhite) {
    byte |= mask;
  } else {
    byte ^= mask;  // kInvert
  }
}

void Canvas::rect(int x, int y, int w, int h, Ink ink) {
  if (!buf_) return;
  // Clip to the surface.
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (w <= 0 || h <= 0 || x >= w_ || y >= h_) return;
  if (x + w > w_) w = w_ - x;
  if (y + h > h_) h = h_ - y;

  const int xEnd = x + w - 1;
  const int byteFirst = x >> 3;
  const int byteLast = xEnd >> 3;
  // Partial masks for the two edge bytes; whole bytes between them get memset.
  const uint8_t maskFirst = (uint8_t)(0xFF >> (x & 7));
  const uint8_t maskLast = (uint8_t)(0xFF << (7 - (xEnd & 7)));

  // Apply `ink` to the bits of `*p` selected by `m`.
  auto blend = [ink](uint8_t* p, uint8_t m) {
    if (ink == kBlack) {
      *p &= (uint8_t)~m;
    } else if (ink == kWhite) {
      *p |= m;
    } else {
      *p ^= m;  // kInvert
    }
  };

  for (int row = 0; row < h; row++) {
    uint8_t* p = buf_ + (y + row) * stride_;
    if (byteFirst == byteLast) {
      blend(p + byteFirst, (uint8_t)(maskFirst & maskLast));
      continue;
    }
    blend(p + byteFirst, maskFirst);
    if (byteLast > byteFirst + 1) {
      const int span = byteLast - byteFirst - 1;
      if (ink == kInvert) {
        // No memset equivalent for XOR — the whole-byte run has to be walked.
        for (int i = 0; i < span; i++) p[byteFirst + 1 + i] ^= 0xFF;
      } else {
        memset(p + byteFirst + 1, ink == kBlack ? 0x00 : 0xFF, span);
      }
    }
    blend(p + byteLast, maskLast);
  }
}

void Canvas::rectOutline(int x, int y, int w, int h, int thickness, Ink ink) {
  for (int i = 0; i < thickness; i++) {
    rect(x + i, y + i, w - 2 * i, 1, ink);
    rect(x + i, y + h - 1 - i, w - 2 * i, 1, ink);
    rect(x + i, y + i, 1, h - 2 * i, ink);
    rect(x + w - 1 - i, y + i, 1, h - 2 * i, ink);
  }
}

void Canvas::circle(int cx, int cy, int r, Ink ink) {
  if (r < 0) return;
  for (int dy = -r; dy <= r; dy++) {
    const int dx = (int)lroundf(sqrtf((float)(r * r - dy * dy)));
    rect(cx - dx, cy + dy, 2 * dx + 1, 1, ink);
  }
}

void Canvas::circleOutline(int cx, int cy, int r, int thickness, Ink ink) {
  // Midpoint circle, repeated inward for thickness.
  for (int t = 0; t < thickness; t++) {
    int rr = r - t;
    if (rr < 0) break;
    int x = rr, y = 0, err = 1 - rr;
    while (x >= y) {
      pixel(cx + x, cy + y, ink); pixel(cx + y, cy + x, ink);
      pixel(cx - y, cy + x, ink); pixel(cx - x, cy + y, ink);
      pixel(cx - x, cy - y, ink); pixel(cx - y, cy - x, ink);
      pixel(cx + y, cy - x, ink); pixel(cx + x, cy - y, ink);
      y++;
      if (err < 0) {
        err += 2 * y + 1;
      } else {
        x--;
        err += 2 * (y - x) + 1;
      }
    }
  }
}

void Canvas::roundRect(int x, int y, int w, int h, int radius, Ink ink) {
  if (radius <= 0) { rect(x, y, w, h, ink); return; }
  radius = min(radius, min(w, h) / 2);
  rect(x + radius, y, w - 2 * radius, h, ink);
  rect(x, y + radius, radius, h - 2 * radius, ink);
  rect(x + w - radius, y + radius, radius, h - 2 * radius, ink);
  circle(x + radius, y + radius, radius, ink);
  circle(x + w - radius - 1, y + radius, radius, ink);
  circle(x + radius, y + h - radius - 1, radius, ink);
  circle(x + w - radius - 1, y + h - radius - 1, radius, ink);
}

void Canvas::roundRectOutline(int x, int y, int w, int h, int radius,
                              int thickness, Ink ink) {
  // Paint the full shape, then knock out the interior with the opposite ink.
  roundRect(x, y, w, h, radius, ink);
  const Ink inverse = (ink == kBlack) ? kWhite : kBlack;
  if (w > 2 * thickness && h > 2 * thickness) {
    roundRect(x + thickness, y + thickness, w - 2 * thickness,
              h - 2 * thickness, clampLow(radius - thickness, 0), inverse);
  }
}

void Canvas::line(int x0, int y0, int x1, int y1, Ink ink) {
  int dx = abs(x1 - x0), dy = -abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    pixel(x0, y0, ink);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void Canvas::lineThick(int x0, int y0, int x1, int y1, int thickness, Ink ink) {
  const int half = thickness / 2;
  const bool steep = abs(y1 - y0) > abs(x1 - x0);
  for (int i = -half; i <= half; i++) {
    // Offset perpendicular to the dominant axis. Good enough for UI chrome.
    if (steep) {
      line(x0 + i, y0, x1 + i, y1, ink);
    } else {
      line(x0, y0 + i, x1, y1 + i, ink);
    }
  }
}

void Canvas::bitmap(int x, int y, const uint8_t* data, int w, int h, Ink ink) {
  const int rowBytes = (w + 7) / 8;
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      const uint8_t byte = pgm_read_byte(&data[row * rowBytes + (col >> 3)]);
      if (byte & (0x80 >> (col & 7))) pixel(x + col, y + row, ink);
    }
  }
}

// ── Text ──────────────────────────────────────────────────────────────────

const GFXfont* Canvas::fontFor(Font font) const {
  switch (font) {
    case Font::kMicro:   return &Picopixel;
    case Font::kBody:    return &FreeSans9pt7b;
    case Font::kLabel:   return &FreeSansBold9pt7b;
    case Font::kTitle:   return &FreeSansBold12pt7b;
    case Font::kDisplay: return &FreeSansBold18pt7b;
    case Font::kJumbo:   return &FreeSansBold24pt7b;
  }
  return &FreeSans9pt7b;
}

// Distance from the top of the line box down to the baseline. GFX glyph
// yOffsets are negative above the baseline, so the largest magnitude across
// the font is its ascent.
int Canvas::ascent(const GFXfont* f) const {
  const uint8_t first = pgm_read_byte(&f->first);
  const uint8_t last = pgm_read_byte(&f->last);
  GFXglyph* glyphs = (GFXglyph*)pgm_read_ptr(&f->glyph);
  int maxAscent = 0;
  for (uint16_t c = first; c <= last; c++) {
    const int8_t yo = (int8_t)pgm_read_byte(&glyphs[c - first].yOffset);
    if (-yo > maxAscent) maxAscent = -yo;
  }
  return maxAscent;
}

int Canvas::lineHeight(Font font) const {
  const GFXfont* f = fontFor(font);
  return (int)pgm_read_byte(&f->yAdvance);
}

int Canvas::capHeight(Font font, int scale) const {
  return ascent(fontFor(font)) * scale;
}

void Canvas::textInBox(int x, int y, int w, int h, const char* s, Font font,
                       Ink ink, Align align, int scale) {
  // Centre on the ink, not on the line box.
  const int top = y + (h - capHeight(font, scale)) / 2;
  const int tw = textWidth(s, font, scale);
  int drawX = x;
  if (align == Align::kCenter) drawX = x + (w - tw) / 2;
  else if (align == Align::kRight) drawX = x + w - tw;
  text(drawX, top, s, font, ink, scale);
}

int Canvas::textWidth(const char* s, Font font, int scale) const {
  if (!s) return 0;
  const GFXfont* f = fontFor(font);
  const uint8_t first = pgm_read_byte(&f->first);
  const uint8_t last = pgm_read_byte(&f->last);
  GFXglyph* glyphs = (GFXglyph*)pgm_read_ptr(&f->glyph);
  int total = 0;
  for (const char* p = s; *p; p++) {
    const uint8_t c = (uint8_t)*p;
    if (c < first || c > last) continue;
    total += (uint8_t)pgm_read_byte(&glyphs[c - first].xAdvance);
  }
  return total * scale;
}

int Canvas::drawGlyph(int x, int baseline, char ch, const GFXfont* f, Ink ink,
                      int scale) {
  const uint8_t first = pgm_read_byte(&f->first);
  const uint8_t last = pgm_read_byte(&f->last);
  const uint8_t c = (uint8_t)ch;
  if (c < first || c > last) return 0;

  GFXglyph* g = &((GFXglyph*)pgm_read_ptr(&f->glyph))[c - first];
  const uint8_t* bmp = (const uint8_t*)pgm_read_ptr(&f->bitmap);
  uint16_t offset = pgm_read_word(&g->bitmapOffset);
  const uint8_t gw = pgm_read_byte(&g->width);
  const uint8_t gh = pgm_read_byte(&g->height);
  const int8_t xo = (int8_t)pgm_read_byte(&g->xOffset);
  const int8_t yo = (int8_t)pgm_read_byte(&g->yOffset);

  // Glyph bitmaps are a packed bitstream, not row-aligned.
  uint8_t bits = 0;
  uint8_t bitsLeft = 0;
  for (int row = 0; row < gh; row++) {
    for (int col = 0; col < gw; col++) {
      if (bitsLeft == 0) {
        bits = pgm_read_byte(&bmp[offset++]);
        bitsLeft = 8;
      }
      if (bits & 0x80) {
        if (scale == 1) {
          pixel(x + xo + col, baseline + yo + row, ink);
        } else {
          // Each source pixel becomes a scale x scale block.
          rect(x + (xo + col) * scale, baseline + (yo + row) * scale, scale,
               scale, ink);
        }
      }
      bits <<= 1;
      bitsLeft--;
    }
  }
  return (int)pgm_read_byte(&g->xAdvance) * scale;
}

void Canvas::text(int x, int y, const char* s, Font font, Ink ink, int scale) {
  if (!s) return;
  const GFXfont* f = fontFor(font);
  const int baseline = y + ascent(f) * scale;
  int cursor = x;
  for (const char* p = s; *p; p++) {
    cursor += drawGlyph(cursor, baseline, *p, f, ink, scale);
  }
}

void Canvas::textAligned(int x, int y, int boxWidth, const char* s, Font font,
                         Ink ink, Align align) {
  const int tw = textWidth(s, font);
  int drawX = x;
  if (align == Align::kCenter) drawX = x + (boxWidth - tw) / 2;
  else if (align == Align::kRight) drawX = x + boxWidth - tw;
  text(drawX, y, s, font, ink);
}

void Canvas::textElided(int x, int y, int maxWidth, const char* s, Font font,
                        Ink ink) {
  if (!s) return;
  if (textWidth(s, font) <= maxWidth) {
    text(x, y, s, font, ink);
    return;
  }
  String truncated(s);
  while (truncated.length() > 0 &&
         textWidth((truncated + "...").c_str(), font) > maxWidth) {
    truncated.remove(truncated.length() - 1);
  }
  truncated += "...";
  text(x, y, truncated.c_str(), font, ink);
}

int Canvas::textWrapped(int x, int y, int maxWidth, int maxLines,
                        const char* s, Font font, Ink ink, int skipLines) {
  if (!s) return 0;
  const int lh = lineHeight(font);
  const String text_(s);

  int logicalLine = 0;   // counts every line the text occupies
  int drawnLines = 0;    // counts only the ones inside the window
  String current;

  // Emits `line` if it falls inside the requested window.
  auto flush = [&](const String& line) {
    if (logicalLine >= skipLines && drawnLines < maxLines) {
      text(x, y + drawnLines * lh, line.c_str(), font, ink);
      drawnLines++;
    }
    logicalLine++;
  };

  size_t pos = 0;
  while (pos < text_.length()) {
    while (pos < text_.length() && text_[pos] == ' ') pos++;
    if (pos >= text_.length()) break;

    const int space = text_.indexOf(' ', pos);
    String word;
    if (space < 0) {
      word = text_.substring(pos);
      pos = text_.length();
    } else {
      word = text_.substring(pos, space);
      pos = (size_t)space + 1;
    }

    const String candidate = current.length() ? current + " " + word : word;
    if (textWidth(candidate.c_str(), font) <= maxWidth) {
      current = candidate;
      continue;
    }

    if (current.length()) {
      flush(current);
      current = word;
      // The word alone may still overflow; fall through to the split below on
      // the next iteration only if it is the sole content of the line.
      if (textWidth(current.c_str(), font) <= maxWidth) continue;
    }

    // A single word wider than the line: hard-split it with a hyphen.
    while (textWidth((current + "-").c_str(), font) > maxWidth &&
           current.length() > 1) {
      String head = current;
      while (head.length() > 1 &&
             textWidth((head + "-").c_str(), font) > maxWidth) {
        head.remove(head.length() - 1);
      }
      flush(head + "-");
      current = current.substring(head.length());
    }
  }
  if (current.length()) flush(current);

  return logicalLine;
}

}  // namespace gfx
