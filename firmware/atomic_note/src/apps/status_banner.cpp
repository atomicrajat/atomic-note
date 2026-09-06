#include "status_banner.h"

#include <Arduino.h>

#include "../display/epaper.h"
#include "../services/sleep.h"
#include "../ui/router.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"

namespace apps {

namespace widgets = ui::widgets;

namespace {

// Two lines because one long word shrinks to nothing on a 200px panel, and a
// banner that cannot be read across a room is not doing its job.
struct Banner {
  const char* top;
  const char* bottom;
  bool inverted;  // the loud ones are white-on-black
};

constexpr Banner kBanners[] = {
    {"DO NOT", "DISTURB", true},
    {"IN A", "MEETING", true},
    {"HEADS", "DOWN", false},
    {"OPEN TO", "TALK", false},
    {"BACK", "SOON", false},
};
constexpr int kBannerCount = sizeof(kBanners) / sizeof(kBanners[0]);

}  // namespace

void StatusBanner::onEnter(ui::Router& router) {
  committed_ = false;
  router.invalidate(epaper::Refresh::kFull);
}

void StatusBanner::draw(gfx::Canvas& canvas) {
  const Banner& banner = kBanners[cursor_];
  const int w = canvas.width();
  const int h = canvas.height();

  if (banner.inverted) canvas.fill(gfx::kBlack);
  const gfx::Ink ink = banner.inverted ? gfx::kWhite : gfx::kBlack;

  widgets::hudFrame(canvas, 3, 3, w - 6, h - 6, 15, 2, ink);

  // Message, centred as a block rather than two independently centred lines.
  const int lineH = canvas.capHeight(gfx::Font::kDisplay);
  const int blockH = lineH * 2 + 14;
  const int top = (h - blockH) / 2;

  canvas.textInBox(0, top, w, lineH, banner.top, gfx::Font::kDisplay, ink);
  canvas.textInBox(0, top + lineH + 14, w, lineH, banner.bottom,
                   gfx::Font::kDisplay, ink);

  if (!committed_) {
    // Only the picker shows chrome. Once committed the sign is just the sign.
    // No button legend here — the grammar is the same on every screen and the
    // Buttons entry in the menu documents it once.
    const int gap = 10;
    const int startX = (w - (kBannerCount - 1) * gap) / 2;
    for (int i = 0; i < kBannerCount; i++) {
      const int x = startX + i * gap;
      if (i == cursor_) {
        canvas.circle(x, h - 16, 3, ink);
      } else {
        canvas.circleOutline(x, h - 16, 3, 1, ink);
      }
    }
    widgets::buttonMarkers(canvas);
  }
}

bool StatusBanner::onEvent(ui::Router& router, input::Button button,
                           input::Event event) {
  if (event == input::Event::kClick) {
    cursor_ = (cursor_ + (button == input::Button::kB ? 1 : -1) + kBannerCount) %
              kBannerCount;
    // Full refresh: these screens are mostly solid ink, and partial updates
    // leave the previous message ghosted behind the new one.
    router.invalidate(epaper::Refresh::kFull);
    return true;
  }

  if (button == input::Button::kA && event == input::Event::kLongPress) {
    // Commit now rather than waiting out the idle timer. Same ending either
    // way — the router calls onSleep() below when the timer gets there.
    onSleep(router);
    services::enterDeepSleep();
    return true;
  }

  return false;
}

bool StatusBanner::onSleep(ui::Router& router) {
  // Drop the chrome and repaint before the chip goes down, so what the panel
  // holds is the sign itself and not the sign plus a picker nobody is using.
  // Returning true tells the router to leave this on the glass instead of
  // painting the ATOMIC NOTE sleep face over it — which is the whole point of
  // the feature: you set a sign, walk away, and it is still there.
  committed_ = true;
  router.invalidate(epaper::Refresh::kFull);
  router.flushPendingPaint();
  return true;
}

}  // namespace apps
