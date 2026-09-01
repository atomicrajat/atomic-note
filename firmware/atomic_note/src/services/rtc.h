// PCF85063 real-time clock.
//
// The chip stores UTC in BCD and is backed by its own capacitor, so it keeps
// time across deep sleep and battery swaps. The ESP32's own clock is set from
// it at boot; everything above this layer uses standard time_t / struct tm.
//
// Local time is derived at display time from the configured UTC offset — the
// chip itself is never set to local time, which keeps DST changes from
// corrupting stored timestamps.
#pragma once

#include <stdint.h>
#include <time.h>

namespace services {
namespace rtc {

// Returns true only if a hardware RTC chip answered. Not finding one is not an
// error — see hasChip().
bool begin();

// Whether a PCF85063 is actually present on this board.
//
// It is not on every revision. Without it, timekeeping falls back to the
// ESP32's internal RTC, which survives deep sleep but NOT a power cut or
// reset. Once NTP lands the difference stops mattering for anything except a
// device that has been fully unpowered.
bool hasChip();

// True if the current time is trustworthy, from whichever source is available.
bool timeIsValid();

// Set the clock from a UTC epoch: system clock always, plus the chip if one is
// present. This is the single entry point for setting time — serial console
// now, NTP later.
bool setSystemUtc(time_t epoch);

bool readUtc(struct tm* out);
bool writeUtc(const struct tm& utc);

// Set the ESP32 system clock from the chip. Returns false if the chip's time
// is not trustworthy.
bool syncSystemFromChip();

// Persist the current system clock into the chip. Used after an NTP sync.
bool syncChipFromSystem();

// Convert a UTC tm to epoch seconds without disturbing the process timezone.
time_t utcToEpoch(struct tm utc);

// ── Local time ────────────────────────────────────────────────────────────
// Minutes east of UTC. Configurable later from the web app; until then this is
// the one place to change it.
int utcOffsetMinutes();
void setUtcOffsetMinutes(int minutes);

// Current local time. Returns false if the clock is not set.
bool localNow(struct tm* out);

}  // namespace rtc
}  // namespace services
