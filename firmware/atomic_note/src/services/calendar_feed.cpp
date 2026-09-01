#include "calendar_feed.h"

#include <HTTPClient.h>

#include "network.h"
#include "settings.h"

namespace services {
namespace calendar_feed {

Agenda fetch() {
  Agenda out;

  if (!network::isJoined()) {
    out.error = "No network";
    return out;
  }
  String base = settings::companionUrl();
  base.trim();
  while (base.endsWith("/")) base.remove(base.length() - 1);
  if (base.length() == 0) {
    out.error = "No service URL";
    return out;
  }

  HTTPClient http;
  http.setTimeout(25000);
  if (!http.begin(base + "/calendar")) {
    out.error = "Bad URL";
    return out;
  }
  const int code = http.GET();
  if (code != 200) {
    out.error = code > 0 ? String("Service said ") + String(code)
                         : String("Cannot reach service");
    http.end();
    return out;
  }
  const String body = http.getString();
  http.end();

  // event <start> <end> <allday>\t<summary>\t<location>\t<description>
  //
  // Numbers first, space-separated; text fields after, tab-separated. Titles
  // and locations routinely contain spaces, so splitting the text on spaces
  // would truncate them at the first word.
  int start = 0;
  while (start < (int)body.length() && out.count < kMaxEvents) {
    int end = body.indexOf('\n', start);
    if (end < 0) end = body.length();
    const String line = body.substring(start, end);
    start = end + 1;
    if (line.length() == 0) continue;

    if (line.startsWith("none")) {
      out.configured = false;
      out.error = "No calendars set up";
      return out;
    }
    if (line.startsWith("empty")) break;
    if (!line.startsWith("event ")) continue;

    const int firstTab = line.indexOf('\t');
    if (firstTab < 0) continue;

    // Numeric header.
    const String header = line.substring(6, firstTab);
    const int spaceA = header.indexOf(' ');
    const int spaceB = header.indexOf(' ', spaceA + 1);
    if (spaceA < 0 || spaceB < 0) continue;

    Event& e = out.events[out.count];
    e.startUtc = (time_t)strtoull(header.substring(0, spaceA).c_str(), nullptr, 10);
    e.endUtc = (time_t)strtoull(header.substring(spaceA + 1, spaceB).c_str(),
                                nullptr, 10);
    e.allDay = header.substring(spaceB + 1) == "1";

    // Text fields.
    const int secondTab = line.indexOf('\t', firstTab + 1);
    const int thirdTab = secondTab < 0 ? -1 : line.indexOf('\t', secondTab + 1);

    const String summary = secondTab < 0 ? line.substring(firstTab + 1)
                                         : line.substring(firstTab + 1, secondTab);
    strncpy(e.summary, summary.c_str(), kMaxSummaryLen - 1);
    e.summary[kMaxSummaryLen - 1] = '\0';

    e.location[0] = '\0';
    e.description[0] = '\0';
    if (secondTab >= 0) {
      const String location = thirdTab < 0
                                  ? line.substring(secondTab + 1)
                                  : line.substring(secondTab + 1, thirdTab);
      strncpy(e.location, location.c_str(), kMaxLocationLen - 1);
      e.location[kMaxLocationLen - 1] = '\0';
    }
    if (thirdTab >= 0) {
      const String description = line.substring(thirdTab + 1);
      strncpy(e.description, description.c_str(), kMaxDescriptionLen - 1);
      e.description[kMaxDescriptionLen - 1] = '\0';
    }
    out.count++;
  }

  out.valid = true;
  return out;
}

}  // namespace calendar_feed
}  // namespace services
