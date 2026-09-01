// Non-blocking two-button input.
//
// Two design decisions worth knowing before changing anything here:
//
// 1. THERE IS NO DOUBLE-CLICK. Detecting one means withholding every single
//    click until the double window expires — 250 ms of dead air on a device
//    whose screen already takes half a second to redraw. Nothing in the UI
//    needed it, so a click fires the instant the button comes up. Reverse
//    navigation is a long press on B, not a double tap.
//
// 2. SAMPLING IS SEPARATE FROM READING. A panel refresh blocks for 500 ms or
//    more; a press that starts and ends inside that window would be missed
//    entirely, because nothing polls the GPIO. So `sample()` advances the
//    state machines and can be called from anywhere — including from inside
//    the display driver's wait loop — while `poll()` is what consumes events.
#pragma once

#include <stdint.h>

// ── The gesture grammar ───────────────────────────────────────────────────
// Four gestures, and the whole device is built from them:
//
//   Tap A    previous        Hold A   select / confirm
//   Tap B    next            Hold B   back
//
// Reverse navigation is on a TAP, deliberately. It used to be hold-B, which
// meant overshooting an item in a long list cost either a slow gesture or a
// full wrap-around — the single worst thing about moving around this device.
//
// The two exceptions, both at the root screen where there is nothing to select
// and nothing to go back to: hold A records, hold B sleeps.
//
namespace input {

enum class Button : uint8_t { kA, kB };

enum class Event : uint8_t {
  kNone,
  kClick,
  kLongPress,
};

constexpr uint16_t kDebounceMs = 20;
constexpr uint16_t kLongPressMs = 600;

struct Reading {
  Button button;
  Event event;
};

void begin();

// Advance both state machines and queue anything they produce. Cheap, safe to
// call at any time, and does not consume events.
void sample();

// Sample, then return the oldest queued event. Returns kNone when empty.
Reading poll();

// True while the button is physically down (debounced).
bool isHeld(Button b);

// Raw level, no debounce — for reading strap state immediately after wake.
bool isDownRaw(Button b);

}  // namespace input
