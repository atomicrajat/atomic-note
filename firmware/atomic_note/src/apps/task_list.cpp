#include "task_list.h"

#include <Arduino.h>

#include "../audio/sound.h"
#include "../display/epaper.h"
#include "../services/storage.h"
#include "../services/tasks.h"
#include "../ui/router.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"

namespace apps {

namespace widgets = ui::widgets;
namespace theme = ui::theme;
namespace tasks = services::tasks;

namespace {

constexpr int kPad = 13;
constexpr int kListY = 40;
constexpr int kRowH = 32;
constexpr int kRowGap = 3;
constexpr int kBoxSize = 16;

// A checkbox, ticked or empty.
void checkbox(gfx::Canvas& canvas, int x, int y, bool done, gfx::Ink ink) {
  canvas.roundRectOutline(x, y, kBoxSize, kBoxSize, theme::kChipRadius, 2,
                          ink);
  if (done) {
    widgets::iconCheck(canvas, x + 3, y + 3, kBoxSize - 6, ink);
  }
}

}  // namespace

void TaskList::onEnter(ui::Router& router) {
  if (cursor_ >= tasks::count()) cursor_ = 0;
  router.invalidate(epaper::Refresh::kFull);
}

void TaskList::draw(gfx::Canvas& canvas) {
  const int left = kPad;
  const int right = canvas.width() - kPad - 6;  // clear the button markers
  const int width = right - left;

  widgets::hudFrame(canvas, 3, 3, canvas.width() - 6, canvas.height() - 6, 15,
                    2);

  // Title with a live count of what is left, which is the number that matters.
  canvas.text(left, 12, "Tasks", gfx::Font::kLabel, gfx::kBlack);
  char counter[16];
  snprintf(counter, sizeof(counter), "%d left", tasks::remainingCount());
  const int counterW = canvas.textWidth(counter, gfx::Font::kBody);
  canvas.text(right - counterW, 13, counter, gfx::Font::kBody, gfx::kBlack);

  const int titleW = canvas.textWidth("Tasks", gfx::Font::kLabel);
  const int ruleX = left + titleW + 8;
  const int ruleEnd = right - counterW - 8;
  if (ruleEnd > ruleX) {
    canvas.rect(ruleX, 18, ruleEnd - ruleX, 4, gfx::kBlack);
  }

  const int total = tasks::count();
  if (total == 0) {
    const char* line1 = services::storage::available()
                            ? "Nothing to do"
                            : "No SD card";
    const char* line2 = services::storage::available()
                            ? "Add from the web app"
                            : "Tasks need a card";
    canvas.textInBox(left, 80, width, 16, line1, gfx::Font::kLabel,
                     gfx::kBlack);
    canvas.textInBox(left, 104, width, 14, line2, gfx::Font::kBody,
                     gfx::kBlack);
    widgets::buttonMarkers(canvas);
    return;
  }

  // Page rather than scroll: every repaint is a whole frame anyway, and a
  // jumping viewport is easier to follow on e-paper than a sliding one.
  const int page = cursor_ / kRowsPerPage;
  const int firstRow = page * kRowsPerPage;
  const int rowsShown = min(kRowsPerPage, total - firstRow);

  for (int i = 0; i < rowsShown; i++) {
    const int index = firstRow + i;
    const tasks::Task* task = tasks::at(index);
    if (!task) continue;

    const int y = kListY + i * (kRowH + kRowGap);
    const bool selected = (index == cursor_);

    if (selected) {
      canvas.roundRect(left, y, width, kRowH, theme::kCardRadius, gfx::kBlack);
    }
    const gfx::Ink ink = selected ? gfx::kWhite : gfx::kBlack;

    checkbox(canvas, left + 8, y + (kRowH - kBoxSize) / 2, task->done, ink);

    const int textX = left + 8 + kBoxSize + 8;
    const int textW = width - (textX - left) - 8;
    canvas.textInBox(textX, y, textW, kRowH, task->text, gfx::Font::kBody, ink,
                     gfx::Align::kLeft);

    // Strike completed tasks through, so done-ness survives the row not being
    // selected and reads at a glance down the list.
    if (task->done) {
      const int strikeW = min(canvas.textWidth(task->text, gfx::Font::kBody),
                              textW);
      canvas.rect(textX, y + kRowH / 2, strikeW, 1, ink);
    }
  }

  const int pages = (total + kRowsPerPage - 1) / kRowsPerPage;
  if (pages > 1) {
    const int dotY = canvas.height() - 18;
    const int gap = 10;
    const int startX = left + (width - (pages - 1) * gap) / 2;
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

bool TaskList::onEvent(ui::Router& router, input::Button button,
                       input::Event event) {
  const int total = tasks::count();
  if (total <= 0) return false;

  if (event == input::Event::kClick) {
    const int delta = (button == input::Button::kB) ? 1 : -1;
    cursor_ = (cursor_ + delta + total) % total;
    router.invalidate();
    return true;
  }

  if (button == input::Button::kA && event == input::Event::kLongPress) {
    tasks::toggleDone(cursor_);
    audio::sound::toggle();
    router.invalidate();
    return true;
  }

  return false;  // hold B falls through to the Router as back
}

}  // namespace apps
