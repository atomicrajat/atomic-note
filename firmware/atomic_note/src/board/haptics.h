// Haptic feedback, on a vibration motor wired to GPIO 3.
//
// The same problem sound solves, solved a second way. A panel refresh takes
// half a second, so nothing visual can acknowledge a press in time to feel
// responsive — which is why UI sounds exist. Vibration is the other immediate
// channel, and it is the better one in the cases where sound fails: in a
// pocket, in a meeting, with the speaker switched off.
//
// It mirrors the sound cues rather than inventing its own vocabulary, and it
// is a SEPARATE preference: turn sound off and keep the buzz, keep both, or
// neither. Nothing here assumes the motor exists — driving a pin nothing is
// attached to costs a few microamps and is otherwise silent, so there is no
// detection and no configuration.
//
// ── Two things about GPIO 3 ────────────────────────────────────────────────
//
// It is a STRAPPING pin (JTAG source select), sampled at reset. Nothing here
// touches it before begin(), and a motor pulling it toward ground at reset
// selects the default anyway, so this is a note rather than a problem.
//
// It cannot drive a motor directly. An ESP32-S3 pin is rated 40 mA absolute
// and a vibration motor pulls 60-100 mA, so the module MUST have its own
// transistor and flyback diode — which the common breakouts do. A bare motor
// soldered to the pin will damage it.
#pragma once

#include <Arduino.h>

namespace haptics {

// Claims the pin and drives it low. Safe to call with nothing attached.
void begin();

void setEnabled(bool on);
bool enabled();

// ── Cues, mirroring audio::sound ──────────────────────────────────────────
// Short and synchronous, for the same reason the sounds are: at these
// durations a blocking pulse is simpler than a timer and a state machine, and
// it is shorter than the audio cue it accompanies.
// Three weights, matching the three gestures. Going back gets its own rather
// than reusing the tap, for the same reason it has its own sound: confirming
// and leaving both change where you are, so they must not feel alike.
void tick();    // moving through a list
void back();    // leaving a screen
void bump();    // confirming
void alert();   // a reminder — NOT gated on the preference, as with sound

// Drive the pin low and leave it there. Called before sleeping, so a motor is
// never left energised by a cue that was still running.
void off();

// Drive the pin high for `ms`, ignoring the preference. Bring-up only: reached
// from the serial console as `buzz [ms]`, to separate "the pulse is too short"
// from "nothing is wired to this pin".
void test(uint32_t ms);

}  // namespace haptics
