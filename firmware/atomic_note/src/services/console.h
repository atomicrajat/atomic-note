// Minimal serial command console.
//
// Exists because there is no NTP until phase 5 and no keyboard ever — the clock
// has to be set from somewhere. Also handy for poking at hardware during
// bring-up without reflashing.
//
// Commands (newline terminated):
//   time <epoch>    set the RTC from a UTC unix timestamp
//   tz <minutes>    set the local offset east of UTC, e.g. 330 for IST
//   now             print the current UTC and local time
//   env             take an SHTC3 reading
//   batt            print battery voltage and percent
//   scan            list responding I2C addresses
//
// BRING-UP ONLY, below. These are compiled out unless config::kDevTools is
// true, because none of them is a product feature — a shipped device has no
// reason to dump its framebuffer or drive its motor on command:
//   buzz [ms]       pulse the haptic motor on GPIO 3, ignoring settings
//   motion [n]      print live accelerometer readings and the peak
//   dist [n]        print live distance readings from the VL53L0X
//   tof             identify which time-of-flight part is on 0x29
//   rail / rot      toggle a power rail / step panel orientation
//   shot            dump the framebuffer as hex, for tools/screenshot.py
//   screen [name]   open a screen by name; no argument lists them
//   sleep           enter deep sleep immediately
//   help            list commands
#pragma once

#include "../display/canvas.h"

namespace services {
namespace console {

// Called when a command changes something the screen shows. Wired to the
// Router by the sketch so console commands can force a repaint.
void setRedrawHook(void (*hook)());

// The framebuffer, for `shot`. Set by the sketch, which owns the Canvas.
void setCanvas(const gfx::Canvas* canvas);

// Open a screen by name, for `screen <name>`. The sketch owns the screen
// table, so it supplies the lookup rather than this file knowing every app.
// Returns false when the name is not recognised.
void setScreenHook(bool (*hook)(const char* name));

// Non-blocking: consumes whatever is buffered and returns. Safe to call every
// loop iteration.
void poll();

}  // namespace console
}  // namespace services
