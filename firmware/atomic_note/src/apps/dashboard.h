// The home screen: time, date, temperature, humidity, battery.
//
// This is what the device shows at rest, and what it wakes into. Because
// e-paper holds its image with no power, the dashboard is still readable while
// the device is in deep sleep — so it is a clock even when it is "off".
#pragma once

#include "../services/environment.h"
#include "../ui/screen.h"

namespace apps {

class Dashboard : public ui::Screen {
 public:
  const char* name() const override { return "dashboard"; }

  // The dashboard is the root screen, so it cannot go "back" to reach the
  // menu — it has to push it. Injected rather than included to keep the app
  // layer free of references to specific other screens.
  void setMenu(ui::Screen* menu) { menu_ = menu; }

  // Hold A anywhere on the clock starts a voice note. The shortcut exists
  // because a thought worth recording does not survive three menu levels.
  void setRecorder(ui::Screen* recorder) { recorder_ = recorder; }

  void onEnter(ui::Router& router) override;
  void draw(gfx::Canvas& canvas) override;
  bool onEvent(ui::Router& router, input::Button button,
               input::Event event) override;

  // Tick every 5s. The screen only repaints when the displayed minute actually
  // changes, so this costs almost nothing.
  uint32_t tickIntervalMs() const override { return 5000; }
  void onTick(ui::Router& router) override;

 private:
  void refreshSensors();

  ui::Screen* menu_ = nullptr;
  ui::Screen* recorder_ = nullptr;
  services::environment::Reading env_ = {};
  int batteryPercent_ = -1;
  int shownMinute_ = -1;   // -1 forces the first paint
  uint32_t lastSensorMs_ = 0;
  bool clockSet_ = false;
  bool shownWifi_ = false;
};

}  // namespace apps
