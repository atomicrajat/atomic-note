#include "router.h"

#include "sleep_face.h"

#include <Arduino.h>

#include "../audio/sound.h"
#include "../board/haptics.h"
#include "../config.h"
#include "../services/sleep.h"
#include "widgets.h"

namespace ui {

void Router::begin(Screen* root) {
  depth_ = 0;
  lastInputMs_ = millis();
  enter(root);
}

void Router::enter(Screen* screen) {
  if (!screen) return;
  if (current_) current_->onExit();
  current_ = screen;
  current_->onEnter(*this);
  dirty_ = true;
  lastTickMs_ = millis();
}

void Router::go(Screen* screen) { enter(screen); }

void Router::push(Screen* screen) {
  if (!screen) return;
  if (depth_ < kMaxDepth && current_) {
    stack_[depth_++] = current_;
  } else if (current_) {
    Serial.println("[router] back-stack full, replacing instead of pushing");
  }
  enter(screen);
}

void Router::back() {
  if (depth_ <= 0) return;
  enter(stack_[--depth_]);
}

void Router::invalidate(epaper::Refresh mode) {
  dirty_ = true;
  // A forced full refresh outranks a pending partial one.
  if (mode == epaper::Refresh::kFull) pendingMode_ = mode;
}

void Router::keepAwake() { lastInputMs_ = millis(); }

void Router::update() {
  if (!current_) return;

  // Drain the queue — several events can land between updates, and dropping
  // one strands the user on a screen that ignored the press.
  for (;;) {
    const input::Reading r = input::poll();
    if (r.event == input::Event::kNone) break;

    keepAwake();

    // Feedback happens HERE, before the screen handles the press. A panel
    // refresh takes ~500 ms, so a sound played after the handler would arrive
    // long after the finger left the button and read as lag.
    // The four gestures each get their own voice: confirming and going back
    // are the two that change where you are, so they must not sound alike.
    // Haptics first, then sound. A cue plays for up to 60 ms, and feedback
    // that lands after it has finished no longer reads as the same event.
    // Each channel decides for itself whether it is switched on.
    if (r.event == input::Event::kLongPress) {
      if (r.button == input::Button::kA) {
        haptics::bump();
        audio::sound::select();
      } else {
        haptics::back();
        audio::sound::back();
      }
    } else {
      haptics::tick();
      audio::sound::click();
    }

    const bool consumed = current_->onEvent(*this, r.button, r.event);
    if (consumed) continue;

    // BACK is a held B, on every screen — handled here rather than per-screen
    // so it cannot drift.
    //
    // Only B. Hold A is SELECT now, and letting an unclaimed hold-A back out
    // as well would mean a screen that forgot to handle its own confirm
    // gesture silently throws the user out of itself instead.
    if (r.button == input::Button::kB && r.event == input::Event::kLongPress &&
        canGoBack()) {
      back();
    }
  }

  const uint32_t interval = current_->tickIntervalMs();
  if (interval > 0 && millis() - lastTickMs_ >= interval) {
    lastTickMs_ = millis();
    current_->onTick(*this);
  }

  repaintIfNeeded();
  sleepIfIdle();
}

void Router::repaintIfNeeded() {
  if (!dirty_) return;
  dirty_ = false;

  canvas_.fill(gfx::kWhite);
  current_->draw(canvas_);
  // The case rounds the visible area — carve the corners so full-bleed
  // elements do not fight the plastic. Screens never handle this themselves.
  widgets::maskScreenCorners(canvas_);

  const uint32_t startMs = millis();
  epaper::present(canvas_, pendingMode_);
  if (config::kDevVerbose) {
    Serial.printf("[paint] %s in %lums\n", current_->name(),
                  (unsigned long)(millis() - startMs));
  }
  pendingMode_ = epaper::Refresh::kAuto;
}

void Router::sleepIfIdle() {
  if (config::kDevKeepAwake) return;
  if (current_->blocksSleep()) {
    keepAwake();
    return;
  }
  if (millis() - lastInputMs_ < kIdleSleepMs) return;

  // Leave the panel showing something that says "asleep, not broken". The
  // refresh costs about 1.4 s of panel current once; the image then holds for
  // the entire sleep at no cost at all.
  drawSleepFace(canvas_);
  epaper::present(canvas_, epaper::Refresh::kFull);

  services::enterDeepSleep();
}

}  // namespace ui
