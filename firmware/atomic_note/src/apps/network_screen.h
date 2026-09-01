// WiFi status and setup.
//
// Controls:
//   tap A    connect (or open the setup portal, if nothing is saved)
//   tap B    open the setup portal
//   hold A   back — also turns the radio off on the way out
#pragma once

#include "../ui/screen.h"

namespace apps {

class NetworkScreen : public ui::Screen {
 public:
  const char* name() const override { return "WiFi"; }

  void onEnter(ui::Router& router) override;
  void onExit() override;
  void draw(gfx::Canvas& canvas) override;
  bool onEvent(ui::Router& router, input::Button button,
               input::Event event) override;

  uint32_t tickIntervalMs() const override { return 1000; }
  void onTick(ui::Router& router) override;

  // Connecting and serving the portal both need the loop running.
  bool blocksSleep() const override;

 private:
  int shownState_ = -1;
  bool syncedThisVisit_ = false;
};

}  // namespace apps
