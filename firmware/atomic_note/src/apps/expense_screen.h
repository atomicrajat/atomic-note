#pragma once

#include "../services/network.h"
#include "../ui/screen.h"

namespace apps {

class ExpenseScreen : public ui::Screen {
 public:
  ExpenseScreen() = default;

  const char* name() const override { return "Expenses"; }

  uint32_t tickIntervalMs() const override { return 200; }
  bool blocksSleep() const override { return phase_ != Phase::kReady; }

  void onEnter(ui::Router& router) override;
  void onExit() override;
  void onTick(ui::Router& router) override;
  void draw(gfx::Canvas& canvas) override;
  bool onEvent(ui::Router& router, input::Button button,
               input::Event event) override;

 private:
  void drawGraph(gfx::Canvas& canvas);
  void drawBudget(gfx::Canvas& canvas);
  void fetchExpenses(ui::Router& router);

  enum class Phase {
    kConnecting,
    kFetching,
    kReady,
  };
  Phase phase_ = Phase::kReady;

  bool radioWasUp_ = false;
  bool hasData_ = false;
  bool showingBudget_ = false;

  float monthTotal_ = 0.0f;
  float daily_[7] = {0}; // 0=Sunday
  String errorMsg_;
};

}  // namespace apps
