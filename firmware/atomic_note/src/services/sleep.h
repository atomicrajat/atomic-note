// Deep sleep and wake-reason reporting.
//
// There is no light-sleep tier. E-paper holds its last image with zero power,
// so the device can go all the way down and still look "on" — waking straight
// into the right screen makes it feel instant.
#pragma once

#include <stdint.h>

namespace services {

enum class WakeReason : uint8_t {
  kColdBoot,   // power-on or reset
  kButtonA,
  kButtonB,
  kTimer,      // scheduled wake — a reminder is due, or nearly
  kUnknown,
};

// Longest single deep-sleep hop when a reminder is pending.
//
// The ESP32's sleep timer runs off an internal RC oscillator that drifts by a
// few percent, so sleeping the whole way to a reminder hours away would land
// minutes late. Hopping instead — sleep a bounded interval, wake, re-read the
// accurate RTC, recompute — keeps the error to one hop rather than the whole
// wait. Ten minutes at 5% is 30 s, comfortably inside the due window.
constexpr uint32_t kMaxSleepHopSec = 600;

// Read the wake cause. Must be called early in setup(), before the buttons
// have had a chance to be released.
WakeReason wakeReason();

// Park the panel, arm both buttons as wake sources, and deep sleep.
// Never returns.
[[noreturn]] void enterDeepSleep();

}  // namespace services
