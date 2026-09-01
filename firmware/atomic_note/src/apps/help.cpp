#include "help.h"

#include <Arduino.h>

#include "../display/epaper.h"
#include "../ui/router.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"

namespace apps {

namespace widgets = ui::widgets;
namespace theme = ui::theme;

namespace {

struct Row {
  const char* gesture;
  const char* meaning;
};

// Only the two that line up with the physical buttons. Back is drawn
// separately below, because it applies on every screen rather than to one
// button's row.
constexpr Row kRows[] = {
    {"Tap A", "Previous"},
    {"Tap B", "Next"},
    {"Hold A", "Select"},
    {"Hold B", "Back"},
};
constexpr int kRowCount = sizeof(kRows) / sizeof(kRows[0]);

}  // namespace

void Help::onEnter(ui::Router& router) {
  router.invalidate(epaper::Refresh::kFull);
}

void Help::draw(gfx::Canvas& canvas) {
  const int left = theme::kMargin;
  const int right = canvas.width() - theme::kMargin - 6;

  widgets::hudFrame(canvas, 3, 3, canvas.width() - 6, canvas.height() - 6, 15,
                    2);

  canvas.text(left, 12, "Buttons", gfx::Font::kLabel, gfx::kBlack);
  const int titleW = canvas.textWidth("Buttons", gfx::Font::kLabel);
  const int ruleX = left + titleW + 8;
  if (right > ruleX) {
    canvas.rect(ruleX, 18, right - ruleX, 4, gfx::kBlack);
  }

  // Four gestures, four rows, in the order you learn them: move first, then
  // commit, then leave. The whole device is these four and nothing else.
  const int firstY = 44;
  const int rowH = 33;
  for (int i = 0; i < kRowCount; i++) {
    const int y = firstY + i * rowH;
    canvas.text(left, y, kRows[i].gesture, gfx::Font::kBody, gfx::kBlack);
    canvas.textInBox(left, y, right - left, 12, kRows[i].meaning,
                     gfx::Font::kLabel, gfx::kBlack, gfx::Align::kRight);
    if (i < kRowCount - 1) {
      canvas.rect(left, y + 20, right - left, 1, gfx::kBlack);
    }
  }

  canvas.textInBox(0, canvas.height() - 26, canvas.width(), 10,
                   "HOLD A ON CLOCK RECORDS", gfx::Font::kMicro, gfx::kBlack,
                   gfx::Align::kCenter, 2);

  widgets::buttonMarkers(canvas);
}

}  // namespace apps
