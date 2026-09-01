#include "note_browser.h"

#include <Arduino.h>

#include "../audio/player.h"
#include "../display/epaper.h"
#include "../services/notes.h"
#include "../services/rtc.h"
#include "../services/settings.h"
#include "../util/str.h"
#include "../ui/router.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"

namespace apps {

namespace widgets = ui::widgets;
namespace theme = ui::theme;
namespace notes = services::notes;
namespace settings = services::settings;

namespace {

constexpr int kPad = 13;

// Header with a rule, shared by both screens here.
int drawHeader(gfx::Canvas& canvas, const char* title, const char* right) {
  const int left = kPad;
  const int rightEdge = canvas.width() - kPad - 6;

  canvas.text(left, 12, title, gfx::Font::kLabel, gfx::kBlack);
  int rightW = 0;
  if (right && *right) {
    rightW = canvas.textWidth(right, gfx::Font::kBody);
    canvas.text(rightEdge - rightW, 13, right, gfx::Font::kBody, gfx::kBlack);
  }
  const int titleW = canvas.textWidth(title, gfx::Font::kLabel);
  const int ruleX = left + titleW + 8;
  const int ruleEnd = rightEdge - (rightW ? rightW + 8 : 0);
  if (ruleEnd > ruleX) canvas.rect(ruleX, 18, ruleEnd - ruleX, 4, gfx::kBlack);
  return 34;
}

void drawPageDots(gfx::Canvas& canvas, int page, int pages) {
  if (pages <= 1) return;
  const int gap = 10;
  const int startX = (canvas.width() - (pages - 1) * gap) / 2;
  const int y = canvas.height() - 18;
  for (int p = 0; p < pages; p++) {
    const int x = startX + p * gap;
    if (p == page) {
      canvas.circle(x, y, 3, gfx::kBlack);
    } else {
      canvas.circleOutline(x, y, 3, 1, gfx::kBlack);
    }
  }
}

}  // namespace

// ── Tag picker ────────────────────────────────────────────────────────────

void NoteTags::onEnter(ui::Router& router) {
  router.invalidate(epaper::Refresh::kFull);
}

void NoteTags::draw(gfx::Canvas& canvas) {
  const int left = kPad;
  const int width = canvas.width() - 2 * kPad - 6;

  widgets::hudFrame(canvas, 3, 3, canvas.width() - 6, canvas.height() - 6, 15,
                    2);

  char total[16];
  snprintf(total, sizeof(total), "%d", notes::count());
  drawHeader(canvas, "Notes", total);

  // Row 0 is "All"; the rest are the configured tags.
  const int rows = settings::tagCount() + 1;
  constexpr int kRowsPerPage = 4;
  const int page = cursor_ / kRowsPerPage;
  const int firstRow = page * kRowsPerPage;
  const int shown = min(kRowsPerPage, rows - firstRow);

  constexpr int kRowH = 28;
  constexpr int kRowGap = 4;

  for (int i = 0; i < shown; i++) {
    const int index = firstRow + i;
    const int y = 40 + i * (kRowH + kRowGap);
    const bool selected = (index == cursor_);

    if (selected) {
      canvas.roundRect(left, y, width, kRowH, theme::kCardRadius, gfx::kBlack);
    }
    const gfx::Ink ink = selected ? gfx::kWhite : gfx::kBlack;

    const bool isAll = (index == 0);
    const String label = isAll ? String("All") : settings::tag(index - 1);
    const int n = isAll ? notes::count() : notes::countWithTag(label.c_str());

    canvas.textInBox(left + 12, y, width - 60, kRowH, label.c_str(),
                     gfx::Font::kBody, ink, gfx::Align::kLeft);
    char countText[8];
    snprintf(countText, sizeof(countText), "%d", n);
    canvas.textInBox(left + width - 46, y, 34, kRowH, countText,
                     gfx::Font::kBody, ink, gfx::Align::kRight);
  }

  drawPageDots(canvas, page, (rows + kRowsPerPage - 1) / kRowsPerPage);
  widgets::buttonMarkers(canvas);
}

bool NoteTags::onEvent(ui::Router& router, input::Button button,
                       input::Event event) {
  const int rows = settings::tagCount() + 1;

  if (event == input::Event::kClick) {
    const int delta = (button == input::Button::kB) ? 1 : -1;
    cursor_ = (cursor_ + delta + rows) % rows;
    router.invalidate();
    return true;
  }

  if (button == input::Button::kA && event == input::Event::kLongPress) {
    if (!list_) return true;
    if (cursor_ == 0) {
      list_->setFilter(nullptr);
    } else {
      list_->setFilter(settings::tag(cursor_ - 1).c_str());
    }
    router.push(list_);
    return true;
  }
  return false;
}

// ── Note list ─────────────────────────────────────────────────────────────

void NoteList::setFilter(const char* tag) {
  filtered_ = (tag != nullptr);
  filter_ = filtered_ ? tag : "";
  cursor_ = 0;
}

const char* NoteList::filterTag() const {
  return filtered_ ? filter_.c_str() : nullptr;
}

void NoteList::onEnter(ui::Router& router) {
  cursor_ = 0;
  playingIndex_ = -1;
  router.invalidate(epaper::Refresh::kFull);
}

void NoteList::onExit() {
  if (audio::player::active()) audio::player::stop();
  playingIndex_ = -1;
}

bool NoteList::blocksSleep() const { return audio::player::active(); }

void NoteList::onTick(ui::Router& router) {
  if (!audio::player::active()) return;

  // Keep the buttons alive while audio is streaming, so playback can be
  // stopped without waiting for it to finish.
  input::sample();

  if (!audio::player::pump()) {
    audio::player::stop();
    playingIndex_ = -1;
    router.invalidate();
  }
}

void NoteList::draw(gfx::Canvas& canvas) {
  const int left = kPad;
  const int width = canvas.width() - 2 * kPad - 6;

  widgets::hudFrame(canvas, 3, 3, canvas.width() - 6, canvas.height() - 6, 15,
                    2);

  const int total = notes::countWithTag(filterTag());
  char right[12];
  snprintf(right, sizeof(right), "%d", total);
  drawHeader(canvas, filtered_ ? filter_.c_str() : "All", right);

  if (total == 0) {
    canvas.textInBox(left, 84, width, 16, "No notes here", gfx::Font::kLabel,
                     gfx::kBlack);
    canvas.textInBox(left, 108, width, 14, "Hold A on the clock",
                     gfx::Font::kBody, gfx::kBlack);
    widgets::buttonMarkers(canvas);
    return;
  }

  const int page = cursor_ / kRowsPerPage;
  const int firstRow = page * kRowsPerPage;
  const int shown = min(kRowsPerPage, total - firstRow);

  constexpr int kRowH = 38;
  constexpr int kRowGap = 4;

  for (int i = 0; i < shown; i++) {
    const int position = firstRow + i;
    const int index = notes::indexOfFiltered(filterTag(), position);
    const notes::Note* note = notes::at(index);
    if (!note) continue;

    const int y = 40 + i * (kRowH + kRowGap);
    const bool selected = (position == cursor_);

    if (selected) {
      canvas.roundRect(left, y, width, kRowH, theme::kCardRadius, gfx::kBlack);
    }
    const gfx::Ink ink = selected ? gfx::kWhite : gfx::kBlack;

    // Number and length on the first line, when it was recorded on the second.
    char head[24];
    snprintf(head, sizeof(head), "%03d   %lus", note->number,
             (unsigned long)(note->durationMs / 1000));
    canvas.text(left + 10, y + 5, head, gfx::Font::kBody, ink);

    char when[24];
    const time_t local = note->createdUtc + services::rtc::utcOffsetMinutes() * 60;
    struct tm t;
    gmtime_r(&local, &t);
    if (note->createdUtc > 1700000000) {
      snprintf(when, sizeof(when), "%02d/%02d  %02d:%02d", t.tm_mday,
               t.tm_mon + 1, t.tm_hour, t.tm_min);
    } else {
      snprintf(when, sizeof(when), "no timestamp");
    }
    canvas.text(left + 10, y + 21, when, gfx::Font::kMicro, ink, 2);

    // Playing marker on the row that is sounding, otherwise a mark showing
    // whether a transcript exists — the thing you most want to know when
    // deciding what to open.
    if (playingIndex_ == index) {
      widgets::iconPlay(canvas, left + width - 24, y + kRowH / 2 - 6, 12, ink);
    } else if (note->hasText) {
      // Three short lines: a page of text.
      const int mx = left + width - 24;
      const int my = y + kRowH / 2 - 5;
      canvas.rect(mx, my, 12, 2, ink);
      canvas.rect(mx, my + 4, 12, 2, ink);
      canvas.rect(mx, my + 8, 8, 2, ink);
    }
  }

  drawPageDots(canvas, page, (total + kRowsPerPage - 1) / kRowsPerPage);
  widgets::buttonMarkers(canvas);
}

bool NoteList::onEvent(ui::Router& router, input::Button button,
                       input::Event event) {
  const int total = notes::countWithTag(filterTag());
  if (total <= 0) return false;

  if (event == input::Event::kClick) {
    // Moving off a playing note stops it: the alternative is audio from a row
    // you are no longer looking at.
    if (audio::player::active()) {
      audio::player::stop();
      playingIndex_ = -1;
    }
    const int delta = (button == input::Button::kB) ? 1 : -1;
    cursor_ = (cursor_ + delta + total) % total;
    router.invalidate();
    return true;
  }

  // Hold A opens the note. Playback lives in there rather than out here, which
  // is what keeps this screen to the four standard gestures.
  if (button == input::Button::kA && event == input::Event::kLongPress) {
    const int index = notes::indexOfFiltered(filterTag(), cursor_);
    if (index < 0 || !detail_) return true;
    detail_->setNote(index);
    router.push(detail_);
    return true;
  }

  return false;  // hold B falls through to the Router as back
}

// ── One note ──────────────────────────────────────────────────────────────

void NoteDetail::onEnter(ui::Router& router) {
  page_ = 0;
  playing_ = false;
  router.invalidate(epaper::Refresh::kFull);
}

void NoteDetail::onExit() {
  if (audio::player::active()) audio::player::stop();
  playing_ = false;
}

bool NoteDetail::blocksSleep() const { return audio::player::active(); }

void NoteDetail::onTick(ui::Router& router) {
  if (!audio::player::active()) return;
  input::sample();  // nothing else samples while audio streams
  if (!audio::player::pump()) {
    audio::player::stop();
    playing_ = false;
    router.invalidate();
  }
}

void NoteDetail::draw(gfx::Canvas& canvas) {
  const int left = kPad;
  const int width = canvas.width() - 2 * kPad - 6;

  widgets::hudFrame(canvas, 3, 3, canvas.width() - 6, canvas.height() - 6, 15,
                    2);

  const notes::Note* note = notes::at(index_);
  if (!note) {
    canvas.textInBox(left, 90, width, 16, "Gone", gfx::Font::kLabel,
                     gfx::kBlack);
    widgets::buttonMarkers(canvas);
    return;
  }

  char header[16];
  snprintf(header, sizeof(header), "%03d", note->number);
  drawHeader(canvas, header, note->tag);

  const String text = notes::transcript(index_);
  if (text.length() == 0) {
    canvas.textInBox(left, 76, width, 16, "No transcript yet",
                     gfx::Font::kLabel, gfx::kBlack);
    canvas.textInBox(left, 100, width, 14, "Run Sync from the menu",
                     gfx::Font::kBody, gfx::kBlack);
  } else {
    const String clean = util::displayable(text);
    totalLines_ = canvas.textWrapped(left, 44, width, kLinesPerPage,
                                     clean.c_str(), gfx::Font::kBody,
                                     gfx::kBlack, page_ * kLinesPerPage);
  }

  // Footer: play state on the left, page position on the right.
  const int footerY = canvas.height() - 22;
  if (audio::player::active()) {
    widgets::iconPlay(canvas, left, footerY, 11, gfx::kBlack);
  } else {
    canvas.circleOutline(left + 5, footerY + 5, 5, 2, gfx::kBlack);
  }

  const int pages = max(1, (totalLines_ + kLinesPerPage - 1) / kLinesPerPage);
  if (pages > 1) {
    char label[12];
    snprintf(label, sizeof(label), "%d/%d", page_ + 1, pages);
    const int labelW = canvas.textWidth(label, gfx::Font::kMicro, 2);
    canvas.text(left + width - labelW, footerY, label, gfx::Font::kMicro,
                gfx::kBlack, 2);
  }

  widgets::buttonMarkers(canvas);
}

bool NoteDetail::onEvent(ui::Router& router, input::Button button,
                         input::Event event) {
  const notes::Note* note = notes::at(index_);
  if (!note) return false;

  if (event == input::Event::kClick) {
    // Page through the transcript, forwards or back.
    const int pages = max(1, (totalLines_ + kLinesPerPage - 1) / kLinesPerPage);
    const int delta = (button == input::Button::kB) ? 1 : -1;
    page_ = (page_ + delta + pages) % pages;
    router.invalidate();
    return true;
  }

  if (button == input::Button::kB) return false;  // hold B = back

  // Hold A plays or stops.
  if (audio::player::active()) {
    audio::player::stop();
    playing_ = false;
  } else {
    const String path = notes::wavPath(note->number);
    playing_ = audio::player::start(path.c_str());
  }
  router.invalidate();
  return true;
}

}  // namespace apps
