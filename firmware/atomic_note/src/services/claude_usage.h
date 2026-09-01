// Claude Code token usage, fetched from the companion.
//
// The source is the session transcripts Claude Code writes on the laptop, not
// Anthropic's Admin API. The Admin API would need an organisation admin key
// and is unavailable to individual accounts — and it reports API usage, which
// is not what someone working in Claude Code actually wants to see.
//
// A side benefit: the device holds no credential at all for this.
#pragma once

#include <Arduino.h>

namespace services {
namespace claude_usage {

constexpr int kMaxDays = 7;

struct Snapshot {
  bool valid = false;
  int dayCount = 0;
  char dayLabel[kMaxDays][4] = {};
  uint32_t dayTokens[kMaxDays] = {};
  uint32_t dayCents[kMaxDays] = {};
  uint64_t totalTokens = 0;
  uint32_t totalCents = 0;
  uint32_t turns = 0;
  String topModel;
  String error;
};

// The current rate-limit windows.
//
// The ceiling is INFERRED from your own history — the busiest window you have
// ever had. Neither Anthropic's subscription thresholds nor the live figures
// Claude Code displays are readable from disk, so this is the honest best
// available, and the UI labels it as an estimate. It only becomes a true limit
// once you have actually hit one; before that the percentage reads low.
struct Limits {
  bool valid = false;
  uint64_t blockUsed = 0;
  uint64_t blockLimit = 0;
  int blockPercent = 0;
  uint32_t blockResetsInSec = 0;
  uint64_t weekUsed = 0;
  uint64_t weekLimit = 0;
  int weekPercent = 0;
  uint32_t weekResetsInSec = 0;
  int blocksObserved = 0;
  bool calibrated = false;
  String error;
};

// Blocking HTTP GET. Needs a joined network and a configured companion.
Snapshot fetch();
Limits fetchLimits();

}  // namespace claude_usage
}  // namespace services
