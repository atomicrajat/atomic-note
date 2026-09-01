// A tape measure, using the time-of-flight sensor.
//
// Live by default, because the useful gesture is pointing the device at
// something and reading the number. Holding it still and pressing a button to
// take a reading would be a worse tape measure than a tape measure.
//
// Three states and nothing else:
//   live    the distance updates as you point it
//   held    frozen on the last good reading, so it can be written down
//   none    no sensor attached
//
// Units change with a tap in either state. The reading is stored in
// millimetres and converted on the way out, so switching units never loses
// precision or re-measures.
#pragma once

#include "../ui/screen.h"

namespace apps {

class Measure : public ui::Screen {
 public:
  const char* name() const override { return "Measure"; }

  void onEnter(ui::Router& router) override;
  void draw(gfx::Canvas& canvas) override;
  bool onEvent(ui::Router& router, input::Button button,
               input::Event event) override;

  // The sensor produces a reading every 33 ms, but the PANEL takes about half
  // a second to show one. Polling faster than the screen can draw only burns
  // power, so this is paced to the display rather than to the sensor.
  uint32_t tickIntervalMs() const override { return 250; }
  void onTick(ui::Router& router) override;

 private:
  enum class Unit : uint8_t { kMm, kCm, kInch, kFeet, kCount };

  bool held_ = false;
  bool hasReading_ = false;
  uint16_t mm_ = 0;
  Unit unit_ = Unit::kCm;

  // Only repaint when the displayed value actually changes.
  //
  // A live reading jitters by a millimetre or two while the device is held
  // still, and repainting for that would refresh the panel four times a second
  // to show the same number — visible as constant flicker, and pointless.
  uint16_t shownMm_ = 0;

  void step(ui::Router& router);
};

}  // namespace apps
