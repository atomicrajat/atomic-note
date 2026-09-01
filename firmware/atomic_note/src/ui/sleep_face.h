// The image the panel keeps while the device is asleep.
//
// E-paper holds its last frame with zero current, so whatever is on screen
// when we power down stays there for free — for days, for the whole time the
// battery lasts. Leaving the last app on screen made a sleeping device look
// like a frozen one, which is the only real problem this solves.
//
// To be clear about what this does NOT do: it saves no power. Deep sleep is
// where the battery life comes from, and this costs one extra refresh on the
// way down. It is here so that a device which is working correctly looks like
// it is working correctly.
#pragma once

#include "../display/canvas.h"

namespace ui {

void drawSleepFace(gfx::Canvas& canvas);

}  // namespace ui
