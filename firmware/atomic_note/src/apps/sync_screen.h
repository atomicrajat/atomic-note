// Fetching transcripts for everything that is waiting.
//
// Mirrors the reference firmware's sync flow: connect, set the clock, work
// through every note without a transcript showing progress, then drop the
// radio. Re-running it is safe — notes that already have text are skipped, so
// a failed run can simply be repeated.
#pragma once

#include "../ui/screen.h"

namespace apps {

class SyncScreen : public ui::Screen {
 public:
  const char* name() const override { return "Sync"; }

  void onEnter(ui::Router& router) override;
  void onExit() override;
  void draw(gfx::Canvas& canvas) override;
  bool onEvent(ui::Router& router, input::Button button,
               input::Event event) override;

  uint32_t tickIntervalMs() const override { return 300; }
  void onTick(ui::Router& router) override;

  bool blocksSleep() const override { return phase_ != Phase::kDone; }

 private:
  enum class Phase : uint8_t { kConnecting, kWorking, kDone };

  Phase phase_ = Phase::kConnecting;
  int total_ = 0;
  int done_ = 0;
  int failed_ = 0;
  bool radioWasUp_ = false;
  String message_;
};

}  // namespace apps
