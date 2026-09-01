// A month at a glance.
//
// Controls:
//   tap B    next month
//   hold B   previous month
//   tap A    jump back to today
//   hold A   back
#pragma once

#include "../ui/screen.h"

namespace apps {

class Calendar : public ui::Screen {
 public:
  const char* name() const override { return "Calendar"; }

  void onEnter(ui::Router& router) override;
  void draw(gfx::Canvas& canvas) override;
  bool onEvent(ui::Router& router, input::Button button,
               input::Event event) override;

 private:
  void goToToday();
  void shiftMonth(int delta);

  int year_ = 0;   // 0 means "not initialised yet"
  int month_ = 0;  // 1-12
};

}  // namespace apps
