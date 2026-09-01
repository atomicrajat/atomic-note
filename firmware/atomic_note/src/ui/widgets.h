// Shared UI chrome. Every screen draws its own content, but headers, tiles and
// button markers must look and sit in exactly the same place everywhere, so
// they live here rather than being re-invented per screen.
#pragma once

#include "../display/canvas.h"
#include "theme.h"

namespace ui {
namespace widgets {

// ── Screen corners ────────────────────────────────────────────────────────
// The case rounds the visible area, so a full-bleed element drawn to the panel
// edge has its corners clipped by plastic and looks wrong. This whites out the
// pixels outside theme::kScreenRadius. The Router calls it after every screen
// draws, so screens never need to think about it.
void maskScreenCorners(gfx::Canvas& canvas);

// ── Button markers ────────────────────────────────────────────────────────
// The two buttons sit on the RIGHT EDGE, one above the other. Each is marked
// by a small tab flush against that edge, at the button's height — a pointer to
// the hardware, not a label.
//
// Deliberately unlettered. "A" and "B" carried no meaning for the reader (the
// buttons are not labelled on the case either) and the glyphs read as visual
// noise. What a button *does* is shown by actionBar() on the screens that
// genuinely need it; most do not.
constexpr int kSlotTopY = 126;     // centre line of the upper button
constexpr int kSlotBottomY = 174;  // centre line of the lower button

// Width reserved on the right for the markers. Content stays left of
// contentRight().
constexpr int kBezelWidth = 14;

enum class Slot : uint8_t { kTop, kBottom };

void buttonMarker(gfx::Canvas& canvas, Slot slot, bool active = false);

// Both markers. The default for every screen.
void buttonMarkers(gfx::Canvas& canvas);

// Rightmost x that content may use without colliding with the markers.
int contentRight(const gfx::Canvas& canvas);

// Optional bottom strip spelling out what the buttons do. Only for screens
// where the action is not obvious — a confirmation prompt, say. Returns the
// height consumed so callers can subtract it from their layout.
int actionBar(gfx::Canvas& canvas, const char* aLabel, const char* bLabel);

// ── Tiles ─────────────────────────────────────────────────────────────────
// The dashboard is composed of tiles rather than loose text, which is what
// gives it the instrument-panel feel.
enum class TileStyle : uint8_t {
  kFilled,   // solid ink, for inverted content
  kOutline,  // hairline border
  kBracket,  // corner brackets only — open edges, HUD-like
};

void tile(gfx::Canvas& canvas, int x, int y, int w, int h, TileStyle style);

// A rectangle with its corners cut off — the instrument-panel frame the
// dashboard sits inside. `chamfer` is how far the cut runs along each edge.
void hudFrame(gfx::Canvas& canvas, int x, int y, int w, int h, int chamfer,
              int thickness, gfx::Ink ink = gfx::kBlack);

// ── Glyph icons ───────────────────────────────────────────────────────────
// Drawn rather than stored as bitmaps so they scale with the layout and cost
// no PROGMEM. Each occupies roughly 12x24.
void iconThermometer(gfx::Canvas& canvas, int x, int y, gfx::Ink ink);
void iconDroplet(gfx::Canvas& canvas, int x, int y, gfx::Ink ink);

// Transport glyphs, drawn into a `size` x `size` box. Used by the focus timer
// now and by audio playback later.
void iconPlay(gfx::Canvas& canvas, int x, int y, int size, gfx::Ink ink);
void iconPause(gfx::Canvas& canvas, int x, int y, int size, gfx::Ink ink);
void iconCheck(gfx::Canvas& canvas, int x, int y, int size, gfx::Ink ink);

// ── Status bar ────────────────────────────────────────────────────────────
// The inverted strip along the top: free text on the left, connectivity and
// power indicators packed against the right.
//
// Icons are laid out right-to-left from the edge and only drawn when their
// flag is set, so the row stays tight however many are active. WiFi and
// Bluetooth have no backing implementation until phase 5 — the slots exist now
// so the layout does not have to be redesigned when they arrive.
struct Status {
  bool wifi = false;
  bool bluetooth = false;
  int batteryPercent = -1;  // negative hides the battery
};

void statusBar(gfx::Canvas& canvas, int x, int y, int w, int h,
               const char* leftText, const Status& status);

// Individual icons, for screens that place them themselves. `ink` should
// contrast with whatever is behind them.
// `size` is the icon's width; the arcs scale with it.
void iconWifi(gfx::Canvas& canvas, int x, int y, gfx::Ink ink, int size = 13);
void iconBluetooth(gfx::Canvas& canvas, int x, int y, gfx::Ink ink);
void iconBattery(gfx::Canvas& canvas, int x, int y, int percent, gfx::Ink ink);

// Phone-style battery: a wide cell with the charge level filled from the left
// and the percentage printed inside it. The number is drawn with kInvert so it
// stays legible whether the fill has reached it or not.
//
// Returns the total width used, including the terminal nub.
int batteryGauge(gfx::Canvas& canvas, int x, int y, int w, int h, int percent);

// ── Header ────────────────────────────────────────────────────────────────
// Inverted title band across the top. Returns the y below it.
int header(gfx::Canvas& canvas, const char* title);

}  // namespace widgets
}  // namespace ui
