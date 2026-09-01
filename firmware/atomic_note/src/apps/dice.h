// A die you roll by shaking the device.
//
// The first thing here that is an INPUT rather than a readout. Two buttons can
// confirm and step; they cannot express "roll it", and a shake can — which is
// the whole argument for the sensor being worth having.
//
// Needs an MPU6050. The menu row is struck through without one, so this screen
// is normally unreachable in that state; it still checks, because a part can
// be unplugged while the device is running.
#pragma once

#include "../ui/screen.h"

namespace apps {

class Dice : public ui::Screen {
 public:
  const char* name() const override { return "Dice"; }

  void onEnter(ui::Router& router) override;
  void draw(gfx::Canvas& canvas) override;
  bool onEvent(ui::Router& router, input::Button button,
               input::Event event) override;

  // Polled often enough to catch the peak of a shake. A shake lasts a couple
  // hundred milliseconds and the peak is a fraction of that, so sampling at
  // 25 ms is the difference between catching it and missing it.
  uint32_t tickIntervalMs() const override { return 25; }
  void onTick(ui::Router& router) override;

 private:
  enum class Phase : uint8_t { kWaiting, kRolled, kNoSensor };

  Phase phase_ = Phase::kWaiting;
  int value_ = 0;
  int rolls_ = 0;

  // A shake is one gesture, not the dozen samples above threshold that it
  // produces. After a roll the device has to come back to rest before another
  // can trigger, or a single vigorous shake rolls five times.
  bool armed_ = true;
  uint32_t calmSinceMs_ = 0;

  // What this sensor reports lying still, measured on entry. See dice.cpp:
  // an uncalibrated MPU6050 is several percent off 1 g, and a fixed threshold
  // would mean a different gesture on every board.
  float restG_ = 1.0f;

  void roll(ui::Router& router);
};

}  // namespace apps
