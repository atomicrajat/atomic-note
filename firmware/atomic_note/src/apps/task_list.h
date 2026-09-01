// Read and triage tasks.
//
// Creating tasks needs text, which two buttons cannot provide — that arrives
// with the web app in phase 5. What this screen does is the half that belongs
// in your hand: see what is outstanding, tick things off, clear the done ones.
//
// Controls:
//   tap B    next task
//   hold B   previous task
//   tap A    toggle done
//   hold A   back
#pragma once

#include "../ui/screen.h"

namespace apps {

class TaskList : public ui::Screen {
 public:
  const char* name() const override { return "Tasks"; }

  void onEnter(ui::Router& router) override;
  void draw(gfx::Canvas& canvas) override;
  bool onEvent(ui::Router& router, input::Button button,
               input::Event event) override;

 private:
  static constexpr int kRowsPerPage = 4;

  int cursor_ = 0;
};

}  // namespace apps
