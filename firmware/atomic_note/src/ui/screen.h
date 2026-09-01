// The one abstraction every feature implements.
//
// Screens never touch the panel directly and never call refresh themselves.
// They draw into a Canvas when asked and call `invalidate()` when their content
// changed. The Router decides when to actually push pixels, which keeps e-paper
// refreshes proportional to real changes rather than to loop iterations.
#pragma once

#include <stdint.h>

#include "../display/canvas.h"
#include "../input/buttons.h"

namespace ui {

class Router;

class Screen {
 public:
  virtual ~Screen() = default;

  // Short name for logs and for the router's back-stack.
  virtual const char* name() const = 0;

  // Called when this screen becomes visible, and again when returning to it
  // from a screen that was pushed on top.
  virtual void onEnter(Router& router) {}
  virtual void onExit() {}

  virtual void draw(gfx::Canvas& canvas) = 0;

  // Return true if the event was consumed. Unconsumed long-presses on B are
  // treated as "back" by the router.
  virtual bool onEvent(Router& router, input::Button button,
                       input::Event event) {
    return false;
  }

  // Periodic work. Return the desired interval in ms, or 0 for no ticking.
  // A ticking screen still only repaints if it calls invalidate().
  virtual uint32_t tickIntervalMs() const { return 0; }
  virtual void onTick(Router& router) {}

  // Screens that must stay awake (recording, timers, a live web server)
  // override this so the idle timer never sleeps the device under them.
  virtual bool blocksSleep() const { return false; }
};

}  // namespace ui
