#include "power.h"

#include <Arduino.h>

#include "pins.h"

namespace power {
namespace {

bool adcReady = false;

void ensureAdc() {
  if (adcReady) return;
  pinMode(pins::BATT_ADC, INPUT);
  analogSetPinAttenuation(pins::BATT_ADC, ADC_11db);
  analogReadMilliVolts(pins::BATT_ADC);  // discard the first conversion
  adcReady = true;
}

// Discharge curve for a single Li-ion cell, sampled at five points. Linear
// interpolation between them is plenty for a battery icon; anything fancier
// would imply an accuracy this divider does not have.
struct CurvePoint {
  float volts;
  int percent;
};

constexpr CurvePoint kCurve[] = {
    {3.20f, 0}, {3.55f, 20}, {3.75f, 50}, {3.95f, 80}, {4.20f, 100},
};
constexpr int kCurveLen = sizeof(kCurve) / sizeof(kCurve[0]);

}  // namespace

void latch() {
  pinMode(pins::BATT_LATCH, OUTPUT);
  digitalWrite(pins::BATT_LATCH, HIGH);
}

void shutdown() {
  digitalWrite(pins::BATT_LATCH, LOW);
  delay(200);  // on USB power we survive this; make it obvious we tried
}

void epdRail(bool on) {
  pinMode(pins::RAIL_EPD, OUTPUT);
  digitalWrite(pins::RAIL_EPD, on ? LOW : HIGH);  // active low
}

void audioRail(bool on) {
  pinMode(pins::RAIL_AUDIO, OUTPUT);
  digitalWrite(pins::RAIL_AUDIO, on ? LOW : HIGH);  // active low
}

float batteryVolts() {
  ensureAdc();
  constexpr int kSamples = 16;
  uint32_t total = 0;
  for (int i = 0; i < kSamples; i++) {
    total += analogReadMilliVolts(pins::BATT_ADC);
    delay(2);
  }
  return (total / (float)kSamples) / 1000.0f * 2.0f;
}

int batteryPercent() {
  const float v = batteryVolts();
  if (v < 2.5f) return -1;  // nothing sensible on the divider
  if (v <= kCurve[0].volts) return 0;
  if (v >= kCurve[kCurveLen - 1].volts) return 100;

  for (int i = 1; i < kCurveLen; i++) {
    if (v > kCurve[i].volts) continue;
    const CurvePoint& lo = kCurve[i - 1];
    const CurvePoint& hi = kCurve[i];
    const float t = (v - lo.volts) / (hi.volts - lo.volts);
    return lo.percent + (int)lroundf((hi.percent - lo.percent) * t);
  }
  return 100;
}

}  // namespace power
