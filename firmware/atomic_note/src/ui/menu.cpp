#include "menu.h"

#include <Arduino.h>

#include "router.h"
#include "theme.h"
#include "widgets.h"

namespace ui {
namespace {

constexpr int kFrameInset = 3;
constexpr int kFrameChamfer = 15;
constexpr int kPad = 13;

constexpr int kTitleY = 12;
constexpr int kListY = 40;
constexpr int kRowH = 30;
constexpr int kRowGap = 4;

}  // namespace

bool Menu::usable(int index) const {
  if (index < 0 || index >= count_) return false;
  const MenuEntry& entry = entries_[index];
  if (entry.screen == nullptr) return false;
  return entry.available == nullptr || entry.available();
}

void Menu::onEnter(Router& router) { router.invalidate(); }

void Menu::draw(gfx::Canvas& canvas) {
  const int contentLeft = kPad;
  const int contentRight = canvas.width() - kPad - 6;  // clear the markers
  const int contentW = contentRight - contentLeft;

  widgets::hudFrame(canvas, kFrameInset, kFrameInset,
                    canvas.width() - 2 * kFrameInset,
                    canvas.height() - 2 * kFrameInset, kFrameChamfer, 2);

  // Title, with a rule running out to the edge of the content column.
  canvas.text(contentLeft, kTitleY, title_, gfx::Font::kLabel, gfx::kBlack);
  const int titleW = canvas.textWidth(title_, gfx::Font::kLabel);
  const int ruleX = contentLeft + titleW + 8;
  if (contentRight > ruleX) {
    canvas.rect(ruleX, kTitleY + 6, contentRight - ruleX, 4, gfx::kBlack);
  }

  if (count_ <= 0) {
    canvas.textInBox(contentLeft, kListY, contentW, kRowH, "Nothing here",
                     gfx::Font::kBody, gfx::kBlack);
    widgets::buttonMarkers(canvas);
    return;
  }

  // Page the list so the cursor is always on screen, without scrolling the
  // rows one at a time — on e-paper a jumping viewport is easier to read than
  // a sliding one, because every repaint is a full frame anyway.
  const int page = cursor_ / kRowsPerPage;
  const int firstRow = page * kRowsPerPage;
  const int rowsShown = min(kRowsPerPage, count_ - firstRow);

  for (int i = 0; i < rowsShown; i++) {
    const int index = firstRow + i;
    const int y = kListY + i * (kRowH + kRowGap);
    const bool selected = (index == cursor_);

    if (selected) {
      canvas.roundRect(contentLeft, y, contentW, kRowH, theme::kCardRadius,
                       gfx::kBlack);
    }
    // kInvert would also work, but an explicit ink keeps the disabled state
    // below readable.
    const gfx::Ink ink = selected ? gfx::kWhite : gfx::kBlack;
    canvas.textInBox(contentLeft + 12, y, contentW - 24, kRowH,
                     entries_[index].label, gfx::Font::kBody, ink,
                     gfx::Align::kLeft);

    if (!usable(index)) {
      // Struck through: either not implemented yet, or waiting on hardware
      // that is not attached. Shown rather than hidden, because a row that
      // appears when a sensor is plugged in is a discoverable feature, and a
      // menu that silently changes length is not.
      const int textW =
          canvas.textWidth(entries_[index].label, gfx::Font::kBody);
      canvas.rect(contentLeft + 12, y + kRowH / 2, textW, 1, ink);
    }
  }

  // Page indicator, only when there is more than one page.
  const int pages = (count_ + kRowsPerPage - 1) / kRowsPerPage;
  if (pages > 1) {
    const int dotY = canvas.height() - 18;
    const int gap = 10;
    const int startX = contentLeft + (contentW - (pages - 1) * gap) / 2;
    for (int p = 0; p < pages; p++) {
      const int x = startX + p * gap;
      if (p == page) {
        canvas.circle(x, dotY, 3, gfx::kBlack);
      } else {
        canvas.circleOutline(x, dotY, 3, 1, gfx::kBlack);
      }
    }
  }

  widgets::buttonMarkers(canvas);
}

bool Menu::onEvent(Router& router, input::Button button, input::Event event) {
  if (count_ <= 0) return false;

  // Tap A / tap B step the cursor. Having a reverse gesture on a TAP is the
  // whole point of this grammar: overshooting an item in a long list used to
  // mean wrapping all the way round to reach it again.
  if (event == input::Event::kClick) {
    const int delta = (button == input::Button::kB) ? 1 : -1;
    cursor_ = (cursor_ + delta + count_) % count_;
    router.invalidate();
    return true;
  }

  if (button == input::Button::kA && event == input::Event::kLongPress) {
    if (!usable(cursor_)) return true;  // disabled row, swallow the press
    Screen* target = entries_[cursor_].screen;
    router.push(target);
    return true;
  }

  // Holding B is back, handled once in the Router so every screen agrees.
  return false;
}

}  // namespace ui
