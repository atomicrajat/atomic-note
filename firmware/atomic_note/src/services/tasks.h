// The task list.
//
// Held in RAM and mirrored to the card. The card is optional: with no card the
// list still works for the session, it just does not survive a reboot — which
// is better than a task screen that refuses to open.
//
// Tasks are CREATED from the web app in phase 5, because two buttons cannot
// enter text. What the device does well is triage: read them, tick them off,
// clear them. That is the half worth having in your hand.
#pragma once

#include <Arduino.h>

namespace services {
namespace tasks {

// Bounded so the whole list is a fixed allocation. 40 tasks is far past the
// point where a 200px screen is a sensible way to read them.
constexpr int kMaxTasks = 40;
constexpr int kMaxTextLen = 48;

struct Task {
  uint32_t id;
  char text[kMaxTextLen];
  bool done;
};

// Loads from the card if there is one. Safe to call with no card.
void begin();

int count();
const Task* at(int index);

// Returns false if the list is full or the text is empty.
bool add(const char* text);

void toggleDone(int index);
void removeAt(int index);
void clearCompleted();

int remainingCount();

// Write the list to the card. Called automatically after every mutation; the
// list is small enough that batching would be a false economy.
bool save();

}  // namespace tasks
}  // namespace services
