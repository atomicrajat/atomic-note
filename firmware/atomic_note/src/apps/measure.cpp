#include "measure.h"

#include <Arduino.h>

#include "../audio/sound.h"
#include "../board/haptics.h"
#include "../display/epaper.h"
#include "../services/range.h"
#include "../ui/router.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"

namespace apps {

namespace widgets = ui::widgets;
namespace theme = ui::theme;
namespace range = services::range;

namespace {

// Below this the reading is treated as unchanged.
//
// Set from measured noise, not from the datasheet. Pointed at a fixed target
// about 11 cm away, this sensor returned 104-114 mm — a 10 mm spread with
// nothing moving. A 5 mm threshold therefore repaints constantly on a still
// target, which on e-paper is half a second of flashing every time. 12 mm sits
// just outside the noise: a stationary reading holds, and real movement of a
// centimetre or more updates immediately.
constexpr uint16_t kChangeMm = 12;

const char* unitName(int unit) {
  switch (unit) {
    case 0: return "MM";
    case 1: return "CM";
    case 2: return "INCH";
    default: return "FEET";
  }
}

// Formatted from the stored millimetres every time, so switching units is a
// conversion rather than a re-measurement.
void formatValue(char* out, size_t size, uint16_t mm, int unit) {
  switch (unit) {
    case 0:
      snprintf(out, size, "%u", (unsigned)mm);
      return;
    case 1:
      snprintf(out, size, "%.1f", mm / 10.0f);
      return;
    case 2:
      snprintf(out, size, "%.2f", mm / 25.4f);
      return;
    default:
      snprintf(out, size, "%.2f", mm / 304.8f);
      return;
  }
}

}  // namespace

void Measure::onEnter(ui::Router& router) {
  held_ = false;
  hasReading_ = false;
  mm_ = 0;
  shownMm_ = 0;
  router.invalidate(epaper::Refresh::kFull);
}

void Measure::onTick(ui::Router& router) {
  if (held_ || !range::available()) return;

  const uint16_t mm = range::readMm();
  if (mm == 0) {
    // Out of range. Say so rather than holding the last number, which would
    // read as a measurement of whatever the device is now pointing at.
    if (hasReading_) {
      hasReading_ = false;
      router.invalidate(epaper::Refresh::kPartial);
    }
    return;
  }

  const bool first = !hasReading_;
  mm_ = mm;
  hasReading_ = true;

  if (first || (uint16_t)abs((int)mm - (int)shownMm_) >= kChangeMm) {
    shownMm_ = mm;
    router.invalidate(epaper::Refresh::kPartial);
  }
}

void Measure::draw(gfx::Canvas& canvas) {
  const int w = canvas.width();
  widgets::hudFrame(canvas, 3, 3, w - 6, canvas.height() - 6, 15, 2);

  if (!range::available()) {
    canvas.textInBox(0, 78, w, 18, "No range sensor", gfx::Font::kTitle,
                     gfx::kBlack);
    canvas.textInBox(0, 106, w, 12, "CONNECT A VL53L0X", gfx::Font::kMicro,
                     gfx::kBlack, gfx::Align::kCenter, 2);
    widgets::buttonMarkers(canvas);
    return;
  }

  canvas.text(theme::kMargin, 12, "Measure", gfx::Font::kLabel, gfx::kBlack);
  const int titleW = canvas.textWidth("Measure", gfx::Font::kLabel);
  canvas.rect(theme::kMargin + titleW + 8, 18,
              w - 2 * theme::kMargin - titleW - 8, 4, gfx::kBlack);

  // HELD is stated in an inverted chip, so a frozen number is never mistaken
  // for a live one — that is the only way to misread this screen badly.
  if (held_) {
    const char* text = "HELD";
    const int tw = canvas.textWidth(text, gfx::Font::kMicro, 2);
    const int bw = tw + 16;
    canvas.roundRect((w - bw) / 2, 36, bw, 15, 4, gfx::kBlack);
    canvas.textInBox((w - bw) / 2, 40, bw, 8, text, gfx::Font::kMicro,
                     gfx::kWhite, gfx::Align::kCenter, 2);
  } else {
    canvas.textInBox(0, 38, w, 10, "LIVE", gfx::Font::kMicro, gfx::kBlack,
                     gfx::Align::kCenter, 2);
  }

  if (!hasReading_) {
    canvas.textInBox(0, 92, w, 18, "Out of range", gfx::Font::kTitle,
                     gfx::kBlack);
    canvas.textInBox(0, 120, w, 10, "POINT AT SOMETHING CLOSER",
                     gfx::Font::kMicro, gfx::kBlack, gfx::Align::kCenter, 2);
  } else {
    char value[16];
    formatValue(value, sizeof(value), mm_, (int)unit_);
    canvas.textInBox(0, 74, w, 34, value, gfx::Font::kDisplay, gfx::kBlack);
    canvas.textInBox(0, 116, w, 12, unitName((int)unit_), gfx::Font::kMicro,
                     gfx::kBlack, gfx::Align::kCenter, 2);
  }

  canvas.textInBox(0, canvas.height() - 20, w, 10,
                   held_ ? "HOLD A LIVE   TAP UNITS"
                         : "HOLD A FREEZE   TAP UNITS",
                   gfx::Font::kMicro, gfx::kBlack, gfx::Align::kCenter, 2);
  widgets::buttonMarkers(canvas);
}

void Measure::step(ui::Router& router) {
  unit_ = (Unit)(((int)unit_ + 1) % (int)Unit::kCount);
  router.invalidate(epaper::Refresh::kPartial);
}

bool Measure::onEvent(ui::Router& router, input::Button button,
                      input::Event event) {
  if (!range::available()) return false;

  // A tap changes units, in either state. Converting a held reading is the
  // common case — you measure something, freeze it, then want it in inches.
  if (event == input::Event::kClick) {
    step(router);
    return true;
  }

  // Hold A freezes, and freezes again to resume. One gesture for both, because
  // it is one idea: stop, or start again.
  if (button == input::Button::kA && event == input::Event::kLongPress) {
    held_ = !held_;
    if (!held_) {
      // Resuming takes a fresh reading rather than showing the stale one.
      hasReading_ = false;
      shownMm_ = 0;
    }
    haptics::bump();
    audio::sound::select();
    router.invalidate(epaper::Refresh::kFull);
    return true;
  }

  return false;  // hold B falls through to the Router for Back
}

}  // namespace apps
