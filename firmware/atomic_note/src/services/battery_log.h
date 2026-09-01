// Overnight battery drain measurement.
//
// The device has no fuel gauge and the cell has no documented capacity, so the
// only honest way to answer "how long does it last" is to measure it. This
// wakes on a timer, appends one voltage reading to a CSV on the card, and goes
// straight back to sleep without touching the panel or the radio.
//
// The measurement disturbs what it measures — each wake costs a second or two
// of active current. At a 15-minute interval that is well under 1% of the
// elapsed time, which is small enough to read the sleep floor off the curve
// and large enough to be worth stating rather than hiding.
#pragma once

#include <Arduino.h>

namespace services {
namespace battery_log {

// Wake interval while a test is running.
constexpr uint32_t kIntervalSec = 900;  // 15 minutes

bool running();
void start();
void stop();

// Append one reading. Called on a timer wake before anything expensive comes
// up. Returns false if the card is unavailable.
bool sample();

// Seconds until the next reading is due, or 0 when no test is running.
uint32_t secondsUntilNext();

}  // namespace battery_log
}  // namespace services
