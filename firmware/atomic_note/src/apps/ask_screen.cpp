#include "ask_screen.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <SD_MMC.h>

#include "../audio/player.h"
#include "../audio/recorder.h"
#include "../audio/sound.h"
#include "../display/epaper.h"
#include "../services/network.h"
#include "../services/settings.h"
#include "../services/storage.h"
#include "../ui/router.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"
#include "../util/str.h"

namespace apps {

namespace widgets = ui::widgets;
namespace theme = ui::theme;
namespace net = services::network;

namespace {

// Questions are scratch: one file, reused, never indexed as a note.
constexpr const char* kScratchPath = "/atomic/ask.wav";

// The spoken answer, also scratch and also reused.
constexpr const char* kSpeechPath = "/atomic/answer.wav";

// Footers state the gesture on every state. This screen used to be the one
// place the grammar broke; it no longer is, but a screen whose main action is
// a hold still benefits from saying so.
void drawFooter(gfx::Canvas& canvas, const char* text) {
  canvas.textInBox(0, canvas.height() - 20, canvas.width(), 10, text,
                   gfx::Font::kMicro, gfx::kBlack, gfx::Align::kCenter, 2);
}

// A toggle you cannot see the state of is a toggle you press twice. Filled
// when answers are read aloud, outlined when they are not.
void drawSpeakBadge(gfx::Canvas& canvas, int y) {
  const bool on = services::settings::speakAnswers();
  const char* label = on ? "VOICE ON" : "VOICE OFF";
  const int textW = canvas.textWidth(label, gfx::Font::kMicro, 2);
  const int boxW = textW + 16;
  const int x = (canvas.width() - boxW) / 2;

  if (on) {
    canvas.roundRect(x, y, boxW, 15, 4, gfx::kBlack);
  } else {
    canvas.roundRectOutline(x, y, boxW, 15, 4, 1, gfx::kBlack);
  }
  canvas.textInBox(x, y + 4, boxW, 8, label, gfx::Font::kMicro,
                   on ? gfx::kWhite : gfx::kBlack, gfx::Align::kCenter, 2);
}
// The answer column runs from under the question band to just above the
// footer. Eight lines rather than six because scrolling costs a refresh per
// page, so fitting more per page is worth more than white space.
constexpr int kLinesPerPage = 8;

}  // namespace

void AskScreen::onEnter(ui::Router& router) {
  phase_ = Phase::kIdle;
  page_ = 0;
  question_ = "";
  answer_ = "";
  error_ = "";
  if (!net::isJoined()) net::join();
  router.invalidate(epaper::Refresh::kFull);
}

void AskScreen::onExit() {
  stopSpeaking();
  if (audio::recorder::active()) audio::recorder::abort();
}

void AskScreen::beginRecording(ui::Router& router) {
  if (!services::storage::available()) {
    error_ = "No SD card";
    phase_ = Phase::kFailed;
    router.invalidate(epaper::Refresh::kFull);
    return;
  }

  // Paint the listening screen BEFORE opening the recorder.
  //
  // Capture runs as a tight loop that never repaints - if the phase changes
  // without a paint here, the screen stays on the idle view for the whole
  // recording and there is no sign it is listening at all. The half second the
  // panel takes is also the half second the user spends drawing breath.
  phase_ = Phase::kRecording;
  router.invalidate(epaper::Refresh::kFull);
  router.flushPendingPaint();

  if (!audio::recorder::start(kScratchPath)) {
    error_ = "Cannot record";
    phase_ = Phase::kFailed;
    router.invalidate(epaper::Refresh::kFull);
    return;
  }
}

void AskScreen::sendQuestion(ui::Router& router) {
  phase_ = Phase::kThinking;
  router.invalidate(epaper::Refresh::kFull);
  router.flushPendingPaint();  // show "thinking" before blocking on the network

  if (!net::isJoined()) {
    error_ = "No network";
    phase_ = Phase::kFailed;
    router.invalidate(epaper::Refresh::kFull);
    return;
  }

  String base = services::settings::companionUrl();
  base.trim();
  while (base.endsWith("/")) base.remove(base.length() - 1);
  if (base.length() == 0) {
    error_ = "No service URL";
    phase_ = Phase::kFailed;
    router.invalidate(epaper::Refresh::kFull);
    return;
  }

  File wav = SD_MMC.open(kScratchPath, FILE_READ);
  if (!wav) {
    error_ = "Audio missing";
    phase_ = Phase::kFailed;
    router.invalidate(epaper::Refresh::kFull);
    return;
  }

  HTTPClient http;
  // 65 seconds, which is the ceiling: setTimeout takes a uint16_t and anything
  // larger wraps. This asked for 120000 and was silently getting 54.4 seconds
  // — a real bug, and one that got worse when Ask started searching the notes
  // before answering, because that is more work inside a shorter budget than
  // the source claimed.
  //
  // Transcription and generation still happen back to back on the far end, so
  // this wants every millisecond it can legally have.
  http.setTimeout(65000);
  http.begin(base + "/ask-voice");
  http.addHeader("Content-Type", "audio/wav");

  // Say whether this answer is going to be read out loud.
  //
  // An answer written for a 200px panel and the same answer written to be
  // spoken are not the same text. The clipped register that makes a screen
  // reply readable — no contractions, minimum words — is exactly what makes a
  // synthesised voice sound like a station announcement. The companion picks
  // its wording from this, so the flag has to go out with the question rather
  // than being discovered later when the audio is fetched.
  http.addHeader("X-Atomic-Speak",
                 services::settings::speakAnswers() ? "1" : "0");
  const int code = http.sendRequest("POST", &wav, wav.size());
  wav.close();

  if (code != 200) {
    error_ = code > 0 ? String("Service said ") + String(code)
                      : String("Cannot reach service");
    http.end();
    phase_ = Phase::kFailed;
    router.invalidate(epaper::Refresh::kFull);
    return;
  }

  const String body = http.getString();
  http.end();

  // "question\n---\nanswer"
  const int split = body.indexOf("\n---\n");
  if (split < 0) {
    question_ = "";
    answer_ = body;
  } else {
    question_ = body.substring(0, split);
    answer_ = body.substring(split + 5);
  }
  question_.trim();
  answer_.trim();

  page_ = 0;
  phase_ = Phase::kAnswer;
  audio::sound::success();

  // Paint the answer BEFORE synthesising it. Fetching the audio takes a couple
  // of seconds, and the answer is readable that whole time — there is no
  // reason to make someone who can read wait on someone else's speech.
  router.invalidate(epaper::Refresh::kFull);
  router.flushPendingPaint();

  if (services::settings::speakAnswers()) speakAnswer();
}

void AskScreen::speakAnswer() {
  if (answer_.length() == 0 || !net::isJoined()) return;

  String base = services::settings::companionUrl();
  base.trim();
  while (base.endsWith("/")) base.remove(base.length() - 1);
  if (base.length() == 0) return;

  HTTPClient http;
  http.setTimeout(30000);
  if (!http.begin(base + "/speak")) return;
  http.addHeader("Content-Type", "text/plain");

  const int code = http.POST(answer_);
  if (code != 200) {
    Serial.printf("[ask] speak failed (%d)\n", code);
    http.end();
    return;
  }

  // Stream to the card rather than into RAM. The player reads from a file
  // anyway, and a long answer is a few hundred KB that PSRAM should not have
  // to hold for no reason.
  File out = SD_MMC.open(kSpeechPath, FILE_WRITE);
  if (!out) {
    http.end();
    return;
  }
  // Take the count from writeToStream, NOT from out.size(). A file opened for
  // writing does not report the bytes just written, so size() reads back as 0
  // and the length check below rejects a download that in fact arrived intact.
  const int written = http.writeToStream(&out);
  out.close();
  http.end();

  if (written <= 44) {
    Serial.printf("[ask] speech download short (%d)\n", written);
    return;
  }
  if (!audio::player::start(kSpeechPath)) {
    Serial.println("[ask] speech will not play");
    return;
  }
  Serial.printf("[ask] speaking %d bytes\n", written);
  phase_ = Phase::kSpeaking;
}

void AskScreen::stopSpeaking() {
  if (audio::player::active()) audio::player::stop();
  if (phase_ == Phase::kSpeaking) phase_ = Phase::kAnswer;
}

void AskScreen::onTick(ui::Router& router) {
  if (phase_ == Phase::kSpeaking) {
    // Same contract as the note player: pumped from the tick so the buttons
    // stay alive and the answer can be interrupted mid-sentence.
    if (!audio::player::pump()) {
      audio::player::stop();
      phase_ = Phase::kAnswer;
      router.invalidate(epaper::Refresh::kPartial);
    }
    return;
  }
  if (phase_ != Phase::kRecording) return;

  // Same tight loop as the note recorder: nothing else runs, no repaints, or
  // the capture DMA overruns and the question comes back garbled.
  for (;;) {
    input::sample();
    if (!audio::recorder::pump()) {
      audio::recorder::abort();
      error_ = "Recording failed";
      phase_ = Phase::kFailed;
      router.invalidate(epaper::Refresh::kFull);
      return;
    }
    if (!input::isHeld(input::Button::kA) &&
        audio::recorder::elapsedMs() >= audio::recorder::kMinDurationMs) {
      break;
    }
  }

  if (!audio::recorder::stop()) {
    phase_ = Phase::kIdle;
    router.invalidate(epaper::Refresh::kFull);
    return;
  }
  sendQuestion(router);
}

void AskScreen::draw(gfx::Canvas& canvas) {
  const int w = canvas.width();
  const int left = theme::kMargin;
  const int width = w - 2 * theme::kMargin - 6;

  if (phase_ == Phase::kRecording) {
    // Inverted while listening, matching the note recorder so the state is
    // recognisable from across a desk.
    canvas.fill(gfx::kBlack);
    widgets::hudFrame(canvas, 3, 3, w - 6, canvas.height() - 6, 15, 2,
                      gfx::kWhite);
    canvas.textInBox(0, 34, w, 16, "LISTENING", gfx::Font::kLabel,
                     gfx::kWhite);

    // Concentric rings around a solid centre: reads as a live microphone at a
    // glance, and unmistakably different from the hollow ring shown at rest.
    canvas.circle(w / 2, 104, 15, gfx::kWhite);
    canvas.circleOutline(w / 2, 104, 27, 3, gfx::kWhite);
    canvas.circleOutline(w / 2, 104, 39, 2, gfx::kWhite);

    canvas.textInBox(0, 162, w, 12, "RELEASE TO ASK", gfx::Font::kMicro,
                     gfx::kWhite, gfx::Align::kCenter, 2);
    return;
  }

  widgets::hudFrame(canvas, 3, 3, w - 6, canvas.height() - 6, 15, 2);

  if (phase_ == Phase::kThinking) {
    canvas.textInBox(0, 84, w, 18, "Thinking", gfx::Font::kTitle, gfx::kBlack);
    canvas.textInBox(0, 112, w, 12, "FIRST ANSWER IS SLOWER",
                     gfx::Font::kMicro, gfx::kBlack, gfx::Align::kCenter, 2);
    widgets::buttonMarkers(canvas);
    return;
  }

  if (phase_ == Phase::kFailed) {
    canvas.textInBox(left, 80, width, 16, error_.c_str(), gfx::Font::kLabel,
                     gfx::kBlack);
    drawFooter(canvas, "HOLD A RETRY   HOLD B BACK");
    widgets::buttonMarkers(canvas);
    return;
  }

  if (phase_ == Phase::kIdle) {
    canvas.text(left, 12, "Ask", gfx::Font::kLabel, gfx::kBlack);
    const int titleW = canvas.textWidth("Ask", gfx::Font::kLabel);
    canvas.rect(left + titleW + 8, 18, width - titleW - 8, 3, gfx::kBlack);

    canvas.circleOutline(w / 2, 88, 26, 3, gfx::kBlack);
    canvas.textInBox(0, 124, w, 16, "Hold A and speak", gfx::Font::kBody,
                     gfx::kBlack);
    drawSpeakBadge(canvas, 146);
    drawFooter(canvas, "HOLD A SPEAK   HOLD B BACK");
    widgets::buttonMarkers(canvas);
    return;
  }

  // ── The answer ──────────────────────────────────────────────────────────
  // What it heard, in an inverted band, above what it said. Getting a strange
  // answer is usually a mis-heard question, and showing both makes that
  // obvious instead of mysterious.
  if (question_.length() > 0) {
    canvas.roundRect(left - 4, 8, width + 8, 28, theme::kCardRadius,
                     gfx::kBlack);
    const String heard = util::displayable(question_);
    canvas.textElided(left + 2, 15, width, heard.c_str(), gfx::Font::kBody,
                      gfx::kWhite);
  }

  const String text = util::displayable(answer_);
  totalLines_ = canvas.textWrapped(left, 44, width, kLinesPerPage,
                                   text.c_str(), gfx::Font::kBody, gfx::kBlack,
                                   page_ * kLinesPerPage);

  const int pages = max(1, (totalLines_ + kLinesPerPage - 1) / kLinesPerPage);

  if (pages > 1) {
    // A scrollbar on the right edge: the page count alone does not convey how
    // much is left, and on a screen this size that matters.
    const int trackTop = 44;
    const int trackH = canvas.height() - 70;
    const int barX = canvas.width() - 12;
    canvas.rect(barX, trackTop, 2, trackH, gfx::kBlack);

    const int thumbH = max(10, trackH / pages);
    const int thumbY = trackTop + (trackH - thumbH) * page_ / max(1, pages - 1);
    canvas.roundRect(barX - 2, thumbY, 6, thumbH, 3, gfx::kBlack);

    char footer[44];
    snprintf(footer, sizeof(footer), "%d/%d  A/B PAGE  HOLD B BACK", page_ + 1,
             pages);
    drawFooter(canvas, footer);
  } else {
    drawFooter(canvas, "TAP VOICE   HOLD A ASK   HOLD B BACK");
  }

  widgets::buttonMarkers(canvas);
}

bool AskScreen::onEvent(ui::Router& router, input::Button button,
                        input::Event event) {
  if (phase_ == Phase::kRecording || phase_ == Phase::kThinking) {
    return true;  // the gesture belongs to the question in flight
  }

  // While it is talking, ANY press shuts it up first and does nothing else.
  // Interrupting is the one thing you want to be able to do without aiming,
  // and having the same press also turn a page would be its own annoyance.
  if (phase_ == Phase::kSpeaking) {
    stopSpeaking();
    router.invalidate(epaper::Refresh::kPartial);
    return true;
  }

  // Hold A asks. This IS the select gesture — asking is the only thing there
  // is to confirm on this screen — so the standard grammar holds here without
  // an exception, which it did not under the old one.
  if (button == input::Button::kA && event == input::Event::kLongPress) {
    stopSpeaking();
    beginRecording(router);
    return true;
  }

  if (event == input::Event::kClick) {
    // With an answer up, taps page through it. Without one there is nothing to
    // page, so they fall to the only other setting worth reaching from here.
    if (phase_ == Phase::kAnswer) {
      const int pages =
          max(1, (totalLines_ + kLinesPerPage - 1) / kLinesPerPage);
      if (pages > 1) {
        const int delta = (button == input::Button::kB) ? 1 : -1;
        page_ = (page_ + delta + pages) % pages;
        router.invalidate(epaper::Refresh::kFull);
        return true;
      }
    }

    // Toggle reading answers out loud. Persisted, so this is also how it gets
    // turned on the first time — no menu entry to go hunting for.
    const bool on = !services::settings::speakAnswers();
    services::settings::setSpeakAnswers(on);

    // Turning it on with an answer already up reads that answer, rather than
    // making you ask again to hear what you just got.
    if (on && phase_ == Phase::kAnswer) {
      router.invalidate(epaper::Refresh::kPartial);
      router.flushPendingPaint();
      speakAnswer();
      return true;
    }
    router.invalidate(epaper::Refresh::kPartial);
    return true;
  }

  // Hold B is back, and it must silence the speaker on the way out.
  if (button == input::Button::kB && event == input::Event::kLongPress) {
    stopSpeaking();
  }
  return false;
}

}  // namespace apps
