#include "sleep_face.h"

#include <stdio.h>

#include "../board/power.h"
#include "theme.h"
#include "widgets.h"

namespace ui {

void drawSleepFace(gfx::Canvas& canvas) {
  const int w = canvas.width();
  const int h = canvas.height();

  canvas.fill(gfx::kWhite);

  // The name, stacked, filling the panel. Two short words one above the other
  // read at arm's length across a room; one long line at this width does not.
  canvas.textInBox(0, 58, w, 30, "ATOMIC", gfx::Font::kJumbo, gfx::kBlack,
                   gfx::Align::kCenter);
  canvas.textInBox(0, 100, w, 30, "NOTE", gfx::Font::kJumbo, gfx::kBlack,
                   gfx::Align::kCenter);

  // A rule between the name and the status line, so the bottom of the panel
  // reads as a separate register rather than as more of the title.
  const int ruleY = 148;
  canvas.rect(w / 4, ruleY, w / 2, 2, gfx::kBlack);

  // Charge, because a sleeping device gives no other clue and this is the one
  // number worth knowing before you pick it up. Held for free by the panel.
  const int pct = power::batteryPercent();
  char status[24];
  if (pct >= 0) {
    snprintf(status, sizeof(status), "%d%%", pct);
  } else {
    snprintf(status, sizeof(status), "--");
  }
  canvas.textInBox(0, ruleY + 12, w, 14, status, gfx::Font::kLabel,
                   gfx::kBlack, gfx::Align::kCenter);

  canvas.textInBox(0, h - 26, w, 10, "PRESS ANY BUTTON", gfx::Font::kMicro,
                   gfx::kBlack, gfx::Align::kCenter, 2);

  widgets::maskScreenCorners(canvas);
}

}  // namespace ui
