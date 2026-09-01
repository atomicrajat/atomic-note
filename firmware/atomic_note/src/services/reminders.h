// Timed reminders.
//
// A reminder is a local time, a set of weekdays, and something to say. The
// device sleeps until the next one is due, wakes, shows it, and goes back to
// sleep.
//
// WAKING ON TIME, without an RTC interrupt line:
// The PCF85063 has an alarm output, but whether it reaches a GPIO on this
// board is not documented and the reference firmware never used it. So the
// scheduling uses the ESP32's own deep-sleep timer instead, which needs no
// extra wiring.
//
// That timer runs off an internal RC oscillator and drifts by a few percent —
// useless on its own for a reminder hours away. The fix is to never sleep for
// the whole interval: sleep in bounded hops, and on each wake re-read the
// accurate PCF85063 and recompute. Drift then cannot accumulate, and the worst
// case is one hop's error rather than the whole wait's.
#pragma once

#include <Arduino.h>
#include <time.h>

namespace services {
namespace reminders {

constexpr int kMaxReminders = 16;
constexpr int kMaxTextLen = 48;

// Bit 0 is Sunday, matching struct tm's tm_wday.
constexpr uint8_t kEveryDay = 0x7F;
constexpr uint8_t kWeekdays = 0x3E;  // Mon-Fri
constexpr uint8_t kOnce = 0x00;      // fires at the next occurrence, then off

struct Reminder {
  uint32_t id;
  char text[kMaxTextLen];
  uint8_t hour;    // local
  uint8_t minute;  // local
  uint8_t days;    // weekday bitmask, or kOnce
  bool enabled;
  time_t lastFiredUtc;
};

void begin();

int count();
const Reminder* at(int index);

bool add(const char* text, int hour, int minute, uint8_t days);
bool removeAt(int index);
void setEnabled(int index, bool enabled);

// UTC epoch of the soonest enabled reminder, or 0 if there is nothing pending.
time_t nextDueUtc();

// Index of a reminder that is due right now, or -1. Fired reminders are
// suppressed for a minute so one alert does not repeat on every wake.
int dueNow();

void markFired(int index);

// Seconds until the next reminder, clamped for the caller's sleep. Zero means
// nothing is scheduled.
uint32_t secondsUntilNext();

bool save();

}  // namespace reminders
}  // namespace services
