// Upcoming calendar events, fetched from the companion.
//
// The companion holds the feed URLs, not the device. A Google secret-iCal URL
// grants read access to a whole calendar to anyone who has it, and the
// device's web app is served unauthenticated on the LAN — so that secret stays
// on the laptop and only the resulting events cross the network.
#pragma once

#include <Arduino.h>
#include <time.h>

namespace services {
namespace calendar_feed {

constexpr int kMaxEvents = 10;
constexpr int kMaxSummaryLen = 80;
constexpr int kMaxLocationLen = 48;
constexpr int kMaxDescriptionLen = 120;

struct Event {
  time_t startUtc;
  time_t endUtc;  // 0 when the feed gave no end
  bool allDay;
  char summary[kMaxSummaryLen];
  char location[kMaxLocationLen];
  char description[kMaxDescriptionLen];
};

struct Agenda {
  bool valid = false;
  bool configured = true;  // false when no feeds are set up at all
  int count = 0;
  Event events[kMaxEvents];
  String error;
};

Agenda fetch();

}  // namespace calendar_feed
}  // namespace services
