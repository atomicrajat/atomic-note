// The screen a reminder wakes you with.
//
// Deliberately loud and deliberately modal: inverted, full bleed, no chrome
// except what is needed to dismiss it. A reminder that looks like every other
// screen is a reminder you scroll past.
#pragma once

#include "../ui/screen.h"

namespace apps {

class ReminderAlert : public ui::Screen {
 public:
  const char* name() const override { return "Reminder"; }

  // Which reminder to show. Set before the screen is pushed.
  void setIndex(int index) { index_ = index; }

  void onEnter(ui::Router& router) override;
  void draw(gfx::Canvas& canvas) override;
  bool onEvent(ui::Router& router, input::Button button,
               input::Event event) override;

  // An alert nobody has acknowledged must not sleep itself away.
  bool blocksSleep() const override { return true; }

 private:
  int index_ = -1;
};

}  // namespace apps
