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

  // What the idle timer is allowed to do while this screen is up.
  //
  // Deep sleep is the whole battery story, but sleeping under someone who is
  // still using the device costs them their place, because waking resets the
  // chip and comes back at the dashboard. So only the screens whose job is to
  // sit there being looked at go down directly: the dashboard, and the status
  // sign. Everything else either holds indefinitely or, after a much longer
  // wait, drops back to the dashboard and sleeps from there — which keeps a
  // device abandoned in a menu from staying awake until the battery is flat.
  enum class Idle : uint8_t {
    kSleep,   // deep sleep after kIdleSleepMs
    kReturn,  // fall back to the dashboard after kIdleReturnMs — the default
    kStay,    // never time out: recording, a running timer, a live web server
  };
  virtual Idle idlePolicy() const { return Idle::kReturn; }

  // Called immediately before the router deep-sleeps under this screen.
  // Return true to leave this screen's own image on the glass instead of the
  // default sleep face. The panel holds it at no cost, so a screen that exists
  // to be read across a room wants that rather than the name of the product.
  virtual bool onSleep(Router& router) { return false; }
};

}  // namespace ui
