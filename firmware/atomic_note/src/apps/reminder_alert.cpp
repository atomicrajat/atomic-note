#include "reminder_alert.h"

#include <Arduino.h>

#include "../audio/sound.h"
#include "../board/haptics.h"
#include "../display/epaper.h"
#include "../services/reminders.h"
#include "../services/rtc.h"
#include "../services/sleep.h"
#include "../ui/router.h"
#include "../ui/widgets.h"

namespace apps {

namespace widgets = ui::widgets;
namespace reminders = services::reminders;

void ReminderAlert::onEnter(ui::Router& router) {
  router.invalidate(epaper::Refresh::kFull);
  // Paint first, then sound: the panel takes ~500 ms, and a device that beeps
  // at a blank screen is worse than one that beeps a moment late.
  router.flushPendingPaint();
  // Both channels, and neither is gated on its preference: a reminder is
  // the one thing you asked to be interrupted by.
  haptics::alert();
  audio::sound::alert();
}

void ReminderAlert::draw(gfx::Canvas& canvas) {
  const int w = canvas.width();
  const int h = canvas.height();

  const reminders::Reminder* r = reminders::at(index_);
  if (!r) {
    canvas.textInBox(0, 90, w, 16, "No reminder", gfx::Font::kLabel,
                     gfx::kBlack);
    widgets::buttonMarkers(canvas);
    return;
  }

  // Inverted, so it is unmistakably different from every other screen.
  canvas.fill(gfx::kBlack);
  widgets::hudFrame(canvas, 3, 3, w - 6, h - 6, 15, 2, gfx::kWhite);

  // The scheduled time, small, at the top.
  char when[12];
  int hour12 = r->hour % 12;
  if (hour12 == 0) hour12 = 12;
  snprintf(when, sizeof(when), "%d:%02d %s", hour12, r->minute,
           r->hour < 12 ? "AM" : "PM");
  canvas.textInBox(0, 22, w, 14, when, gfx::Font::kLabel, gfx::kWhite);
  canvas.rect(40, 44, w - 80, 2, gfx::kWhite);

  // The message gets the middle of the screen and wraps. Three lines at this
  // size holds the 48 characters a reminder can carry.
  canvas.textWrapped(22, 62, w - 44, 3, r->text, gfx::Font::kTitle,
                     gfx::kWhite);

  canvas.textInBox(0, h - 40, w, 14, "Press to dismiss", gfx::Font::kMicro,
                   gfx::kWhite, gfx::Align::kCenter, 2);

  widgets::buttonMarkers(canvas);
}

bool ReminderAlert::onEvent(ui::Router& router, input::Button button,
                            input::Event event) {
  if (event != input::Event::kClick) return false;

  // Either button dismisses. Someone reaching for a beeping device should not
  // have to remember which button — and there is only one thing to do here.
  reminders::markFired(index_);

  if (router.canGoBack()) {
    router.back();
  } else {
    // Woken purely for this: acknowledge and go straight back to sleep, so the
    // device does not sit awake on a desk at 3am.
    services::enterDeepSleep();
  }
  return true;
}

}  // namespace apps
