#include "dice.h"

#include <Arduino.h>
#include <esp_random.h>

#include "../audio/sound.h"
#include "../board/haptics.h"
#include "../display/epaper.h"
#include "../services/motion.h"
#include "../ui/router.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"

namespace apps {

namespace widgets = ui::widgets;
namespace theme = ui::theme;
namespace motion = services::motion;

namespace {

// Thresholds are relative to what THIS sensor reads at rest, not absolute.
//
// Gravity is 1 g and an ideal part would report exactly that lying still. The
// one on this device reads 1.11 g — about 11% high, which is ordinary
// uncalibrated scale error and varies from part to part. Hard-coding "2.4 g
// means shaken" therefore means something slightly different on every board,
// and on a part reading 10% low it would take a noticeably harder shake.
//
// So rest is measured on entry and everything is an offset from it.
// 1.9 g above rest — about 3.0 g on the part in this device.
//
// Started at 1.3 and it registered rolls from ordinary handling: setting the
// device down, or picking it up briskly, both cross 2.4 g. The failure is
// asymmetric, which is why this is set high rather than in the middle: a die
// that will not roll makes you shake harder and you get your roll, whereas one
// that rolls when you put it down has already destroyed the previous result.
constexpr float kShakeDeltaG = 1.9f;
constexpr float kCalmDeltaG = 0.3f;   // back to roughly still
constexpr uint32_t kCalmMs = 400;

// How long to spend measuring rest when the screen opens.
constexpr int kRestSamples = 8;

constexpr int kFaceSize = 116;
constexpr int kPipRadius = 9;

// Pip positions on a 3x3 grid, as bit positions:
//   0 1 2
//   3 4 5
//   6 7 8
// A die's faces are conventional, so these are the conventional layouts.
const uint16_t kFaces[7] = {
    0,
    1 << 4,                                        // 1: centre
    (1 << 0) | (1 << 8),                           // 2: one diagonal
    (1 << 0) | (1 << 4) | (1 << 8),                // 3: diagonal + centre
    (1 << 0) | (1 << 2) | (1 << 6) | (1 << 8),     // 4: corners
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 8),
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8),
};

void drawFace(gfx::Canvas& canvas, int cx, int cy, int value) {
  const int half = kFaceSize / 2;
  canvas.roundRectOutline(cx - half, cy - half, kFaceSize, kFaceSize, 18, 3,
                          gfx::kBlack);

  if (value < 1 || value > 6) return;
  const uint16_t layout = kFaces[value];

  // The pips sit on a 3x3 grid inset from the face edge.
  const int step = kFaceSize / 3;
  const int origin = -step;
  for (int cell = 0; cell < 9; cell++) {
    if (!(layout & (1 << cell))) continue;
    const int px = cx + origin + (cell % 3) * step;
    const int py = cy + origin + (cell / 3) * step;
    canvas.circle(px, py, kPipRadius, gfx::kBlack);
  }
}

}  // namespace

void Dice::onEnter(ui::Router& router) {
  phase_ = motion::available() ? Phase::kWaiting : Phase::kNoSensor;
  value_ = 0;
  rolls_ = 0;
  armed_ = true;
  calmSinceMs_ = millis();

  // Measure rest now. It is a few milliseconds, it happens while the screen is
  // still painting, and it makes the shake feel the same on any sensor.
  restG_ = 1.0f;
  if (phase_ == Phase::kWaiting) {
    float total = 0.0f;
    int taken = 0;
    for (int i = 0; i < kRestSamples; i++) {
      const float g = motion::magnitude();
      if (g > 0.0f) {
        total += g;
        taken++;
      }
      delay(5);
    }
    if (taken) restG_ = total / taken;
    Serial.printf("[dice] rest %.2f g, shake at %.2f g\n", restG_,
                  restG_ + kShakeDeltaG);
  }

  router.invalidate(epaper::Refresh::kFull);
}

void Dice::roll(ui::Router& router) {
  // esp_random() is the hardware RNG. Seeding anything would be a downgrade,
  // and % 6 on a 32-bit value has a bias far below what six faces can show.
  value_ = (int)(esp_random() % 6) + 1;
  rolls_++;
  phase_ = Phase::kRolled;

  // Felt before it is seen: the panel takes about half a second to show the
  // result, and a shake that produces nothing at all in that time reads as a
  // device that missed it.
  haptics::bump();
  audio::sound::select();

  armed_ = false;
  calmSinceMs_ = 0;
  router.invalidate(epaper::Refresh::kFull);
}

void Dice::onTick(ui::Router& router) {
  if (phase_ == Phase::kNoSensor) return;

  const float g = motion::magnitude();
  if (g <= 0.0f) return;  // a failed read is not a still device

  if (!armed_) {
    // Wait for it to settle, and for the settling to last.
    if (g > restG_ + kCalmDeltaG) {
      calmSinceMs_ = 0;
      return;
    }
    if (calmSinceMs_ == 0) {
      calmSinceMs_ = millis();
      return;
    }
    if (millis() - calmSinceMs_ >= kCalmMs) armed_ = true;
    return;
  }

  if (g >= restG_ + kShakeDeltaG) {
    // Log what actually triggered, so the threshold can be tuned from real
    // gestures rather than guessed at again.
    Serial.printf("[dice] roll at %.2f g (rest %.2f, threshold %.2f)\n", g,
                  restG_, restG_ + kShakeDeltaG);
    roll(router);
  }
}

void Dice::draw(gfx::Canvas& canvas) {
  const int w = canvas.width();
  widgets::hudFrame(canvas, 3, 3, w - 6, canvas.height() - 6, 15, 2);

  if (phase_ == Phase::kNoSensor) {
    canvas.textInBox(0, 78, w, 18, "No motion sensor", gfx::Font::kTitle,
                     gfx::kBlack);
    canvas.textInBox(0, 106, w, 12, "CONNECT AN MPU6050", gfx::Font::kMicro,
                     gfx::kBlack, gfx::Align::kCenter, 2);
    widgets::buttonMarkers(canvas);
    return;
  }

  canvas.text(theme::kMargin, 12, "Dice", gfx::Font::kLabel, gfx::kBlack);
  const int titleW = canvas.textWidth("Dice", gfx::Font::kLabel);
  canvas.rect(theme::kMargin + titleW + 8, 18,
              w - 2 * theme::kMargin - titleW - 8, 4, gfx::kBlack);

  if (phase_ == Phase::kWaiting) {
    canvas.roundRectOutline((w - kFaceSize) / 2, 52, kFaceSize, kFaceSize, 18,
                            3, gfx::kBlack);
    canvas.textInBox(0, 96, w, 16, "Shake", gfx::Font::kTitle, gfx::kBlack);
    canvas.textInBox(0, canvas.height() - 20, w, 10, "SHAKE TO ROLL",
                     gfx::Font::kMicro, gfx::kBlack, gfx::Align::kCenter, 2);
    widgets::buttonMarkers(canvas);
    return;
  }

  drawFace(canvas, w / 2, 52 + kFaceSize / 2, value_);

  char footer[28];
  snprintf(footer, sizeof(footer), "ROLL %d   SHAKE AGAIN", rolls_);
  canvas.textInBox(0, canvas.height() - 20, w, 10, footer, gfx::Font::kMicro,
                   gfx::kBlack, gfx::Align::kCenter, 2);
  widgets::buttonMarkers(canvas);
}

bool Dice::onEvent(ui::Router& router, input::Button button,
                   input::Event event) {
  // Hold A rolls too. The shake is the point, but a die you cannot roll while
  // the device is on a desk — or when someone is watching and you would rather
  // not flail — is needlessly strict.
  if (button == input::Button::kA && event == input::Event::kLongPress &&
      phase_ != Phase::kNoSensor) {
    roll(router);
    return true;
  }
  return false;  // hold B falls through to the Router for Back
}

}  // namespace apps
