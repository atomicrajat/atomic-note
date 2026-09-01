// A focus timer.
//
// Counts in whole minutes, because a per-second display would mean a panel
// refresh every second — visible flicker, accumulated ghosting, and a battery
// cost out of all proportion to the information. The remaining minute is shown
// as a draining ring instead, which conveys progress without redrawing
// continuously.
//
// Controls:
//   tap A    start / pause / resume  (acknowledge, when finished)
//   tap B    change length (idle) or reset (running, paused, finished)
//   hold B   previous length, when idle
//   hold A   back
#pragma once

#include "../ui/screen.h"

namespace apps {

class Pomodoro : public ui::Screen {
 public:
  const char* name() const override { return "Focus"; }

  void onEnter(ui::Router& router) override;
  void draw(gfx::Canvas& canvas) override;
  bool onEvent(ui::Router& router, input::Button button,
               input::Event event) override;

  uint32_t tickIntervalMs() const override { return 1000; }
  void onTick(ui::Router& router) override;

  // A running timer must not be slept out from under the user. A paused one
  // may sleep — it is holding a number, not counting.
  bool blocksSleep() const override { return state_ == State::kRunning; }

 private:
  enum class State : uint8_t { kIdle, kRunning, kPaused, kDone };

  int presetIndex_ = 0;
  State state_ = State::kIdle;

  // Running: the deadline. Paused: the seconds that were left when it stopped.
  // Keeping both means pausing loses nothing to rounding.
  uint32_t endsAtMs_ = 0;
  int pausedRemainingSec_ = 0;

  int shownMinutes_ = -1;
  uint32_t lastPaintMs_ = 0;
  bool heartbeat_ = false;  // flips each repaint: proof the timer is live

  int remainingSeconds() const;
  void reset();
};

}  // namespace apps
