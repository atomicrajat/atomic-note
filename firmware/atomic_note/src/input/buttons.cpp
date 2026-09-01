#include "buttons.h"

#include <Arduino.h>

#include "../board/pins.h"

namespace input {
namespace {

struct State {
  gpio_num_t pin;
  bool stable = false;     // debounced level: true = pressed
  bool lastRaw = false;
  uint32_t lastChangeMs = 0;
  uint32_t pressedAtMs = 0;
  bool longFired = false;  // long press already reported for this hold
};

State states[2];

// Events queue rather than being returned one per call: both buttons are
// stepped on every sample, a panel refresh can span several samples, and
// dropping any of it strands the user on a screen that never saw the press.
constexpr int kQueueSize = 8;
Reading queue[kQueueSize];
uint8_t queueHead = 0;
uint8_t queueCount = 0;

void enqueue(Button button, Event event) {
  if (queueCount >= kQueueSize) return;  // full: keep the oldest, drop this
  const uint8_t tail = (uint8_t)((queueHead + queueCount) % kQueueSize);
  queue[tail] = {button, event};
  queueCount++;
}

State& stateOf(Button b) { return states[b == Button::kA ? 0 : 1]; }

// Advance one button's state machine and return whatever it produced.
Event step(State& s, uint32_t now) {
  const bool raw = (digitalRead(s.pin) == LOW);  // active low

  if (raw != s.lastRaw) {
    s.lastRaw = raw;
    s.lastChangeMs = now;
  }

  // Accept the new level only once it has been steady long enough.
  if (raw != s.stable && (now - s.lastChangeMs) >= kDebounceMs) {
    s.stable = raw;
    if (s.stable) {
      s.pressedAtMs = now;
      s.longFired = false;
    } else if (!s.longFired) {
      // Released without having crossed the long-press threshold: that is a
      // click, and it fires NOW. There is no double-click window to wait out.
      return Event::kClick;
    }
  }

  // Long press fires while the button is still down, so the user gets the
  // feedback at the moment the gesture is recognised rather than on release.
  if (s.stable && !s.longFired && (now - s.pressedAtMs) >= kLongPressMs) {
    s.longFired = true;
    return Event::kLongPress;
  }

  return Event::kNone;
}

}  // namespace

void begin() {
  states[0].pin = pins::BTN_A;
  states[1].pin = pins::BTN_B;
  for (State& s : states) {
    pinMode(s.pin, INPUT_PULLUP);
    s.stable = false;
    s.lastRaw = false;
    s.lastChangeMs = millis();
  }
  queueHead = 0;
  queueCount = 0;
}

void sample() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < 2; i++) {
    const Event event = step(states[i], now);
    if (event != Event::kNone) {
      enqueue(i == 0 ? Button::kA : Button::kB, event);
    }
  }
}

Reading poll() {
  sample();
  if (queueCount == 0) return {Button::kA, Event::kNone};

  const Reading reading = queue[queueHead];
  queueHead = (uint8_t)((queueHead + 1) % kQueueSize);
  queueCount--;
  return reading;
}

bool isHeld(Button b) { return stateOf(b).stable; }

bool isDownRaw(Button b) { return digitalRead(stateOf(b).pin) == LOW; }

}  // namespace input
