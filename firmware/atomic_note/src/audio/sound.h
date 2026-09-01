// UI sounds.
//
// On e-paper, sound is the only feedback channel that is actually immediate —
// a panel refresh takes half a second, so a press cannot be acknowledged
// visually in time to feel responsive. That is what these are for.
//
// Every cue is short (10-60 ms). They play synchronously, which is fine at
// that length and avoids a mixer task and its buffers.
#pragma once

#include <Arduino.h>

namespace audio {
namespace sound {

// Requires the peripheral rail up and I2C running.
bool begin();
bool available();

void setEnabled(bool on);
bool enabled();

// ── Cues ──────────────────────────────────────────────────────────────────
void click();    // moving through a list
void select();   // confirming
void back();     // leaving a screen
void toggle();   // ticking a task
void success();  // a run finished, a sync landed
void alert();    // a reminder — deliberately the only insistent one

// Silence the amplifier. Called before recording, and before sleeping.
void quiet();

}  // namespace sound
}  // namespace audio
