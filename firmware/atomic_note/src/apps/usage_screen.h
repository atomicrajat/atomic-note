// Claude Code usage, as a week of bars.
//
// Requires the network, so it connects on entry and drops the radio on the way
// out unless it was already up.
#pragma once

#include "../services/claude_usage.h"
#include "../ui/screen.h"

namespace apps {

class UsageScreen : public ui::Screen {
 public:
  const char* name() const override { return "Claude"; }

  void onEnter(ui::Router& router) override;
  void onExit() override;
  void draw(gfx::Canvas& canvas) override;
  bool onEvent(ui::Router& router, input::Button button,
               input::Event event) override;

  uint32_t tickIntervalMs() const override { return 400; }
  void onTick(ui::Router& router) override;

  bool blocksSleep() const override { return phase_ != Phase::kReady; }

 private:
  enum class Phase : uint8_t { kConnecting, kFetching, kReady };

  Phase phase_ = Phase::kConnecting;
  bool radioWasUp_ = false;
  bool showingWeekBars_ = false;  // B toggles limits <-> daily history
  services::claude_usage::Snapshot data_;
  services::claude_usage::Limits limits_;

  void drawLimits(gfx::Canvas& canvas);
  void drawHistory(gfx::Canvas& canvas);
};

}  // namespace apps
