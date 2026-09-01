// Driver for the 1.54" 200x200 SSD1681 e-paper panel.
//
// Two refresh modes:
//   full    — ~2s, flashes black/white, leaves the panel perfectly clean
//   partial — ~350ms, no flash, but deposits a little ghosting each time
//
// `present()` picks between them. It forces a full refresh every
// kPartialsBeforeFull updates so ghosting can never accumulate without bound —
// the panel stays readable no matter how long the device runs.
#pragma once

#include <stdint.h>

namespace gfx {
class Canvas;
}

namespace epaper {

// Full refresh after this many partials. ~40 keeps the flash rare enough to go
// unnoticed while holding contrast steady.
constexpr uint16_t kPartialsBeforeFull = 40;

enum class Refresh : uint8_t {
  kAuto,     // partial, with a periodic full refresh mixed in
  kPartial,  // force partial — for animation frames
  kFull,     // force full — on wake, or after heavy inversion
};

// How the framebuffer maps onto the physical glass.
//
// The controller can reverse its scan direction but cannot rotate or mirror
// cleanly — bit order inside a byte is fixed in hardware — so the whole
// transform is applied while streaming instead. The panel is square, so a
// quarter turn needs no change of dimensions.
//
// `quarterTurns` rotates the CONTENT clockwise by that many 90-degree steps.
// `mirror` then flips it horizontally, which covers the reflected cases.
// Between them these describe all eight ways a panel can be mounted.
struct Orientation {
  uint8_t quarterTurns;  // 0..3
  bool mirror;
};

constexpr Orientation kOrientations[] = {
    {0, false}, {1, false}, {2, false}, {3, false},
    {0, true},  {1, true},  {2, true},  {3, true},
};
constexpr int kOrientationCount = 8;

// Confirmed on hardware: this panel's gate scan runs bottom-to-top, so the
// content needs a vertical flip and nothing else. Expressed here as a half
// turn plus a horizontal mirror, which is the same transform.
//
// The rotation machinery above is more than this board needs; it stays because
// it costs nothing and makes the driver reusable if the panel is ever remounted.
constexpr Orientation kDefaultOrientation = kOrientations[6];  // {2 turns, mirror}

// Called repeatedly while the driver waits on the panel. A refresh blocks for
// 500 ms or more, and without this nothing samples the buttons during that
// window — a press that begins and ends inside a repaint is simply lost.
void setBusyHook(void (*hook)());

bool begin();

void setOrientation(Orientation o);
Orientation orientation();

// Push the canvas to the panel. Blocks until the panel reports idle.
void present(const gfx::Canvas& canvas, Refresh mode = Refresh::kAuto);

// Put the panel into deep sleep. It retains the last image with no power draw.
// begin() must be called again afterwards.
void sleep();

}  // namespace epaper
