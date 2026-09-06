// A full-screen "do not disturb" sign.
//
// This is the feature e-paper is genuinely best at: once the banner is on the
// glass it costs nothing to keep it there, so the device shows it for hours on
// a desk with the radio off and the CPU asleep.
//
// Pick a message with A, commit with B. Committing sleeps the device with the
// banner still displayed; any button wakes it back to the picker.
#pragma once

#include "../ui/screen.h"

namespace apps {

class StatusBanner : public ui::Screen {
 public:
  const char* name() const override { return "Status"; }

  void onEnter(ui::Router& router) override;
  void draw(gfx::Canvas& canvas) override;
  bool onEvent(ui::Router& router, input::Button button,
               input::Event event) override;

  // The sign sleeps like the dashboard does, and keeps itself on the glass
  // when it goes. Holding an image costs nothing on e-paper, so a sign left
  // on a desk stays readable for as long as the battery lasts.
  Idle idlePolicy() const override { return Idle::kSleep; }
  bool onSleep(ui::Router& router) override;

 private:
  int cursor_ = 0;
  bool committed_ = false;
};

}  // namespace apps
