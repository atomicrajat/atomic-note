// Power rail and battery-latch control.
//
// The board can cut its own power. `latch()` must be the first thing setup()
// does, before any delay longer than a few milliseconds, or the device dies as
// soon as the user lets go of the power button.
#pragma once

#include <stdint.h>

namespace power {

// Assert the battery latch. Call FIRST in setup().
void latch();

// Release the latch — the board loses power immediately if running on battery.
// This never returns when unplugged.
void shutdown();

void epdRail(bool on);
void audioRail(bool on);

// Configure ADC and take an averaged reading of the cell voltage.
float batteryVolts();

// 0-100, or -1 if the reading looks implausible (e.g. USB-only, no cell).
int batteryPercent();

}  // namespace power
