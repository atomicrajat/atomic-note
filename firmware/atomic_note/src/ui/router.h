// Screen stack, repaint scheduling, and the idle-sleep timer.
#pragma once

#include <stdint.h>

#include "../display/canvas.h"
#include "../display/epaper.h"
#include "screen.h"

namespace ui {

// Deep-sleep after this long with no input, unless the active screen blocks it.
//
// This is the single biggest lever on battery life, because awake current is
// thousands of times sleep current — every second here is paid on every
// interaction, all day. It is not set lower than this because waking resets
// the chip and returns to the dashboard: sleeping under someone who is still
// reading a long answer would lose their place, which is worse than the
// milliamps are worth.
constexpr uint32_t kIdleSleepMs = 60000;

class Router {
 public:
  Router(gfx::Canvas& canvas) : canvas_(canvas) {}

  void begin(Screen* root);

  // Replace the current screen, keeping the back-stack intact.
  void go(Screen* screen);
  // Push onto the back-stack — `back()` returns here.
  void push(Screen* screen);
  // Pop to the previous screen. No-op at the root.
  void back();
  bool canGoBack() const { return depth_ > 0; }

  // Mark the current screen as needing a repaint.
  void invalidate(epaper::Refresh mode = epaper::Refresh::kAuto);

  // Reset the idle timer. Input does this automatically; long-running work
  // should call it too so the device does not sleep mid-task.
  void keepAwake();

  // Drive one iteration: input, ticks, repaint, sleep check.
  void update();

  // Paint right now if anything is pending, instead of waiting for the next
  // update(). Needed by screens that sleep or block immediately after asking
  // for a repaint — otherwise the frame never reaches the glass.
  void flushPendingPaint() { repaintIfNeeded(); }

  Screen* current() { return current_; }
  gfx::Canvas& canvas() { return canvas_; }

 private:
  void enter(Screen* screen);
  void repaintIfNeeded();
  void sleepIfIdle();

  static constexpr int kMaxDepth = 6;

  gfx::Canvas& canvas_;
  Screen* current_ = nullptr;
  Screen* stack_[kMaxDepth] = {};
  int depth_ = 0;

  bool dirty_ = true;
  epaper::Refresh pendingMode_ = epaper::Refresh::kFull;
  uint32_t lastInputMs_ = 0;
  uint32_t lastTickMs_ = 0;
};

}  // namespace ui
