#include "agenda_screen.h"

#include <Arduino.h>

#include "../display/epaper.h"
#include "../services/network.h"
#include "../services/rtc.h"
#include "../ui/router.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"
#include "../util/str.h"

namespace apps {

namespace widgets = ui::widgets;
namespace theme = ui::theme;
namespace net = services::network;
namespace feed = services::calendar_feed;

namespace {

// ── Timeline geometry ─────────────────────────────────────────────────────
// One column for the time, one for the spine, the rest for the title. Fixed
// columns are what make the times line up down the page — laying each row out
// from its own text width is what made the previous version look ragged.
constexpr int kLeft = 10;
constexpr int kTimeW = 40;          // right-aligned times
constexpr int kSpineX = kLeft + kTimeW + 9;
constexpr int kTextX = kSpineX + 11;
constexpr int kRowH = 27;
constexpr int kListTop = 38;
constexpr int kDotR = 4;

const char* kWeekdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const char* kMonths[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

struct LocalTime {
  struct tm t;
  int dayNumber;
};

LocalTime localise(time_t utc) {
  LocalTime out;
  const time_t local = utc + (time_t)services::rtc::utcOffsetMinutes() * 60;
  gmtime_r(&local, &out.t);
  out.dayNumber = (int)(local / 86400);
  return out;
}

int todayNumber() {
  const time_t local =
      time(nullptr) + (time_t)services::rtc::utcOffsetMinutes() * 60;
  return (int)(local / 86400);
}

}  // namespace

void AgendaScreen::onEnter(ui::Router& router) {
  radioWasUp_ = net::isJoined();
  cursor_ = 0;
  showingDetail_ = false;
  agenda_ = feed::Agenda();

  if (net::isJoined()) {
    phase_ = Phase::kFetching;
  } else {
    phase_ = Phase::kConnecting;
    net::join();
  }
  router.invalidate(epaper::Refresh::kFull);
}

void AgendaScreen::onExit() {
  if (!radioWasUp_) net::stop();
}

void AgendaScreen::onTick(ui::Router& router) {
  if (phase_ == Phase::kReady) return;

  if (phase_ == Phase::kConnecting) {
    if (net::isJoined()) {
      phase_ = Phase::kFetching;
      router.invalidate(epaper::Refresh::kFull);
      return;
    }
    if (net::state() == net::State::kFailed) {
      phase_ = Phase::kReady;
      agenda_.error = "No network";
      router.invalidate(epaper::Refresh::kFull);
    }
    return;
  }

  agenda_ = feed::fetch();
  phase_ = Phase::kReady;
  router.invalidate(epaper::Refresh::kFull);
}

void AgendaScreen::draw(gfx::Canvas& canvas) {
  widgets::hudFrame(canvas, 3, 3, canvas.width() - 6, canvas.height() - 6, 15,
                    2);

  if (phase_ != Phase::kReady) {
    canvas.textInBox(0, 90, canvas.width(), 16,
                     phase_ == Phase::kConnecting ? "Connecting" : "Fetching",
                     gfx::Font::kTitle, gfx::kBlack);
    widgets::buttonMarkers(canvas);
    return;
  }

  if (showingDetail_) {
    drawDetail(canvas);
  } else {
    drawTimeline(canvas);
  }
  widgets::buttonMarkers(canvas);
}

void AgendaScreen::drawTimeline(gfx::Canvas& canvas) {
  const int right = canvas.width() - 10 - 6;

  canvas.text(kLeft, 11, "Agenda", gfx::Font::kLabel, gfx::kBlack);
  const int titleW = canvas.textWidth("Agenda", gfx::Font::kLabel);
  if (right > kLeft + titleW + 8) {
    canvas.rect(kLeft + titleW + 8, 17, right - kLeft - titleW - 8, 3,
                gfx::kBlack);
  }

  if (!agenda_.valid || agenda_.count == 0) {
    const char* headline = !agenda_.configured  ? "No calendars"
                           : agenda_.valid      ? "Nothing today"
                                                : agenda_.error.c_str();
    canvas.textInBox(kLeft, 84, right - kLeft, 16, headline, gfx::Font::kLabel,
                     gfx::kBlack);
    if (agenda_.valid && agenda_.configured) {
      canvas.textInBox(kLeft, 108, right - kLeft, 14, "or tomorrow",
                       gfx::Font::kBody, gfx::kBlack);
    }
    return;
  }

  const int today = todayNumber();
  const int page = cursor_ / kRowsPerPage;
  const int first = page * kRowsPerPage;
  const int shown = min(kRowsPerPage, agenda_.count - first);

  // The spine runs between the first and last dots, not the full height —
  // a line dangling past the last event reads as "more below" when there is
  // nothing more.
  const int firstDotY = kListTop + kRowH / 2;
  const int lastDotY = kListTop + (shown - 1) * kRowH + kRowH / 2;
  if (shown > 1) {
    canvas.rect(kSpineX - 1, firstDotY, 2, lastDotY - firstDotY, gfx::kBlack);
  }

  for (int i = 0; i < shown; i++) {
    const int index = first + i;
    const feed::Event& e = agenda_.events[index];
    const LocalTime when = localise(e.startUtc);
    const int y = kListTop + i * kRowH;
    const bool selected = (index == cursor_);

    if (selected) {
      // The band spans the text column only, so the spine and times stay
      // readable and the row still reads as part of the timeline.
      canvas.roundRect(kTextX - 6, y + 1, right - kTextX + 6, kRowH - 3,
                       theme::kCardRadius, gfx::kBlack);
    }

    // Time, right-aligned in its column so every colon lines up.
    char timeText[8];
    if (e.allDay) {
      snprintf(timeText, sizeof(timeText), "all");
    } else {
      snprintf(timeText, sizeof(timeText), "%02d:%02d", when.t.tm_hour,
               when.t.tm_min);
    }
    canvas.textInBox(kLeft, y, kTimeW, kRowH, timeText, gfx::Font::kBody,
                     gfx::kBlack, gfx::Align::kRight);

    // Dot on the spine — filled for today, hollow for tomorrow, so the day is
    // legible without a heading on every row.
    const int dotY = y + kRowH / 2;
    if (when.dayNumber == today) {
      canvas.circle(kSpineX, dotY, kDotR, gfx::kBlack);
    } else {
      canvas.circle(kSpineX, dotY, kDotR, gfx::kBlack);
      canvas.circle(kSpineX, dotY, kDotR - 2, gfx::kWhite);
    }

    canvas.textInBox(kTextX, y, right - kTextX - 4, kRowH, e.summary,
                     gfx::Font::kBody, selected ? gfx::kWhite : gfx::kBlack,
                     gfx::Align::kLeft);
  }

  // Footer: which day the page is showing, and the page dots.
  const LocalTime firstShown = localise(agenda_.events[first].startUtc);
  const char* dayLabel = firstShown.dayNumber == today ? "TODAY" : "TOMORROW";
  canvas.text(kLeft, canvas.height() - 20, dayLabel, gfx::Font::kMicro,
              gfx::kBlack, 2);

  const int pages = (agenda_.count + kRowsPerPage - 1) / kRowsPerPage;
  if (pages > 1) {
    const int gap = 9;
    for (int p = 0; p < pages; p++) {
      const int x = right - (pages - 1 - p) * gap;
      if (p == page) {
        canvas.circle(x, canvas.height() - 16, 3, gfx::kBlack);
      } else {
        canvas.circleOutline(x, canvas.height() - 16, 3, 1, gfx::kBlack);
      }
    }
  }
}

void AgendaScreen::drawDetail(gfx::Canvas& canvas) {
  const int right = canvas.width() - 10 - 6;
  const int width = right - kLeft;

  if (cursor_ < 0 || cursor_ >= agenda_.count) return;
  const feed::Event& e = agenda_.events[cursor_];
  const LocalTime start = localise(e.startUtc);
  const int today = todayNumber();

  // Header band: the when, inverted, so the title below has the whole screen.
  canvas.roundRect(kLeft - 4, 8, width + 8, 34, theme::kCardRadius,
                   gfx::kBlack);

  char when[40];
  if (e.allDay) {
    snprintf(when, sizeof(when), "%s  ALL DAY",
             start.dayNumber == today ? "TODAY" : "TOMORROW");
  } else if (e.endUtc > e.startUtc) {
    const LocalTime end = localise(e.endUtc);
    snprintf(when, sizeof(when), "%02d:%02d - %02d:%02d", start.t.tm_hour,
             start.t.tm_min, end.t.tm_hour, end.t.tm_min);
  } else {
    snprintf(when, sizeof(when), "%02d:%02d", start.t.tm_hour, start.t.tm_min);
  }
  canvas.textInBox(kLeft - 4, 8, width + 8, 34, when, gfx::Font::kLabel,
                   gfx::kWhite);

  // Date line.
  char date[24];
  snprintf(date, sizeof(date), "%s %d %s", kWeekdays[start.t.tm_wday % 7],
           start.t.tm_mday, kMonths[start.t.tm_mon % 12]);
  canvas.text(kLeft, 48, date, gfx::Font::kMicro, gfx::kBlack, 2);

  // Title, wrapped — the whole point of opening the event.
  const String title = util::displayable(String(e.summary));
  const int usedLines =
      canvas.textWrapped(kLeft, 66, width, 3, title.c_str(), gfx::Font::kBody,
                         gfx::kBlack);

  int y = 66 + min(usedLines, 3) * 18 + 6;

  if (e.location[0] != '\0') {
    canvas.rect(kLeft, y, width, 1, gfx::kBlack);
    y += 6;
    const String location = util::displayable(String(e.location));
    canvas.textWrapped(kLeft, y, width, 2, location.c_str(), gfx::Font::kBody,
                       gfx::kBlack);
    y += 26;
  }

  if (e.description[0] != '\0' && y < canvas.height() - 40) {
    const String description = util::displayable(String(e.description));
    const int lines = (canvas.height() - 30 - y) / 18;
    if (lines > 0) {
      canvas.textWrapped(kLeft, y, width, lines, description.c_str(),
                         gfx::Font::kBody, gfx::kBlack);
    }
  }
}

bool AgendaScreen::onEvent(ui::Router& router, input::Button button,
                           input::Event event) {
  if (showingDetail_) {
    // Hold B backs out of the DETAIL, not the screen — one level at a time is
    // what back means everywhere else, so it means that here too.
    if (button == input::Button::kB && event == input::Event::kLongPress) {
      showingDetail_ = false;
      router.invalidate(epaper::Refresh::kFull);
      return true;
    }
    // Taps walk the events without leaving the detail view.
    if (event == input::Event::kClick && agenda_.count > 0) {
      const int delta = (button == input::Button::kB) ? 1 : -1;
      cursor_ = (cursor_ + delta + agenda_.count) % agenda_.count;
      router.invalidate(epaper::Refresh::kFull);
      return true;
    }
    return true;  // swallow the rest; hold A has nothing to select in here
  }

  if (agenda_.count == 0) {
    // Nothing to move through; select refetches.
    if (event == input::Event::kLongPress && button == input::Button::kA) {
      phase_ = net::isJoined() ? Phase::kFetching : Phase::kConnecting;
      if (!net::isJoined()) net::join();
      router.invalidate(epaper::Refresh::kFull);
      return true;
    }
    return false;
  }

  if (event == input::Event::kClick) {
    const int delta = (button == input::Button::kB) ? 1 : -1;
    cursor_ = (cursor_ + delta + agenda_.count) % agenda_.count;
    router.invalidate();
    return true;
  }

  if (button == input::Button::kA && event == input::Event::kLongPress) {
    showingDetail_ = true;
    router.invalidate(epaper::Refresh::kFull);
    return true;
  }

  return false;  // hold B falls through to the Router as back
}

}  // namespace apps
