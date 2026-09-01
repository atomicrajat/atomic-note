#include "usage_screen.h"

#include <Arduino.h>

#include "../display/epaper.h"
#include "../services/network.h"
#include "../ui/router.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"

namespace apps {

namespace widgets = ui::widgets;
namespace theme = ui::theme;
namespace net = services::network;

namespace {

// Weekly totals reach into the billions, so a raw figure is unreadable at this
// size. Short scale, one decimal, which is how anyone would say it aloud.
String humanTokens(uint64_t value) {
  char buf[16];
  if (value >= 1000000000ULL) {
    snprintf(buf, sizeof(buf), "%.2fB", value / 1000000000.0);
  } else if (value >= 1000000ULL) {
    snprintf(buf, sizeof(buf), "%.1fM", value / 1000000.0);
  } else if (value >= 1000ULL) {
    snprintf(buf, sizeof(buf), "%.1fK", value / 1000.0);
  } else {
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
  }
  return String(buf);
}

// "4h 06m", or "12m" when under an hour — a countdown you read at a glance.
String humanDuration(uint32_t seconds) {
  char buf[16];
  if (seconds >= 86400) {
    snprintf(buf, sizeof(buf), "%lud %luh", (unsigned long)(seconds / 86400),
             (unsigned long)((seconds % 86400) / 3600));
  } else if (seconds >= 3600) {
    snprintf(buf, sizeof(buf), "%luh %02lum", (unsigned long)(seconds / 3600),
             (unsigned long)((seconds % 3600) / 60));
  } else {
    snprintf(buf, sizeof(buf), "%lum", (unsigned long)(seconds / 60));
  }
  return String(buf);
}

// A labelled gauge: name, percentage, bar, and what it resets to.
void drawGauge(gfx::Canvas& canvas, int x, int y, int w, const char* label,
               int percent, uint32_t resetsIn, const String& used) {
  canvas.text(x, y, label, gfx::Font::kMicro, gfx::kBlack, 2);

  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", percent);
  const int pctW = canvas.textWidth(pct, gfx::Font::kTitle);
  canvas.text(x + w - pctW, y - 4, pct, gfx::Font::kTitle, gfx::kBlack);

  constexpr int kBarH = 14;
  const int barY = y + 16;
  canvas.roundRectOutline(x, barY, w, kBarH, theme::kChipRadius, 2,
                          gfx::kBlack);
  const int inner = w - 8;
  int fill = (inner * (percent > 100 ? 100 : percent)) / 100;
  if (fill < 2 && percent > 0) fill = 2;
  if (fill > 0) canvas.rect(x + 4, barY + 4, fill, kBarH - 8, gfx::kBlack);

  // Under the bar: how much, and how long until it clears.
  String footer = used + "  resets " + humanDuration(resetsIn);
  canvas.text(x, barY + kBarH + 4, footer.c_str(), gfx::Font::kMicro,
              gfx::kBlack, 2);
}

}  // namespace

void UsageScreen::onEnter(ui::Router& router) {
  radioWasUp_ = net::isJoined();
  data_ = services::claude_usage::Snapshot();

  if (net::isJoined()) {
    phase_ = Phase::kFetching;
  } else {
    phase_ = Phase::kConnecting;
    net::join();
  }
  router.invalidate(epaper::Refresh::kFull);
}

void UsageScreen::onExit() {
  if (!radioWasUp_) net::stop();
}

void UsageScreen::onTick(ui::Router& router) {
  if (phase_ == Phase::kReady) return;

  if (phase_ == Phase::kConnecting) {
    if (net::isJoined()) {
      phase_ = Phase::kFetching;
      router.invalidate(epaper::Refresh::kFull);
      return;
    }
    if (net::state() == net::State::kFailed) {
      phase_ = Phase::kReady;
      data_.error = "No network";
      router.invalidate(epaper::Refresh::kFull);
    }
    return;
  }

  limits_ = services::claude_usage::fetchLimits();
  data_ = services::claude_usage::fetch();
  phase_ = Phase::kReady;
  router.invalidate(epaper::Refresh::kFull);
}

void UsageScreen::draw(gfx::Canvas& canvas) {
  const int w = canvas.width();
  const int left = theme::kMargin;
  const int width = w - 2 * theme::kMargin - 6;

  widgets::hudFrame(canvas, 3, 3, w - 6, canvas.height() - 6, 15, 2);

  const char* title = showingWeekBars_ ? "History" : "Claude";
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

  if (showingWeekBars_) {
    drawHistory(canvas);
  } else {
    drawLimits(canvas);
  }
  widgets::buttonMarkers(canvas);
}

void UsageScreen::drawLimits(gfx::Canvas& canvas) {
  const int left = theme::kMargin;
  const int width = canvas.width() - 2 * theme::kMargin - 6;

  if (!limits_.valid) {
    canvas.textInBox(left, 84, width, 16, limits_.error.c_str(),
                     gfx::Font::kLabel, gfx::kBlack);
    canvas.textInBox(left, 108, width, 14, "A retry", gfx::Font::kBody,
                     gfx::kBlack);
    return;
  }

  drawGauge(canvas, left, 40, width, "5 HOUR BLOCK", limits_.blockPercent,
            limits_.blockResetsInSec, humanTokens(limits_.blockUsed));

  drawGauge(canvas, left, 100, width, "THIS WEEK", limits_.weekPercent,
            limits_.weekResetsInSec, humanTokens(limits_.weekUsed));

  // Whether these are real or guessed is the most important thing on the
  // screen. Uncalibrated, the inferred ceiling read 10% when the truth was
  // 93% — so it says so plainly rather than showing a confident wrong number.
  canvas.textInBox(left, canvas.height() - 22, width, 10,
                   limits_.calibrated ? "CALIBRATED"
                                      : "ESTIMATE - CALIBRATE IN WEB APP",
                   gfx::Font::kMicro, gfx::kBlack, gfx::Align::kCenter, 2);
}

void UsageScreen::drawHistory(gfx::Canvas& canvas) {
  const int left = theme::kMargin;
  const int width = canvas.width() - 2 * theme::kMargin - 6;

  if (!data_.valid) {
    canvas.textInBox(left, 84, width, 16, data_.error.c_str(),
                     gfx::Font::kLabel, gfx::kBlack);
    return;
  }

  canvas.textInBox(left, 32, width, 24, humanTokens(data_.totalTokens).c_str(),
                   gfx::Font::kDisplay, gfx::kBlack);
  canvas.textInBox(left, 62, width, 12, "TOKENS THIS WEEK", gfx::Font::kMicro,
                   gfx::kBlack, gfx::Align::kCenter, 2);

  // Scaled to the busiest day: absolute scale is meaningless when one day can
  // be a hundred times another.
  uint32_t peak = 1;
  for (int i = 0; i < data_.dayCount; i++) {
    if (data_.dayTokens[i] > peak) peak = data_.dayTokens[i];
  }

  constexpr int kChartTop = 84;
  constexpr int kChartHeight = 52;
  const int slot = width / (data_.dayCount > 0 ? data_.dayCount : 1);
  const int barW = slot - 6;

  for (int i = 0; i < data_.dayCount; i++) {
    const int x = left + i * slot + 3;
    int h = (int)(((uint64_t)data_.dayTokens[i] * kChartHeight) / peak);
    if (h < 2 && data_.dayTokens[i] > 0) h = 2;
    if (h > 0) {
      canvas.roundRect(x, kChartTop + kChartHeight - h, barW, h,
                       theme::kChipRadius, gfx::kBlack);
    }
    canvas.textInBox(x - 2, kChartTop + kChartHeight + 4, barW + 4, 10,
                     data_.dayLabel[i], gfx::Font::kMicro, gfx::kBlack,
                     gfx::Align::kCenter, 2);
  }
  canvas.rect(left, kChartTop + kChartHeight + 1, width, 1, gfx::kBlack);

  char footer[40];
  snprintf(footer, sizeof(footer), "~$%lu AT API RATES",
           (unsigned long)(data_.totalCents / 100));
  canvas.textInBox(left, canvas.height() - 22, width, 10, footer,
                   gfx::Font::kMicro, gfx::kBlack, gfx::Align::kCenter, 2);
}

bool UsageScreen::onEvent(ui::Router& router, input::Button button,
                          input::Event event) {
  // Two views, so either tap flips between them — "previous" and "next" are
  // the same move when there are only two.
  if (event == input::Event::kClick) {
    showingWeekBars_ = !showingWeekBars_;
    router.invalidate(epaper::Refresh::kFull);
    return true;
  }

  // Select refetches.
  if (button == input::Button::kA && event == input::Event::kLongPress) {
    phase_ = net::isJoined() ? Phase::kFetching : Phase::kConnecting;
    if (!net::isJoined()) net::join();
    router.invalidate(epaper::Refresh::kFull);
    return true;
  }
  return false;
}

}  // namespace apps
