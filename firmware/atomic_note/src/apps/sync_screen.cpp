#include "sync_screen.h"

#include <Arduino.h>

#include "../audio/sound.h"
#include "../display/epaper.h"
#include "../services/network.h"
#include "../services/notes.h"
#include "../services/transcribe.h"
#include "../ui/router.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"

namespace apps {

namespace widgets = ui::widgets;
namespace theme = ui::theme;
namespace notes = services::notes;
namespace net = services::network;

namespace {

// Matches the reference: three attempts per note before giving up on it. A
// transient hiccup on one recording should not abandon the whole batch.
constexpr int kAttemptsPerNote = 3;

}  // namespace

void SyncScreen::onEnter(ui::Router& router) {
  total_ = notes::countWithoutText();
  done_ = 0;
  failed_ = 0;
  message_ = "";
  radioWasUp_ = net::isJoined();

  if (total_ == 0) {
    phase_ = Phase::kDone;
    message_ = "Nothing to sync";
    router.invalidate(epaper::Refresh::kFull);
    return;
  }

  if (net::isJoined()) {
    phase_ = Phase::kWorking;
  } else {
    phase_ = Phase::kConnecting;
    net::join();
  }
  router.invalidate(epaper::Refresh::kFull);
}

void SyncScreen::onExit() {
  // Leave the radio as it was found. Syncing should not silently turn WiFi on
  // and leave it on afterwards.
  if (!radioWasUp_) net::stop();
}

void SyncScreen::onTick(ui::Router& router) {
  if (phase_ == Phase::kDone) return;

  if (phase_ == Phase::kConnecting) {
    if (net::isJoined()) {
      // Set the clock while the link is up — the same opportunity the
      // reference takes, and the only other thing that wants a network.
      net::syncTime();
      phase_ = Phase::kWorking;
      router.invalidate(epaper::Refresh::kFull);
      return;
    }
    if (net::state() == net::State::kFailed) {
      phase_ = Phase::kDone;
      message_ = "No network";
      router.invalidate(epaper::Refresh::kFull);
    }
    return;
  }

  // One note per tick, so the progress figure actually moves rather than
  // jumping from nothing to finished.
  const int index = notes::indexOfUntranscribed(0);
  if (index < 0) {
    phase_ = Phase::kDone;
    message_ = failed_ ? "Some failed" : "All done";
    if (!failed_) audio::sound::success();
    router.invalidate(epaper::Refresh::kFull);
    return;
  }

  String error;
  bool ok = false;
  for (int attempt = 0; attempt < kAttemptsPerNote && !ok; attempt++) {
    ok = services::transcribe::one(index, error);
    if (!ok && attempt + 1 < kAttemptsPerNote) delay(1500);
  }

  if (ok) {
    done_++;
  } else {
    failed_++;
    message_ = error;
    // Record the failure as the transcript. Without this the same failing note
    // is picked again on the next tick and the batch never finishes — and the
    // user gets to see WHY it failed, on the note itself.
    notes::setTranscript(index, String("[not transcribed] ") + error);
  }
  router.invalidate();
}

void SyncScreen::draw(gfx::Canvas& canvas) {
  const int w = canvas.width();
  widgets::hudFrame(canvas, 3, 3, w - 6, canvas.height() - 6, 15, 2);

  canvas.textInBox(0, 16, w, 14, "SYNC", gfx::Font::kLabel, gfx::kBlack);
  canvas.rect(theme::kMargin, 36, w - 2 * theme::kMargin, 2, gfx::kBlack);

  if (phase_ == Phase::kConnecting) {
    widgets::iconWifi(canvas, w / 2 - 11, 66, gfx::kBlack, 22);
    canvas.textInBox(0, 104, w, 16, "Connecting", gfx::Font::kTitle,
                     gfx::kBlack);
    widgets::buttonMarkers(canvas);
    return;
  }

  if (phase_ == Phase::kWorking) {
    char counter[16];
    snprintf(counter, sizeof(counter), "%d / %d", done_ + failed_, total_);
    canvas.textInBox(0, 58, w, 24, counter, gfx::Font::kDisplay, gfx::kBlack);
    canvas.textInBox(0, 94, w, 14, "Transcribing", gfx::Font::kBody,
                     gfx::kBlack);

    // Progress bar: the count alone does not convey how much is left once the
    // total gets large.
    const int barX = theme::kMargin;
    const int barW = w - 2 * theme::kMargin;
    canvas.roundRectOutline(barX, 122, barW, 14, theme::kChipRadius, 2,
                            gfx::kBlack);
    if (total_ > 0) {
      const int fill = ((barW - 8) * (done_ + failed_)) / total_;
      if (fill > 0) canvas.rect(barX + 4, 126, fill, 6, gfx::kBlack);
    }
    widgets::buttonMarkers(canvas);
    return;
  }

  if (failed_ == 0 && total_ > 0) {
    widgets::iconCheck(canvas, w / 2 - 12, 60, 24, gfx::kBlack);
  }
  canvas.textInBox(0, 98, w, 18, message_.c_str(), gfx::Font::kTitle,
                   gfx::kBlack);
  if (total_ > 0) {
    char summary[32];
    snprintf(summary, sizeof(summary), "%d done, %d failed", done_, failed_);
    canvas.textInBox(0, 126, w, 14, summary, gfx::Font::kBody, gfx::kBlack);
  }
  widgets::buttonMarkers(canvas);
}

bool SyncScreen::onEvent(ui::Router& router, input::Button button,
                         input::Event event) {
  // Nothing to press while it works; hold either button to leave.
  return event == input::Event::kClick;
}

}  // namespace apps
