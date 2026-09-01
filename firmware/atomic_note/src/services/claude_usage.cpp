#include "claude_usage.h"

#include <HTTPClient.h>

#include "network.h"
#include "settings.h"

namespace services {
namespace claude_usage {
namespace {

// Reading a few hundred session files takes the companion a moment.
constexpr uint16_t kTimeoutMs = 20000;

String fieldAt(const String& line, int index) {
  int start = 0;
  for (int i = 0; i < index; i++) {
    const int space = line.indexOf(' ', start);
    if (space < 0) return String("");
    start = space + 1;
  }
  const int end = line.indexOf(' ', start);
  return end < 0 ? line.substring(start) : line.substring(start, end);
}

}  // namespace

Snapshot fetch() {
  Snapshot out;

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
  http.setTimeout(kTimeoutMs);
  if (!http.begin(base + "/claude-usage")) {
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

  // Line-oriented on purpose: no JSON parser in the firmware.
  int start = 0;
  while (start < (int)body.length()) {
    int end = body.indexOf('\n', start);
    if (end < 0) end = body.length();
    const String line = body.substring(start, end);
    start = end + 1;
    if (line.length() == 0) continue;

    if (line.startsWith("day ") && out.dayCount < kMaxDays) {
      const String label = fieldAt(line, 1);
      strncpy(out.dayLabel[out.dayCount], label.c_str(), 3);
      out.dayLabel[out.dayCount][3] = '\0';
      out.dayTokens[out.dayCount] = (uint32_t)fieldAt(line, 2).toInt();
      out.dayCents[out.dayCount] = (uint32_t)fieldAt(line, 3).toInt();
      out.dayCount++;
    } else if (line.startsWith("total ")) {
      // Weekly totals run into the billions, past what toInt() holds.
      out.totalTokens = (uint64_t)strtoull(fieldAt(line, 1).c_str(), nullptr, 10);
      out.totalCents = (uint32_t)fieldAt(line, 2).toInt();
    } else if (line.startsWith("turns ")) {
      out.turns = (uint32_t)fieldAt(line, 1).toInt();
    } else if (line.startsWith("top ")) {
      out.topModel = fieldAt(line, 1);
    }
  }

  out.valid = out.dayCount > 0;
  if (!out.valid) out.error = "No usage data";
  return out;
}

Limits fetchLimits() {
  Limits out;

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
  http.setTimeout(kTimeoutMs);
  if (!http.begin(base + "/claude-limits")) {
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

  int start = 0;
  while (start < (int)body.length()) {
    int end = body.indexOf('\n', start);
    if (end < 0) end = body.length();
    const String line = body.substring(start, end);
    start = end + 1;
    if (line.length() == 0) continue;

    if (line.startsWith("block ")) {
      out.blockUsed = strtoull(fieldAt(line, 1).c_str(), nullptr, 10);
      out.blockLimit = strtoull(fieldAt(line, 2).c_str(), nullptr, 10);
      out.blockPercent = fieldAt(line, 3).toInt();
      out.blockResetsInSec = (uint32_t)fieldAt(line, 4).toInt();
    } else if (line.startsWith("week ")) {
      out.weekUsed = strtoull(fieldAt(line, 1).c_str(), nullptr, 10);
      out.weekLimit = strtoull(fieldAt(line, 2).c_str(), nullptr, 10);
      out.weekPercent = fieldAt(line, 3).toInt();
      out.weekResetsInSec = (uint32_t)fieldAt(line, 4).toInt();
    } else if (line.startsWith("basis ")) {
      out.blocksObserved = fieldAt(line, 1).toInt();
    } else if (line.startsWith("source ")) {
      out.calibrated = (fieldAt(line, 1) == "calibrated");
    }
  }

  out.valid = out.blocksObserved > 0;
  if (!out.valid) out.error = "No usage history";
  return out;
}

}  // namespace claude_usage
}  // namespace services
