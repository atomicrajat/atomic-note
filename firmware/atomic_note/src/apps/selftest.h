// Phase 0 bring-up screen.
//
// Exists to prove the whole pipeline end to end on real hardware: panel init,
// partial vs full refresh, font rendering, the button state machine, battery
// sense, and the wake-reason path. It gets deleted once the dashboard lands.
#pragma once

#include "../ui/screen.h"

namespace apps {

class SelfTest : public ui::Screen {
 public:
  const char* name() const override { return "selftest"; }

  void onEnter(ui::Router& router) override;
  void draw(gfx::Canvas& canvas) override;
  bool onEvent(ui::Router& router, input::Button button,
               input::Event event) override;
  uint32_t tickIntervalMs() const override { return 1000; }
  void onTick(ui::Router& router) override;

  // Bring-up only: sleeping drops the native USB CDC, which makes the board
  // vanish from /dev mid-session and blocks reflashing. The dashboard that
  // replaces this screen will sleep normally.
  bool blocksSleep() const override { return true; }

 private:
  int orientationIndex_ = 3;  // matches epaper::kDefaultOrientation
  int pressCount_ = 0;
  input::Button lastButton_ = input::Button::kA;
  input::Event lastEvent_ = input::Event::kNone;
  int batteryPercent_ = -1;
  uint32_t uptimeSec_ = 0;
};

}  // namespace apps
