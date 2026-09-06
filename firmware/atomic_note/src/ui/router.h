// Screen stack, repaint scheduling, and the idle-sleep timer.
#pragma once

#include <stdint.h>

#include "../display/canvas.h"
#include "../display/epaper.h"
#include "screen.h"

namespace ui {

// Deep-sleep after this long with no input, on a screen whose idle policy is
// kSleep — in practice the dashboard and the status sign.
//
// This is the single biggest lever on battery life, because awake current is
// thousands of times sleep current — every second here is paid on every
// interaction, all day. It is not set lower than this because waking resets
// the chip: sleeping under someone who is still reading would lose their
// place, which is worse than the milliamps are worth.
constexpr uint32_t kIdleSleepMs = 60000;

// On every other screen — menus, apps, results — the idle timer does not sleep
// the device at all. It waits this much longer and then puts the dashboard
// back, which sleeps on its own timer a minute after that.
//
// The split exists because the old single timer sat under the apps too, and a
// minute is nothing while you are reading a note, picking a tag or watching a
// distance reading settle. Long enough that no real use of an app reaches it;
// short enough that a device left face-up on a desk still ends up asleep
// rather than awake all night.
constexpr uint32_t kIdleReturnMs = 300000;

class Router {
 public:
  Router(gfx::Canvas& canvas) : canvas_(canvas) {}

  // `home` is where the idle timer falls back to, and defaults to the root.
  // They differ on a reminder wake, where the device starts on the alert but
  // still belongs back at the dashboard once it is left alone.
  void begin(Screen* root, Screen* home = nullptr);

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
  Screen* home_ = nullptr;
  Screen* stack_[kMaxDepth] = {};
  int depth_ = 0;

  bool dirty_ = true;
  epaper::Refresh pendingMode_ = epaper::Refresh::kFull;
  uint32_t lastInputMs_ = 0;
  uint32_t lastTickMs_ = 0;
};

}  // namespace ui
