#include "widgets.h"

#include <Arduino.h>

namespace ui {
namespace widgets {
namespace {

// Marker tab geometry. Narrow on purpose — it points at the button, it is not
// a control in its own right.
constexpr int kTabWidth = 7;
constexpr int kTabHeight = 24;
constexpr int kTabInset = 5;  // sits inside the HUD frame, not on it

constexpr int kBarHeight = 22;

int slotY(Slot slot) {
  return slot == Slot::kTop ? kSlotTopY : kSlotBottomY;
}

// White out one corner's arc. (cx, cy) is the centre of the fillet circle;
// (dirX, dirY) point from that centre toward the panel corner.
void maskCorner(gfx::Canvas& canvas, int cx, int cy, int dirX, int dirY,
                int radius) {
  const int rSquared = radius * radius;
  for (int dy = 0; dy <= radius; dy++) {
    for (int dx = 0; dx <= radius; dx++) {
      if (dx * dx + dy * dy <= rSquared) continue;  // inside the fillet
      canvas.pixel(cx + dirX * dx, cy + dirY * dy, gfx::kWhite);
    }
  }
}

}  // namespace

void maskScreenCorners(gfx::Canvas& canvas) {
  const int r = theme::kScreenRadius;
  if (r <= 0) return;
  const int w = canvas.width();
  const int h = canvas.height();

  maskCorner(canvas, r, r, -1, -1, r);                // top-left
  maskCorner(canvas, w - 1 - r, r, 1, -1, r);         // top-right
  maskCorner(canvas, r, h - 1 - r, -1, 1, r);         // bottom-left
  maskCorner(canvas, w - 1 - r, h - 1 - r, 1, 1, r);  // bottom-right
}

void buttonMarker(gfx::Canvas& canvas, Slot slot, bool active) {
  // A solid tab just inside the right edge, at the button's height. Filled
  // rather than outlined — at this size an outline reads as a smudge, and the
  // marker has to hold its own against a busy frame.
  const int x = canvas.width() - kTabWidth - kTabInset;
  const int y = slotY(slot) - kTabHeight / 2;

  canvas.roundRect(x, y, kTabWidth, kTabHeight, theme::kChipRadius,
                   gfx::kBlack);
  if (active) {
    // Pressed: a white notch through the middle, visible against the fill.
    canvas.rect(x + 1, y + kTabHeight / 2 - 1, kTabWidth - 2, 3, gfx::kWhite);
  }
}

void buttonMarkers(gfx::Canvas& canvas) {
  buttonMarker(canvas, Slot::kTop);
  buttonMarker(canvas, Slot::kBottom);
}

// ── Status icons ──────────────────────────────────────────────────────────
// All drawn on a 12x11 footprint so they queue up evenly in the status bar.

void iconWifi(gfx::Canvas& canvas, int x, int y, gfx::Ink ink, int size) {
  // Arcs over a dot. Drawn as circle outlines with the lower half painted out,
  // which is cheaper than a real arc primitive. Radii are proportional so the
  // icon keeps its shape at any size.
  const int cx = x + size / 2;
  const int baseY = y + (size * 77) / 100;
  const int outerR = (size * 54) / 100;
  const int innerR = (size * 31) / 100;
  const gfx::Ink bg = (ink == gfx::kBlack) ? gfx::kWhite : gfx::kBlack;

  canvas.circleOutline(cx, baseY, outerR, 2, ink);
  canvas.circleOutline(cx, baseY, innerR, 2, ink);
  // Knock out everything below the base line, leaving only the upper arcs.
  canvas.rect(x - 3, baseY + 1, size + 6, outerR + 3, bg);
  canvas.rect(cx - 1, baseY - 1, 2, 2, ink);
}

void iconBluetooth(gfx::Canvas& canvas, int x, int y, gfx::Ink ink) {
  // The rune: a vertical spine with two chevrons crossing it.
  const int cx = x + 5;
  const int top = y + 1;
  const int bottom = y + 11;
  const int mid = (top + bottom) / 2;

  canvas.rect(cx, top, 2, bottom - top, ink);
  canvas.line(cx, top, cx + 4, top + 3, ink);
  canvas.line(cx + 4, top + 3, cx - 3, mid + 2, ink);
  canvas.line(cx, bottom, cx + 4, bottom - 3, ink);
  canvas.line(cx + 4, bottom - 3, cx - 3, mid - 2, ink);
}

void iconBattery(gfx::Canvas& canvas, int x, int y, int percent, gfx::Ink ink) {
  constexpr int kW = 20;
  constexpr int kH = 11;

  canvas.rectOutline(x, y, kW, kH, 1, ink);
  canvas.rect(x + kW, y + 3, 2, kH - 6, ink);

  if (percent < 0) {
    canvas.rect(x + 5, y + kH / 2, kW - 10, 1, ink);
    return;
  }
  const int fillMax = kW - 6;
  const int fill = (fillMax * constrain(percent, 0, 100)) / 100;
  if (fill > 0) canvas.rect(x + 3, y + 3, fill, kH - 6, ink);
}

int batteryGauge(gfx::Canvas& canvas, int x, int y, int w, int h, int percent) {
  constexpr int kNubW = 3;
  constexpr int kBorder = 2;

  // Shell.
  canvas.roundRectOutline(x, y, w, h, theme::kChipRadius, kBorder,
                          gfx::kBlack);
  canvas.roundRect(x + w, y + h / 2 - 4, kNubW, 8, 1, gfx::kBlack);

  // Charge level, filled from the left inside the border.
  const int innerX = x + kBorder + 1;
  const int innerY = y + kBorder + 1;
  const int innerW = w - 2 * (kBorder + 1);
  const int innerH = h - 2 * (kBorder + 1);

  char text[8];
  if (percent < 0) {
    snprintf(text, sizeof(text), "--");
  } else {
    const int fill = (innerW * constrain(percent, 0, 100)) / 100;
    if (fill > 0) canvas.rect(innerX, innerY, fill, innerH, gfx::kBlack);
    snprintf(text, sizeof(text), "%d", percent);
  }

  // kInvert means the digits punch through the fill where it has reached them
  // and print normally where it has not — no need to know the level here.
  // Micro font at 2x: the bundled fonts jump from ~7px to 9pt with nothing in
  // between, and 9pt was too heavy inside a gauge this size.
  canvas.textInBox(innerX, innerY, innerW, innerH, text, gfx::Font::kMicro,
                   gfx::kInvert, gfx::Align::kCenter, 2);

  return w + kNubW;
}

void statusBar(gfx::Canvas& canvas, int x, int y, int w, int h,
               const char* leftText, const Status& status) {
  tile(canvas, x, y, w, h, TileStyle::kFilled);

  constexpr int kIconGap = 6;
  constexpr int kIconY = 11;  // icon top, relative to the bar

  // Pack right to left so the row stays tight whatever is active.
  int cursor = x + w - 10;

  if (status.batteryPercent >= -1) {
    cursor -= 22;  // 20 wide plus the terminal nub
    iconBattery(canvas, cursor, y + (h - kIconY) / 2, status.batteryPercent,
                gfx::kWhite);
    cursor -= kIconGap;
  }
  if (status.bluetooth) {
    cursor -= 10;
    iconBluetooth(canvas, cursor, y + (h - 12) / 2, gfx::kWhite);
    cursor -= kIconGap;
  }
  if (status.wifi) {
    cursor -= 13;
    iconWifi(canvas, cursor, y + (h - 12) / 2, gfx::kWhite);
    cursor -= kIconGap;
  }

  if (leftText && *leftText) {
    const int textY = y + (h - canvas.lineHeight(gfx::Font::kLabel)) / 2 + 1;
    canvas.textElided(x + 10, textY, cursor - x - 14, leftText,
                      gfx::Font::kLabel, gfx::kWhite);
  }
}

int contentRight(const gfx::Canvas& canvas) {
  return canvas.width() - kBezelWidth;
}

int actionBar(gfx::Canvas& canvas, const char* aLabel, const char* bLabel) {
  const int y = canvas.height() - kBarHeight;
  canvas.rect(0, y, canvas.width(), 1, gfx::kBlack);

  const int textY = y + (kBarHeight - canvas.lineHeight(gfx::Font::kBody)) / 2;
  if (aLabel && *aLabel) {
    char text[24];
    snprintf(text, sizeof(text), "A  %s", aLabel);
    canvas.text(theme::kMargin, textY, text, gfx::Font::kBody, gfx::kBlack);
  }
  if (bLabel && *bLabel) {
    char text[24];
    snprintf(text, sizeof(text), "B  %s", bLabel);
    canvas.textAligned(0, textY, canvas.width() - theme::kMargin, text,
                       gfx::Font::kBody, gfx::kBlack, gfx::Align::kRight);
  }
  return kBarHeight;
}

void tile(gfx::Canvas& canvas, int x, int y, int w, int h, TileStyle style) {
  switch (style) {
    case TileStyle::kFilled:
      canvas.roundRect(x, y, w, h, theme::kCardRadius, gfx::kBlack);
      break;

    case TileStyle::kOutline:
      canvas.roundRectOutline(x, y, w, h, theme::kCardRadius,
                              theme::kStrokeThin, gfx::kBlack);
      break;

    case TileStyle::kBracket: {
      // Corner brackets with open edges. Cheaper in ink than a full border and
      // it reads as an instrument readout rather than a box of text.
      const int len = min(14, min(w, h) / 3);
      const int t = theme::kStrokeBold;
      // top-left
      canvas.rect(x, y, len, t, gfx::kBlack);
      canvas.rect(x, y, t, len, gfx::kBlack);
      // top-right
      canvas.rect(x + w - len, y, len, t, gfx::kBlack);
      canvas.rect(x + w - t, y, t, len, gfx::kBlack);
      // bottom-left
      canvas.rect(x, y + h - t, len, t, gfx::kBlack);
      canvas.rect(x, y + h - len, t, len, gfx::kBlack);
      // bottom-right
      canvas.rect(x + w - len, y + h - t, len, t, gfx::kBlack);
      canvas.rect(x + w - t, y + h - len, t, len, gfx::kBlack);
      break;
    }
  }
}

void hudFrame(gfx::Canvas& canvas, int x, int y, int w, int h, int chamfer,
              int thickness, gfx::Ink ink) {
  const int x2 = x + w - 1;
  const int y2 = y + h - 1;
  const int c = chamfer;

  // Straight runs, each pulled back by the chamfer at both ends.
  canvas.rect(x + c, y, w - 2 * c, thickness, ink);
  canvas.rect(x + c, y2 - thickness + 1, w - 2 * c, thickness, ink);
  canvas.rect(x, y + c, thickness, h - 2 * c, ink);
  canvas.rect(x2 - thickness + 1, y + c, thickness, h - 2 * c, ink);

  // The four cut corners.
  canvas.lineThick(x, y + c, x + c, y, thickness, ink);
  canvas.lineThick(x2 - c, y, x2, y + c, thickness, ink);
  canvas.lineThick(x, y2 - c, x + c, y2, thickness, ink);
  canvas.lineThick(x2 - c, y2, x2, y2 - c, thickness, ink);
}

void iconPlay(gfx::Canvas& canvas, int x, int y, int size, gfx::Ink ink) {
  // Right-pointing triangle, built from horizontal runs: widest on the centre
  // line, tapering to nothing at top and bottom. The canvas has no triangle
  // primitive and this needs no new one.
  const int half = size / 2;
  for (int row = 0; row < size; row++) {
    const int dy = abs(row - half);
    const int len = size - 2 * dy;
    if (len > 0) canvas.rect(x, y + row, len, 1, ink);
  }
}

void iconPause(gfx::Canvas& canvas, int x, int y, int size, gfx::Ink ink) {
  const int bar = size / 3;
  canvas.rect(x, y, bar, size, ink);
  canvas.rect(x + size - bar, y, bar, size, ink);
}

void iconCheck(gfx::Canvas& canvas, int x, int y, int size, gfx::Ink ink) {
  const int t = size / 5 + 1;
  canvas.lineThick(x, y + size / 2, x + size / 3, y + size - t, t, ink);
  canvas.lineThick(x + size / 3, y + size - t, x + size, y, t, ink);
}

void iconThermometer(gfx::Canvas& canvas, int x, int y, gfx::Ink ink) {
  const int cx = x + 5;
  canvas.roundRectOutline(cx - 4, y, 8, 17, 4, 1, ink);
  canvas.circleOutline(cx, y + 18, 5, 1, ink);
  // Filled column and bulb, so it reads as a reading rather than an outline.
  canvas.rect(cx - 1, y + 7, 2, 11, ink);
  canvas.circle(cx, y + 18, 3, ink);
}

void iconDroplet(gfx::Canvas& canvas, int x, int y, gfx::Ink ink) {
  const int cx = x + 5;
  // Taper from the point down to the belly, then close it with a circle.
  constexpr int kTaper = 10;
  for (int i = 0; i < kTaper; i++) {
    const int half = (i * 4) / kTaper;
    canvas.rect(cx - half, y + i, half * 2 + 1, 1, ink);
  }
  canvas.circle(cx, y + 12, 5, ink);
}

int header(gfx::Canvas& canvas, const char* title) {
  constexpr int kHeight = 28;
  canvas.rect(0, 0, canvas.width(), kHeight, gfx::kBlack);
  const int textH = canvas.lineHeight(gfx::Font::kLabel);
  canvas.textAligned(0, (kHeight - textH) / 2 + 1, canvas.width(), title,
                     gfx::Font::kLabel, gfx::kWhite, gfx::Align::kCenter);
  return kHeight;
}

}  // namespace widgets
}  // namespace ui
