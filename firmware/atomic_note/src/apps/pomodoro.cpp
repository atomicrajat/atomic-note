#include "pomodoro.h"

#include <Arduino.h>

#include "../audio/sound.h"
#include "../display/epaper.h"
#include "../display/numerals.h"
#include "../ui/router.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"

namespace apps {

namespace widgets = ui::widgets;

namespace {

constexpr int kPresets[] = {25, 45, 15, 5, 90};
constexpr int kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

constexpr int kRingCx = 100;
constexpr int kRingCy = 104;
constexpr int kRingR = 62;
constexpr int kRingThickness = 7;

// How often the ring redraws while running.
//
// The number only changes once a minute, so repainting on minute boundaries
// alone leaves the screen motionless for 59 seconds — which reads as a frozen
// timer. Half a minute moves the ring visibly and still costs only two panel
// refreshes a minute.
//
// Do not lower this toward one second: each refresh is ~500 ms of blocking,
// deposits ghosting, and burns current. The ring is the fine-grained readout
// precisely so the digits do not have to be.
constexpr uint32_t kRunningRepaintMs = 30000;

// Sweep an arc by stamping dots along it. The canvas has no arc primitive, and
// at this radius a 2-degree step leaves no gaps.
void arc(gfx::Canvas& canvas, int cx, int cy, int r, int fromDeg, int toDeg,
         int thickness, gfx::Ink ink) {
  for (int deg = fromDeg; deg <= toDeg; deg += 2) {
    // -90 so zero is at the top, which is where a clock starts.
    const float rad = (deg - 90) * (float)M_PI / 180.0f;
    const int x = cx + (int)lroundf(cosf(rad) * r);
    const int y = cy + (int)lroundf(sinf(rad) * r);
    canvas.circle(x, y, thickness / 2, ink);
  }
}

}  // namespace

void Pomodoro::onEnter(ui::Router& router) {
  shownMinutes_ = -1;
  lastPaintMs_ = millis();
  router.invalidate(epaper::Refresh::kFull);
}

void Pomodoro::reset() {
  state_ = State::kIdle;
  pausedRemainingSec_ = 0;
  shownMinutes_ = -1;
}

int Pomodoro::remainingSeconds() const {
  switch (state_) {
    case State::kRunning: {
      const int32_t left = (int32_t)(endsAtMs_ - millis());
      return left > 0 ? (int)(left / 1000) : 0;
    }
    case State::kPaused:
      return pausedRemainingSec_;
    case State::kDone:
      return 0;
    default:
      return kPresets[presetIndex_] * 60;
  }
}

void Pomodoro::onTick(ui::Router& router) {
  if (state_ != State::kRunning) return;

  const int left = remainingSeconds();
  if (left <= 0) {
    state_ = State::kDone;
    router.invalidate(epaper::Refresh::kFull);
    router.flushPendingPaint();
    audio::sound::success();
    return;
  }

  const int minutes = (left + 59) / 60;
  const bool minuteChanged = (minutes != shownMinutes_);
  const bool ringStale = (millis() - lastPaintMs_) >= kRunningRepaintMs;
  if (minuteChanged || ringStale) {
    lastPaintMs_ = millis();
    heartbeat_ = !heartbeat_;
    router.invalidate();
  }
}

void Pomodoro::draw(gfx::Canvas& canvas) {
  widgets::hudFrame(canvas, 3, 3, canvas.width() - 6, canvas.height() - 6, 15,
                    2);

  const int totalSeconds = kPresets[presetIndex_] * 60;
  const int left = remainingSeconds();
  const int minutes = (left + 59) / 60;  // round up: "1" until it truly ends
  shownMinutes_ = minutes;

  // ── State row: icon plus word ───────────────────────────────────────────
  // The icon shows what the timer IS doing, not what the button would do.
  // "Is it running?" is the question being asked, so answer that directly.
  const char* caption = "READY";
  switch (state_) {
    case State::kRunning: caption = "RUNNING"; break;
    case State::kPaused:  caption = "PAUSED";  break;
    case State::kDone:    caption = "DONE";    break;
    default:              caption = "READY";   break;
  }

  constexpr int kIconSize = 13;
  const int captionW = canvas.textWidth(caption, gfx::Font::kLabel);
  const int rowW = kIconSize + 8 + captionW;
  const int rowX = (canvas.width() - rowW) / 2;
  const int rowY = 16;

  switch (state_) {
    case State::kRunning:
      widgets::iconPlay(canvas, rowX, rowY, kIconSize, gfx::kBlack);
      break;
    case State::kPaused:
      widgets::iconPause(canvas, rowX, rowY, kIconSize, gfx::kBlack);
      break;
    case State::kDone:
      widgets::iconCheck(canvas, rowX, rowY, kIconSize, gfx::kBlack);
      break;
    default:
      // Idle: hollow ring, so the row keeps its shape before starting.
      canvas.circleOutline(rowX + kIconSize / 2, rowY + kIconSize / 2,
                           kIconSize / 2, 2, gfx::kBlack);
      break;
  }
  canvas.text(rowX + kIconSize + 8, rowY - 2, caption, gfx::Font::kLabel,
              gfx::kBlack);

  // ── Ring ────────────────────────────────────────────────────────────────
  canvas.circleOutline(kRingCx, kRingCy, kRingR, 1, gfx::kBlack);
  if (totalSeconds > 0 && left > 0) {
    const int sweep = (360 * left) / totalSeconds;
    arc(canvas, kRingCx, kRingCy, kRingR, 0, sweep, kRingThickness,
        gfx::kBlack);
    // A tick at twelve o'clock, so a nearly-full ring still reads as a ring
    // with a start rather than as a plain circle.
    canvas.rect(kRingCx - 1, kRingCy - kRingR - 7, 3, 7, gfx::kBlack);
  }

  // Heartbeat: a dot at the foot of the ring that swaps filled/hollow on every
  // repaint. On a long timer the ring moves only a few degrees per redraw,
  // which is easy to miss; this changes unmistakably and proves it is live.
  if (state_ == State::kRunning) {
    const int dotY = kRingCy + kRingR;
    if (heartbeat_) {
      canvas.circle(kRingCx, dotY, 4, gfx::kBlack);
    } else {
      canvas.circleOutline(kRingCx, dotY, 4, 2, gfx::kBlack);
    }
  }

  // ── Minutes remaining ───────────────────────────────────────────────────
  char text[6];
  snprintf(text, sizeof(text), "%d", minutes);
  const gfx::numerals::Metrics m = gfx::numerals::metricsFor(46);
  const int textW = gfx::numerals::widthOf(text, m);
  gfx::numerals::draw(canvas, kRingCx - textW / 2, kRingCy - 30, text, m,
                      gfx::kBlack);
  canvas.textInBox(0, kRingCy + 22, canvas.width(), 12, "MIN",
                   gfx::Font::kMicro, gfx::kBlack, gfx::Align::kCenter, 2);

  widgets::buttonMarkers(canvas);
}

bool Pomodoro::onEvent(ui::Router& router, input::Button button,
                       input::Event event) {
  // Taps step the presets while idle. Mid-session they would silently change
  // the length the ring is scaled against, so there they mean "start over"
  // instead — the only other thing you want from a running timer.
  if (event == input::Event::kClick) {
    if (state_ == State::kIdle) {
      const int delta = (button == input::Button::kB) ? 1 : -1;
      presetIndex_ = (presetIndex_ + delta + kPresetCount) % kPresetCount;
    } else {
      reset();
    }
    router.invalidate(epaper::Refresh::kFull);
    return true;
  }

  // Hold B is back, everywhere.
  if (button == input::Button::kB) return false;

  // Hold A is the transport control: start, pause, resume.
  switch (state_) {
    case State::kIdle:
      endsAtMs_ = millis() + (uint32_t)kPresets[presetIndex_] * 60000UL;
      state_ = State::kRunning;
      lastPaintMs_ = millis();
      heartbeat_ = true;
      break;

    case State::kRunning:
      // Capture what is left so resuming does not lose the part-second.
      pausedRemainingSec_ = remainingSeconds();
      state_ = State::kPaused;
      break;

    case State::kPaused:
      endsAtMs_ = millis() + (uint32_t)pausedRemainingSec_ * 1000UL;
      state_ = State::kRunning;
      lastPaintMs_ = millis();
      break;

    case State::kDone:
      reset();
      break;
  }
  router.invalidate(epaper::Refresh::kFull);
  return true;
}

}  // namespace apps
