// Ask a question out loud, read the answer.
//
// Voice is the input because two buttons cannot type, and the recorder and
// transcription path already exist — so the whole interaction is: hold to
// speak, release, read.
//
// The question and the answer both come back from one request. Showing what it
// HEARD alongside what it said is what makes a wrong answer diagnosable rather
// than baffling.
#pragma once

#include "../ui/screen.h"

namespace apps {

class AskScreen : public ui::Screen {
 public:
  const char* name() const override { return "Ask"; }

  void onEnter(ui::Router& router) override;
  void onExit() override;
  void draw(gfx::Canvas& canvas) override;
  bool onEvent(ui::Router& router, input::Button button,
               input::Event event) override;

  uint32_t tickIntervalMs() const override { return 10; }
  void onTick(ui::Router& router) override;

  Idle idlePolicy() const override {
    return phase_ != Phase::kIdle ? Idle::kStay : Idle::kReturn;
  }

 private:
  enum class Phase : uint8_t {
    kIdle,       // waiting to be asked
    kRecording,  // button held, capturing
    kThinking,   // uploaded, waiting on the model
    kAnswer,     // showing the reply
    kSpeaking,   // showing it and reading it out
    kFailed,
  };

  Phase phase_ = Phase::kIdle;
  int page_ = 0;
  int totalLines_ = 0;
  String question_;
  String answer_;
  String error_;

  void beginRecording(ui::Router& router);
  void sendQuestion(ui::Router& router);

  // Fetch the spoken form and start playing. Silent no-op when speech is off
  // or the fetch fails: losing the audio must never cost the user the answer
  // they can already read.
  void speakAnswer();
  void stopSpeaking();
};

}  // namespace apps
