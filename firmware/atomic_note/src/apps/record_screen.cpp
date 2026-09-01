#include "record_screen.h"

#include <Arduino.h>
#include <SD_MMC.h>

#include "../audio/recorder.h"
#include "../audio/sound.h"
#include "../display/epaper.h"
#include "../display/numerals.h"
#include "../services/notes.h"
#include "../services/settings.h"
#include "../services/storage.h"
#include "../ui/router.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"

namespace apps {

namespace widgets = ui::widgets;
namespace theme = ui::theme;
namespace notes = services::notes;
namespace settings = services::settings;

namespace {

// A microphone, drawn: capsule, stand, and base.
void iconMic(gfx::Canvas& canvas, int cx, int cy, int scale, gfx::Ink ink) {
  const int w = 7 * scale;
  const int h = 16 * scale;
  canvas.roundRect(cx - w / 2, cy - h / 2 - 2 * scale, w, h, w / 2, ink);
  // Cradle: an arc under the capsule, drawn as a ring with its top removed.
  const int r = 7 * scale;
  const int by = cy + 3 * scale;
  canvas.circleOutline(cx, by, r, 2 * scale >= 2 ? 2 : 1, ink);
  canvas.rect(cx - r - 2, by - r - 2, 2 * r + 4, r + 2,
              ink == gfx::kBlack ? gfx::kWhite : gfx::kBlack);
  canvas.roundRect(cx - w / 2, cy - h / 2 - 2 * scale, w, h, w / 2, ink);
  // Stand and foot.
  canvas.rect(cx - scale, by + r - scale, 2 * scale, 4 * scale, ink);
  canvas.rect(cx - 5 * scale, by + r + 3 * scale, 10 * scale, 2 * scale, ink);
}

}  // namespace

void RecordScreen::onEnter(ui::Router& router) {
  phase_ = Phase::kArming;
  shownSeconds_ = -1;
  durationMs_ = 0;
  tagCursor_ = 0;
  failure_ = "";
  router.invalidate(epaper::Refresh::kFull);
  // Paint the recording screen BEFORE opening the file: the panel takes half a
  // second, and the user is already talking by then.
  router.flushPendingPaint();
  beginRecording(router);
}

void RecordScreen::onExit() {
  if (audio::recorder::active()) audio::recorder::abort();
}

// Tags, then one more slot for Discard.
int RecordScreen::optionCount() const { return settings::tagCount() + 1; }

bool RecordScreen::isDiscard(int index) const {
  return index == settings::tagCount();
}

void RecordScreen::discardRecording() {
  // Delete the audio and index nothing. `notes::add` was never called, so
  // there is no catalogue entry to remove — only the file.
  if (noteNumber_ <= 0) return;
  const String path = notes::wavPath(noteNumber_);
  if (SD_MMC.exists(path.c_str())) SD_MMC.remove(path.c_str());
  Serial.printf("[record] discarded note %03d\n", noteNumber_);
  noteNumber_ = 0;  // so a second press cannot delete a later recording
}

void RecordScreen::beginRecording(ui::Router& router) {
  if (!services::storage::available()) {
    failure_ = "No SD card";
    phase_ = Phase::kFailed;
    router.invalidate(epaper::Refresh::kFull);
    return;
  }

  noteNumber_ = notes::nextNumber();
  const String path = notes::wavPath(noteNumber_);
  if (!audio::recorder::start(path.c_str())) {
    failure_ = "Cannot record";
    phase_ = Phase::kFailed;
    router.invalidate(epaper::Refresh::kFull);
    return;
  }
  phase_ = Phase::kRecording;
}

void RecordScreen::finishRecording(ui::Router& router) {
  durationMs_ = audio::recorder::elapsedMs();
  const bool kept = audio::recorder::stop();

  if (!kept) {
    // Too short to be meaningful — treat it as a mis-press and leave quietly
    // rather than asking the user to tag half a second of nothing.
    if (router.canGoBack()) router.back();
    return;
  }

  phase_ = Phase::kTagging;
  tagCursor_ = 0;
  router.invalidate(epaper::Refresh::kFull);
}

void RecordScreen::onTick(ui::Router& router) {
  if (phase_ != Phase::kRecording) return;

  // Capture runs as a TIGHT LOOP that does nothing but read, convert, write —
  // the same shape as this board's reference firmware, and for the same
  // reason. Anything else in the loop is a gap in the audio.
  //
  // In particular there are NO REPAINTS while recording. A panel refresh
  // blocks for around half a second, which overruns the capture DMA and drops
  // a chunk of sound. That is why the screen is painted once on entry and then
  // left alone, and why there is no live elapsed counter: a moving number
  // would cost a refresh a second, and the recording matters more than the
  // number does.
  for (;;) {
    // Nothing else is sampling the buttons while this loop owns the CPU.
    input::sample();

    if (!audio::recorder::pump()) {
      failure_ = "Write failed";
      audio::recorder::abort();
      phase_ = Phase::kFailed;
      router.invalidate(epaper::Refresh::kFull);
      return;
    }

    if (!input::isHeld(input::Button::kA) &&
        audio::recorder::elapsedMs() >= audio::recorder::kMinDurationMs) {
      break;
    }
  }
  finishRecording(router);
}

void RecordScreen::draw(gfx::Canvas& canvas) {
  const int w = canvas.width();

  if (phase_ == Phase::kFailed) {
    widgets::hudFrame(canvas, 3, 3, w - 6, canvas.height() - 6, 15, 2);
    canvas.textInBox(0, 84, w, 18, failure_, gfx::Font::kTitle, gfx::kBlack);
    canvas.textInBox(0, 112, w, 14, "Hold A to go back", gfx::Font::kBody,
                     gfx::kBlack);
    widgets::buttonMarkers(canvas);
    return;
  }

  if (phase_ == Phase::kRecording || phase_ == Phase::kArming) {
    // Inverted while live: unmistakable across a room, and unmistakably
    // different from every other screen.
    canvas.fill(gfx::kBlack);
    widgets::hudFrame(canvas, 3, 3, w - 6, canvas.height() - 6, 15, 2,
                      gfx::kWhite);

    canvas.textInBox(0, 24, w, 14, "RECORDING", gfx::Font::kLabel,
                     gfx::kWhite);
    iconMic(canvas, w / 2, 104, 4, gfx::kWhite);
    canvas.textInBox(0, 168, w, 12, "RELEASE TO STOP", gfx::Font::kMicro,
                     gfx::kWhite, gfx::Align::kCenter, 2);
    return;
  }

  // ── Tagging ─────────────────────────────────────────────────────────────
  widgets::hudFrame(canvas, 3, 3, w - 6, canvas.height() - 6, 15, 2);

  char header[24];
  snprintf(header, sizeof(header), "NOTE %03d  %lus", noteNumber_,
           (unsigned long)(durationMs_ / 1000));
  canvas.textInBox(0, 14, w, 14, header, gfx::Font::kLabel, gfx::kBlack);
  canvas.rect(theme::kMargin, 34, w - 2 * theme::kMargin, 2, gfx::kBlack);

  canvas.textInBox(0, 44, w, 12, "TAG IT", gfx::Font::kMicro, gfx::kBlack,
                   gfx::Align::kCenter, 2);

  // Discard sits at the end of the same carousel the tags are in, so it needs
  // no new gesture: tap past the last tag and hold A, exactly as saving works.
  // A stray press that starts a recording is common enough that throwing one
  // away has to be as cheap as keeping it.
  const int options = optionCount();
  const int index = options > 0 ? (tagCursor_ % options) : 0;
  const bool discarding = isDiscard(index);

  if (options > 0) {
    if (discarding) {
      // Outlined, not filled. Every tag reads as a solid card; the one action
      // that destroys something should not look identical to the six that
      // keep it.
      canvas.roundRectOutline(28, 70, w - 56, 40, theme::kCardRadius, 2,
                              gfx::kBlack);
      canvas.textInBox(28, 70, w - 56, 40, "Discard", gfx::Font::kTitle,
                       gfx::kBlack);
    } else {
      canvas.roundRect(28, 70, w - 56, 40, theme::kCardRadius, gfx::kBlack);
      canvas.textInBox(28, 70, w - 56, 40, settings::tag(index).c_str(),
                       gfx::Font::kTitle, gfx::kWhite);
    }

    const int gap = 12;
    const int startX = (w - (options - 1) * gap) / 2;
    for (int i = 0; i < options; i++) {
      const int x = startX + i * gap;
      if (i == index) {
        canvas.circle(x, 126, 3, gfx::kBlack);
      } else {
        canvas.circleOutline(x, 126, 3, 1, gfx::kBlack);
      }
    }
  }

  canvas.textInBox(0, 150, w, 12,
                   discarding ? "HOLD A DISCARD   TAP CHANGE"
                              : "HOLD A SAVE   TAP CHANGE",
                   gfx::Font::kMicro, gfx::kBlack, gfx::Align::kCenter, 2);
  widgets::buttonMarkers(canvas);
}

bool RecordScreen::onEvent(ui::Router& router, input::Button button,
                           input::Event event) {
  if (phase_ == Phase::kRecording || phase_ == Phase::kArming) {
    // Every press belongs to the recording gesture itself.
    return true;
  }

  if (phase_ == Phase::kFailed) return false;  // hold B leaves

  const int options = optionCount();

  if (event == input::Event::kClick) {
    if (options > 0) {
      const int delta = (button == input::Button::kB) ? 1 : -1;
      tagCursor_ = (tagCursor_ + delta + options) % options;
    }
    router.invalidate();
    return true;
  }

  // Hold B is back, and backing out of tagging means the note was never
  // saved. The audio is already on the card at this point, so leaving without
  // deleting it leaves a file that no index references — invisible, and still
  // taking space. Same outcome as Discard, reached by the standard gesture.
  if (button == input::Button::kB) {
    discardRecording();
    return false;  // let the router pop the screen
  }

  const int index = options > 0 ? (tagCursor_ % options) : 0;

  if (isDiscard(index)) {
    discardRecording();
    audio::sound::back();
    if (router.canGoBack()) router.back();
    return true;
  }

  // Hold A saves with the chosen tag.
  const String tag =
      options > 1 ? settings::tag(index) : String("Note");
  notes::add(noteNumber_, tag.c_str(), durationMs_);
  audio::sound::success();

  if (router.canGoBack()) router.back();
  return true;
}

}  // namespace apps
