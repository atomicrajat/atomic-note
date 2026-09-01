// The button map, on the device.
//
// Two unlabelled buttons cannot explain themselves, and a device you have to
// remember the manual for is a device you put down. One screen, always
// reachable from the menu.
#pragma once

#include "../ui/screen.h"

namespace apps {

class Help : public ui::Screen {
 public:
  const char* name() const override { return "Buttons"; }

  void onEnter(ui::Router& router) override;
  void draw(gfx::Canvas& canvas) override;
};

}  // namespace apps
