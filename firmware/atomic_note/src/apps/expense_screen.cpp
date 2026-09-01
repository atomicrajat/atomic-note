#include "expense_screen.h"

#include <HTTPClient.h>
#include <WiFi.h>

#include "../services/network.h"
#include "../services/settings.h"
#include "../ui/router.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"

namespace apps {

namespace widgets = ui::widgets;
namespace theme = ui::theme;
namespace net = services::network;

void ExpenseScreen::onEnter(ui::Router& router) {
  radioWasUp_ = net::isJoined();
  showingBudget_ = false;

  if (!hasData_) {
    hasData_ = services::settings::loadExpenseData(monthTotal_, daily_);
  }

  if (hasData_) {
    phase_ = Phase::kReady;
  } else {
    errorMsg_ = "";
    if (net::isJoined()) {
      phase_ = Phase::kFetching;
    } else {
      phase_ = Phase::kConnecting;
      net::join();
    }
  }
  router.invalidate(epaper::Refresh::kFull);
}

void ExpenseScreen::onExit() {
  if (!radioWasUp_) net::stop();
}

void ExpenseScreen::fetchExpenses(ui::Router& router) {
  phase_ = Phase::kReady;

  String base = services::settings::companionUrl();
  base.trim();
  while (base.endsWith("/")) base.remove(base.length() - 1);
  if (base.length() == 0) {
    errorMsg_ = "No service URL";
    return;
  }

  const String url = base + "/expenses";
  Serial.printf("[expenses] fetching %s\n", url.c_str());

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, url)) {
    errorMsg_ = "Bad URL";
    Serial.println("[expenses] http.begin failed");
    return;
  }

  const int code = http.GET();
  Serial.printf("[expenses] http code: %d\n", code);
  if (code != 200) {
    errorMsg_ = code > 0 ? String("Service said ") + String(code)
                         : String("Cannot reach service");
    http.end();
    return;
  }

  const String body = http.getString();
  http.end();
  Serial.printf("[expenses] body len: %d\n", body.length());

  // Reset values
  monthTotal_ = 0;
  for (int i=0; i<7; i++) daily_[i] = 0;

  int start = 0;
  while (start < (int)body.length()) {
    int end = body.indexOf('\n', start);
    if (end < 0) end = body.length();
    const String line = body.substring(start, end);
    start = end + 1;
    if (line.length() == 0) continue;

    if (line.startsWith("month ")) {
      // month <YYYY-MM> <total> <count> <currency>
      int space1 = line.indexOf(' ', 6);
      if (space1 > 0) {
        int space2 = line.indexOf(' ', space1 + 1);
        if (space2 > 0) {
          monthTotal_ = line.substring(space1 + 1, space2).toFloat();
        }
      }
    } else if (line.startsWith("day ")) {
      // day <0-6> \t <amount>
      int tab = line.indexOf('\t');
      if (tab > 0) {
        int dayIdx = line.substring(4, tab).toInt();
        if (dayIdx >= 0 && dayIdx <= 6) {
          daily_[dayIdx] = line.substring(tab + 1).toFloat();
        }
      }
    }
  }

  hasData_ = true;
  errorMsg_ = "";
  services::settings::saveExpenseData(monthTotal_, daily_);
}

void ExpenseScreen::onTick(ui::Router& router) {
  if (phase_ == Phase::kReady) return;

  if (phase_ == Phase::kConnecting) {
    if (net::isJoined()) {
      phase_ = Phase::kFetching;
      router.invalidate(epaper::Refresh::kFull);
      return;
    }
    if (net::state() == net::State::kFailed) {
      phase_ = Phase::kReady;
      errorMsg_ = "No network";
      router.invalidate(epaper::Refresh::kFull);
    }
    return;
  }

  if (phase_ == Phase::kFetching) {
    fetchExpenses(router);
    router.invalidate(epaper::Refresh::kFull);
  }
}

void ExpenseScreen::draw(gfx::Canvas& canvas) {
  const int w = canvas.width();
  const int left = theme::kMargin;
  const int width = w - 2 * theme::kMargin - 6;

  widgets::hudFrame(canvas, 3, 3, w - 6, canvas.height() - 6, 15, 2);

  const char* title = showingBudget_ ? "Budget" : "Expenses";
  canvas.text(left, 12, title, gfx::Font::kLabel, gfx::kBlack);
  const int titleW = canvas.textWidth(title, gfx::Font::kLabel);
  if (width > titleW + 8) {
    canvas.rect(left + titleW + 8, 18, width - titleW - 8, 4, gfx::kBlack);
  }

  if (phase_ != Phase::kReady) {
    canvas.textInBox(left, 90, width, 16,
                     phase_ == Phase::kConnecting ? "Connecting" : "Fetching",
                     gfx::Font::kTitle, gfx::kBlack);
    widgets::buttonMarkers(canvas);
    return;
  }

  if (errorMsg_.length() > 0) {
    canvas.textInBox(left, 84, width, 16, errorMsg_.c_str(),
                     gfx::Font::kLabel, gfx::kBlack);
    widgets::buttonMarkers(canvas);
    return;
  }

  if (showingBudget_) {
    drawBudget(canvas);
  } else {
    drawGraph(canvas);
  }
  widgets::buttonMarkers(canvas);
}

void ExpenseScreen::drawGraph(gfx::Canvas& canvas) {
  const int left = theme::kMargin;
  const int width = canvas.width() - 2 * theme::kMargin - 6;

  // Smaller Monthly Total Text
  String monthStr = "RS " + String((int)monthTotal_);
  canvas.textInBox(left, 28, width, 16, monthStr.c_str(),
                   gfx::Font::kTitle, gfx::kBlack, gfx::Align::kCenter);
  canvas.textInBox(left, 46, width, 10, "MONTH TOTAL", gfx::Font::kMicro,
                   gfx::kBlack, gfx::Align::kCenter, 1);

  // Graph
  float peak = 1.0f;
  for (int i = 0; i < 7; i++) {
    if (daily_[i] > peak) peak = daily_[i];
  }

  constexpr int kChartTop = 75;
  constexpr int kChartHeight = 48;
  const int slot = width / 7;
  const int barW = slot - 4;
  
  const char* days[7] = {"S", "M", "T", "W", "T", "F", "S"};

  for (int i = 0; i < 7; i++) {
    const int x = left + i * slot + 2;
    int h = (int)((daily_[i] * kChartHeight) / peak);
    if (h < 2 && daily_[i] > 0) h = 2;
    
    const int barY = kChartTop + kChartHeight - h;

    if (h > 0) {
      canvas.roundRect(x, barY, barW, h, 2, gfx::kBlack);

      // Value on top of bar
      String valStr;
      if (daily_[i] >= 1000) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1fK", daily_[i] / 1000.0f);
        valStr = buf;
      } else {
        valStr = String((int)daily_[i]);
      }
      canvas.textInBox(x - 2, barY - 10, barW + 4, 9, valStr.c_str(),
                       gfx::Font::kMicro, gfx::kBlack, gfx::Align::kCenter, 1);
    }

    // Day label below bar
    canvas.textInBox(x, kChartTop + kChartHeight + 4, barW, 10,
                     days[i], gfx::Font::kMicro, gfx::kBlack,
                     gfx::Align::kCenter, 1);
  }
  canvas.rect(left, kChartTop + kChartHeight + 1, width, 1, gfx::kBlack);

  int budget = services::settings::expenseBudget();
  if (budget > 0) {
    char footer[32];
    int pct = (int)((monthTotal_ * 100) / budget);
    snprintf(footer, sizeof(footer), "%d%% OF %d BUDGET", pct, budget);
    canvas.textInBox(left, canvas.height() - 22, width, 10, footer,
                     gfx::Font::kMicro, gfx::kBlack, gfx::Align::kCenter, 2);
  }
}

void ExpenseScreen::drawBudget(gfx::Canvas& canvas) {
  const int left = theme::kMargin;
  const int width = canvas.width() - 2 * theme::kMargin - 6;

  int budget = services::settings::expenseBudget();
  String budgetStr = "RS " + String(budget);

  canvas.textInBox(left, 80, width, 24, budgetStr.c_str(),
                   gfx::Font::kDisplay, gfx::kBlack, gfx::Align::kCenter);
  canvas.textInBox(left, 110, width, 12, "MONTHLY BUDGET", gfx::Font::kMicro,
                   gfx::kBlack, gfx::Align::kCenter, 2);
}

bool ExpenseScreen::onEvent(ui::Router& router, input::Button button,
                            input::Event event) {
  if (event == input::Event::kClick) {
    if (button == input::Button::kA) {
      if (showingBudget_) {
        // Increase by 1000, wrap around at 100000 maybe? Let's just wrap at 100k
        int budget = services::settings::expenseBudget() + 1000;
        if (budget > 100000) budget = 1000;
        services::settings::setExpenseBudget(budget);
      } else {
        showingBudget_ = true;
      }
      router.invalidate(epaper::Refresh::kFull);
      return true;
    } else if (button == input::Button::kB) {
      if (showingBudget_) {
        showingBudget_ = false;
        router.invalidate(epaper::Refresh::kFull);
        return true;
      }
    }
  }
  
  if (button == input::Button::kA && event == input::Event::kLongPress) {
    if (!showingBudget_) {
      phase_ = net::isJoined() ? Phase::kFetching : Phase::kConnecting;
      if (!net::isJoined()) net::join();
      router.invalidate(epaper::Refresh::kFull);
      return true;
    }
  }

  return false;
}

}  // namespace apps
