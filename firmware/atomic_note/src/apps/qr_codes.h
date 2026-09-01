// Scannable links.
//
// Holding the device up for someone to scan is the whole interaction, so the
// code gets the entire screen and everything else is kept out of its way.
//
// URLs live in links.h for now; phase 5 moves them into settings so they are
// editable from the web app without reflashing.
#pragma once

#include "../ui/screen.h"

namespace apps {

class QrCodes : public ui::Screen {
 public:
  const char* name() const override { return "Links"; }

  void onEnter(ui::Router& router) override;
  void draw(gfx::Canvas& canvas) override;
  bool onEvent(ui::Router& router, input::Button button,
               input::Event event) override;

 private:
  int cursor_ = 0;
};

}  // namespace apps
