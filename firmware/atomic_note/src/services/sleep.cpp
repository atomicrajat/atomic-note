#include "sleep.h"

#include <Arduino.h>
#include <esp_sleep.h>

#include "../board/pins.h"
#include "../board/haptics.h"
#include "../board/power.h"
#include "../display/epaper.h"
#include "../audio/sound.h"
#include "network.h"
#include "battery_log.h"
#include "reminders.h"

namespace services {
namespace {

WakeReason cached = WakeReason::kUnknown;
bool cachedValid = false;

}  // namespace

WakeReason wakeReason() {
  if (cachedValid) return cached;
  cachedValid = true;

  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    cached = WakeReason::kTimer;
    return cached;
  }
  if (cause != ESP_SLEEP_WAKEUP_EXT1) {
    cached = WakeReason::kColdBoot;
    return cached;
  }

  // ext1 reports which pins were low. Both may be set if the user mashed them;
  // A wins because it is the action button.
  const uint64_t mask = esp_sleep_get_ext1_wakeup_status();
  if (mask & (1ULL << pins::BTN_A)) {
    cached = WakeReason::kButtonA;
  } else if (mask & (1ULL << pins::BTN_B)) {
    cached = WakeReason::kButtonB;
  } else {
    cached = WakeReason::kUnknown;
  }
  return cached;
}

void enterDeepSleep() {
  Serial.println("[sleep] entering deep sleep");
  Serial.flush();

  // Bring the radio down cleanly first — deep sleep would cut it anyway, but
  // this closes sockets and lets the AP drop us properly.
  audio::sound::quiet();  // the amplifier draws current even when silent
  haptics::off();         // never sleep with the motor still energised
  network::stop();

  epaper::sleep();
  power::audioRail(false);
  power::epdRail(false);

  // Hold the battery latch through sleep — dropping it would power the board
  // off entirely instead of sleeping it.
  power::latch();

  constexpr uint64_t wakeMask =
      (1ULL << pins::BTN_A) | (1ULL << pins::BTN_B);
  esp_sleep_enable_ext1_wakeup_io(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);

  // Also wake on a timer if a reminder is pending, in bounded hops.
  //
  // A battery measurement, if one is running, wants a wake of its own. Take
  // whichever comes first so the two schedules cannot starve each other.
  uint32_t untilNext = reminders::secondsUntilNext();
  const uint32_t untilSample = battery_log::secondsUntilNext();
  if (untilSample > 0 && (untilNext == 0 || untilSample < untilNext)) {
    untilNext = untilSample;
  }

  if (untilNext > 0) {
    const uint32_t hop =
        untilNext < kMaxSleepHopSec ? untilNext : kMaxSleepHopSec;
    esp_sleep_enable_timer_wakeup((uint64_t)hop * 1000000ULL);
    Serial.printf("[sleep] waking in %us\n", (unsigned)hop);
  }

  delay(20);
  esp_deep_sleep_start();
  // Unreachable — deep sleep resets the chip on wake.
  while (true) {
  }
}

}  // namespace services
