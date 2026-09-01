// Push-to-talk recording, then tagging.
//
// Reached by holding A on the dashboard. Recording runs for exactly as long as
// the button is held — no start/stop to remember, and no way to leave it
// recording by accident.
//
// Two phases in one screen because they are one action: capture, then say what
// it was about. A recording that is never tagged is one you cannot find later.
#pragma once

#include "../ui/screen.h"

namespace apps {

class RecordScreen : public ui::Screen {
 public:
  const char* name() const override { return "Record"; }

  void onEnter(ui::Router& router) override;
  void onExit() override;
  void draw(gfx::Canvas& canvas) override;
  bool onEvent(ui::Router& router, input::Button button,
               input::Event event) override;

  // Recording is driven from the tick, so it has to run often.
  uint32_t tickIntervalMs() const override { return 10; }
  void onTick(ui::Router& router) override;

  bool blocksSleep() const override { return true; }

 private:
  enum class Phase : uint8_t { kArming, kRecording, kTagging, kFailed };

  Phase phase_ = Phase::kArming;
  int noteNumber_ = 0;
  uint32_t durationMs_ = 0;
  int tagCursor_ = 0;
  int shownSeconds_ = -1;
  const char* failure_ = "";

  void beginRecording(ui::Router& router);
  void finishRecording(ui::Router& router);

  // The tag carousel carries one extra position after the tags: Discard.
  // Putting it there rather than on a new gesture means throwing a recording
  // away costs exactly what keeping it costs — a tap and a hold — which is
  // the point, because an accidental recording is a common event.
  int optionCount() const;
  bool isDiscard(int index) const;
  void discardRecording();
};

}  // namespace apps
