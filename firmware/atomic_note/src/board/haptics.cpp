#include "haptics.h"

#include "../services/settings.h"
#include "pins.h"

namespace haptics {
namespace {

bool ready = false;

// Durations, set from a sweep against this actual motor rather than by
// analogy with the audio cues.
//
// The first attempt used 12 ms and 25 ms, picked because the sounds are
// 10-60 ms. That was simply wrong: a motor is not a speaker. An
// eccentric-mass motor has to physically spin up, which takes tens of
// milliseconds before a hand can detect anything at all, so those pulses were
// imperceptible.
//
// Sweeping 50 / 100 / 200 / 500 / 1000 / 2000 ms on the bench and asking what
// each felt like gave the starting point; a second pass on the device settled
// the rest.
//
// THESE ARE VALIDATED VALUES, not estimates. They were confirmed by hand on
// the actual motor, and the three button cues are deliberately distinguishable
// from one another by touch alone — which is the whole point when the device
// is in a pocket. Retune them only against real hardware, the same way, and
// use `buzz [ms]` on the serial console to find a number before changing one
// here. Guessing is what produced the 12 ms first attempt that could not be
// felt at all.
//
// Intensity is not separately adjustable — the pin is driven on or off, so
// duration is the only lever, and a longer pulse simply spins the mass up
// further. That is why "stronger" here means "slightly longer". Real amplitude
// control would need PWM on the pin.
constexpr uint32_t kTickMs = 90;   // stepping through a list
constexpr uint32_t kBackMs = 100;  // leaving — heavier than a step
constexpr uint32_t kBumpMs = 120;  // confirming — the most deliberate of the three
constexpr uint32_t kAlertPulseMs = 200;  // a reminder, meant to interrupt
constexpr uint32_t kAlertGapMs = 130;
constexpr int kAlertPulses = 3;

void pulse(uint32_t ms) {
  if (!ready) return;
  digitalWrite(pins::HAPTIC, HIGH);
  delay(ms);
  digitalWrite(pins::HAPTIC, LOW);
}

}  // namespace

void begin() {
  pinMode(pins::HAPTIC, OUTPUT);
  digitalWrite(pins::HAPTIC, LOW);
  ready = true;
}

void setEnabled(bool on) { services::settings::setHapticsEnabled(on); }

bool enabled() { return services::settings::hapticsEnabled(); }

void tick() {
  if (!enabled()) return;
  pulse(kTickMs);
}

void back() {
  if (!enabled()) return;
  pulse(kBackMs);
}

void bump() {
  if (!enabled()) return;
  pulse(kBumpMs);
}

void alert() {
  // Deliberately NOT gated on the preference, matching audio::sound::alert().
  // Cues are a preference; a reminder is the thing you asked to be
  // interrupted by, and silencing it by accident defeats the feature.
  for (int i = 0; i < kAlertPulses; i++) {
    pulse(kAlertPulseMs);
    if (i + 1 < kAlertPulses) delay(kAlertGapMs);
  }
}

void off() {
  if (!ready) return;
  digitalWrite(pins::HAPTIC, LOW);
}

void test(uint32_t ms) {
  // Ungated on the preference and on `ready`, because the whole point is to
  // answer "is anything attached to this pin at all" — which has to work
  // before anything else is trusted.
  pinMode(pins::HAPTIC, OUTPUT);
  ready = true;
  digitalWrite(pins::HAPTIC, HIGH);
  delay(ms);
  digitalWrite(pins::HAPTIC, LOW);
}

}  // namespace haptics
