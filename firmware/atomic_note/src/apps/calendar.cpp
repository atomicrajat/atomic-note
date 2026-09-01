#include "calendar.h"

#include <Arduino.h>

#include "../display/epaper.h"
#include "../services/rtc.h"
#include "../ui/router.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"

namespace apps {

namespace widgets = ui::widgets;
namespace theme = ui::theme;

namespace {

constexpr int kPad = 10;
constexpr int kGridTop = 52;
constexpr int kCellW = 25;
constexpr int kCellH = 21;
constexpr int kGridLeft = 12;

const char* kMonthNames[] = {"JANUARY", "FEBRUARY", "MARCH",     "APRIL",
                             "MAY",     "JUNE",     "JULY",      "AUGUST",
                             "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
const char* kDayInitials[] = {"S", "M", "T", "W", "T", "F", "S"};

bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int daysInMonth(int year, int month) {
  constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year)) return 29;
  return kDays[month - 1];
}

// Day of week for a date, 0 = Sunday. Sakamoto's method — self-contained, so
// the calendar does not depend on the C library's timezone handling.
int weekdayOf(int year, int month, int day) {
  static const int kShift[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int y = year;
  if (month < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + kShift[month - 1] + day) % 7;
}

}  // namespace

void Calendar::goToToday() {
  struct tm local;
  if (services::rtc::localNow(&local)) {
    year_ = local.tm_year + 1900;
    month_ = local.tm_mon + 1;
  } else {
    // No clock: show something rather than a blank grid.
    year_ = 2026;
    month_ = 1;
  }
}

void Calendar::shiftMonth(int delta) {
  month_ += delta;
  while (month_ > 12) {
    month_ -= 12;
    year_++;
  }
  while (month_ < 1) {
    month_ += 12;
    year_--;
  }
}

void Calendar::onEnter(ui::Router& router) {
  // Always open on the current month, however it was left last time.
  goToToday();
  router.invalidate(epaper::Refresh::kFull);
}

void Calendar::draw(gfx::Canvas& canvas) {
  if (year_ == 0) goToToday();

  widgets::hudFrame(canvas, 3, 3, canvas.width() - 6, canvas.height() - 6, 15,
                    2);

  // Which day is today, if the clock knows and we are looking at that month.
  int todayDay = -1;
  struct tm local;
  if (services::rtc::localNow(&local)) {
    if (local.tm_year + 1900 == year_ && local.tm_mon + 1 == month_) {
      todayDay = local.tm_mday;
    }
  }

  // ── Header: month and year ──────────────────────────────────────────────
  char title[24];
  snprintf(title, sizeof(title), "%s %d", kMonthNames[month_ - 1], year_);
  canvas.textInBox(0, 12, canvas.width(), 14, title, gfx::Font::kLabel,
                   gfx::kBlack);
  canvas.rect(kGridLeft, 32, 7 * kCellW, 2, gfx::kBlack);

  // ── Weekday initials ────────────────────────────────────────────────────
  for (int i = 0; i < 7; i++) {
    canvas.textInBox(kGridLeft + i * kCellW, 36, kCellW, 12, kDayInitials[i],
                     gfx::Font::kMicro, gfx::kBlack, gfx::Align::kCenter, 2);
  }

  // ── Day grid ────────────────────────────────────────────────────────────
  const int firstWeekday = weekdayOf(year_, month_, 1);
  const int dayCount = daysInMonth(year_, month_);

  for (int day = 1; day <= dayCount; day++) {
    const int cellIndex = firstWeekday + day - 1;
    const int col = cellIndex % 7;
    const int row = cellIndex / 7;
    const int x = kGridLeft + col * kCellW;
    const int y = kGridTop + row * kCellH;

    char text[4];
    snprintf(text, sizeof(text), "%d", day);

    if (day == todayDay) {
      // Today is the one thing you look for, so it is inverted rather than
      // outlined — an outline at this size competes with the grid.
      canvas.roundRect(x + 1, y, kCellW - 2, kCellH - 1, theme::kChipRadius,
                       gfx::kBlack);
      canvas.textInBox(x + 1, y, kCellW - 2, kCellH - 1, text,
                       gfx::Font::kMicro, gfx::kWhite, gfx::Align::kCenter, 2);
    } else {
      canvas.textInBox(x, y, kCellW, kCellH - 1, text, gfx::Font::kMicro,
                       gfx::kBlack, gfx::Align::kCenter, 2);
    }
  }

  widgets::buttonMarkers(canvas);
}

bool Calendar::onEvent(ui::Router& router, input::Button button,
                       input::Event event) {
  if (event == input::Event::kClick) {
    shiftMonth(button == input::Button::kB ? 1 : -1);
    router.invalidate();
    return true;
  }

  // Select, on a calendar, means "take me back to now" — the one place you
  // always want to return to after paging months away.
  if (button == input::Button::kA && event == input::Event::kLongPress) {
    goToToday();
    router.invalidate();
    return true;
  }

  return false;  // hold B falls through to the Router as back
}

}  // namespace apps
