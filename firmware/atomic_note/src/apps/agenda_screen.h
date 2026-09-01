// Today and tomorrow, as a timeline.
//
// Only two days: a 200 px screen cannot usefully show a week, and the question
// a desk device is asked is "what is next", not "what does my month look like".
//
//   tap B    next event
//   hold B   previous event
//   tap A    open the event
//   hold A   back
#pragma once

#include "../services/calendar_feed.h"
#include "../ui/screen.h"

namespace apps {

class AgendaScreen : public ui::Screen {
 public:
  const char* name() const override { return "Agenda"; }

  void onEnter(ui::Router& router) override;
  void onExit() override;
  void draw(gfx::Canvas& canvas) override;
  bool onEvent(ui::Router& router, input::Button button,
               input::Event event) override;

  uint32_t tickIntervalMs() const override { return 400; }
  void onTick(ui::Router& router) override;

  bool blocksSleep() const override { return phase_ != Phase::kReady; }

 private:
  enum class Phase : uint8_t { kConnecting, kFetching, kReady };
  static constexpr int kRowsPerPage = 4;

  Phase phase_ = Phase::kConnecting;
  bool radioWasUp_ = false;
  int cursor_ = 0;
  bool showingDetail_ = false;
  services::calendar_feed::Agenda agenda_;

  void drawTimeline(gfx::Canvas& canvas);
  void drawDetail(gfx::Canvas& canvas);
};

}  // namespace apps
