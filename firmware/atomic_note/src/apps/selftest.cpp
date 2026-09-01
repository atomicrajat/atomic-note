#include "selftest.h"

#include <Arduino.h>

#include "../board/power.h"
#include "../display/epaper.h"
#include "../services/sleep.h"
#include "../ui/router.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"

namespace apps {
namespace {

const char* wakeLabel(services::WakeReason r) {
  switch (r) {
    case services::WakeReason::kColdBoot: return "cold boot";
    case services::WakeReason::kButtonA:  return "woke: A";
    case services::WakeReason::kButtonB:  return "woke: B";
    default:                              return "woke: ?";
  }
}

const char* eventLabel(input::Event e) {
  switch (e) {
    case input::Event::kClick:       return "click";
    case input::Event::kLongPress:   return "long";
    default:                         return "-";
  }
}

}  // namespace

void SelfTest::onEnter(ui::Router& router) {
  batteryPercent_ = power::batteryPercent();
  Serial.printf("[selftest] %s\n", wakeLabel(services::wakeReason()));
  Serial.println("[selftest] press each button; long-press cycles orientation");
}

void SelfTest::onTick(ui::Router& router) {
  uptimeSec_++;
  if (uptimeSec_ % 30 == 0) batteryPercent_ = power::batteryPercent();
}

void SelfTest::draw(gfx::Canvas& canvas) {
  const int y0 = ui::widgets::header(canvas, "Atomic Note");
  const int right = ui::widgets::contentRight(canvas);

  int y = y0 + 12;
  const int x = ui::theme::kMargin;
  char line[40];

  // Which logical button fired last — press the physical TOP button and see
  // which of the two tabs lights up. That settles the A/B mapping.
  if (pressCount_ > 0) {
    snprintf(line, sizeof(line), "%s %s",
             lastButton_ == input::Button::kA ? "A" : "B",
             eventLabel(lastEvent_));
    canvas.text(x, y, line, gfx::Font::kTitle, gfx::kBlack);
  } else {
    canvas.text(x, y, "press a key", gfx::Font::kTitle, gfx::kBlack);
  }
  y += canvas.lineHeight(gfx::Font::kTitle) + 10;

  snprintf(line, sizeof(line), "batt %d%%", batteryPercent_);
  canvas.textElided(x, y, right - x, batteryPercent_ >= 0 ? line : "batt n/a",
                    gfx::Font::kBody, gfx::kBlack);
  y += 20;

  snprintf(line, sizeof(line), "up %lus", (unsigned long)uptimeSec_);
  canvas.text(x, y, line, gfx::Font::kBody, gfx::kBlack);
  y += 20;

  snprintf(line, sizeof(line), "presses %d", pressCount_);
  canvas.text(x, y, line, gfx::Font::kBody, gfx::kBlack);

  // Hint tabs on the right edge, at the physical button positions.
  const bool aActive = pressCount_ > 0 && lastButton_ == input::Button::kA;
  const bool bActive = pressCount_ > 0 && lastButton_ == input::Button::kB;
  ui::widgets::buttonMarker(canvas, ui::widgets::Slot::kTop, aActive);
  ui::widgets::buttonMarker(canvas, ui::widgets::Slot::kBottom, bActive);
}

bool SelfTest::onEvent(ui::Router& router, input::Button button,
                       input::Event event) {
  pressCount_++;
  lastButton_ = button;
  lastEvent_ = event;
  Serial.printf("[selftest] %s %s\n", button == input::Button::kA ? "A" : "B",
                eventLabel(event));

  // Long press stays available as an escape hatch if the panel ever comes up
  // in the wrong orientation again.
  if (event == input::Event::kLongPress) {
    orientationIndex_ = (orientationIndex_ + 1) % epaper::kOrientationCount;
    epaper::setOrientation(epaper::kOrientations[orientationIndex_]);
    Serial.printf("[selftest] orientation -> %d\n", orientationIndex_);
    router.invalidate(epaper::Refresh::kFull);
    return true;
  }

  router.invalidate();
  return true;
}

}  // namespace apps
