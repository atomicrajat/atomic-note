#include "reminders.h"

#include "rtc.h"
#include "settings.h"
#include "storage.h"

namespace services {
namespace reminders {
namespace {

constexpr const char* kDir = "/atomic";
constexpr const char* kPath = "/atomic/reminders.tsv";

// A reminder counts as due within this window either side of its time. Wide
// enough to absorb a late wake, narrow enough not to fire the wrong minute.
constexpr int kDueWindowSec = 90;

// Once fired, ignore the same reminder for this long — otherwise every wake
// inside the due window would raise it again.
constexpr int kSuppressSec = 120;

Reminder list[kMaxReminders];
int listCount = 0;
uint32_t nextId = 1;

int offsetSec() { return settings::utcOffsetMinutes() * 60; }

// Seconds past local midnight, and the local weekday, for a UTC instant.
void localParts(time_t utc, int* secondsOfDay, int* weekday) {
  const time_t local = utc + offsetSec();
  struct tm t;
  gmtime_r(&local, &t);  // pre-offset epoch: fields are already local
  *secondsOfDay = t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
  *weekday = t.tm_wday;
}

// The next UTC instant at which this reminder should fire, strictly after
// `afterUtc`. Returns 0 when it can never fire.
time_t nextOccurrence(const Reminder& r, time_t afterUtc) {
  if (!r.enabled) return 0;

  int secondsOfDay = 0;
  int weekday = 0;
  localParts(afterUtc, &secondsOfDay, &weekday);

  const time_t localNow = afterUtc + offsetSec();
  const time_t localMidnight = localNow - secondsOfDay;
  const int target = r.hour * 3600 + r.minute * 60;

  // Look at today and the next seven days; one of them always matches unless
  // the mask is empty.
  for (int ahead = 0; ahead <= 7; ahead++) {
    const time_t candidateLocal = localMidnight + (time_t)ahead * 86400 + target;
    if (candidateLocal <= localNow) continue;

    if (r.days != kOnce) {
      const int candidateWeekday = (weekday + ahead) % 7;
      if ((r.days & (1 << candidateWeekday)) == 0) continue;
    }
    return candidateLocal - offsetSec();
  }
  return 0;
}

void parseLine(const String& line) {
  if (listCount >= kMaxReminders) return;

  // enabled \t id \t hour \t minute \t days \t lastFired \t text
  int field = 0;
  int start = 0;
  String parts[6];
  while (field < 6) {
    const int tab = line.indexOf('\t', start);
    if (tab < 0) return;  // malformed: not enough fields
    parts[field++] = line.substring(start, tab);
    start = tab + 1;
  }
  String text = line.substring(start);
  text.trim();
  if (text.length() == 0) return;

  Reminder& r = list[listCount];
  r.enabled = (parts[0] == "1");
  r.id = (uint32_t)parts[1].toInt();
  r.hour = (uint8_t)constrain(parts[2].toInt(), 0, 23);
  r.minute = (uint8_t)constrain(parts[3].toInt(), 0, 59);
  r.days = (uint8_t)parts[4].toInt();
  r.lastFiredUtc = (time_t)parts[5].toInt();
  strncpy(r.text, text.c_str(), kMaxTextLen - 1);
  r.text[kMaxTextLen - 1] = '\0';

  if (r.id >= nextId) nextId = r.id + 1;
  listCount++;
}

}  // namespace

void begin() {
  listCount = 0;
  nextId = 1;

  if (!storage::available()) {
    Serial.println("[rem] no card — reminders are session-only");
    return;
  }
  storage::ensureDir(kDir);

  String blob;
  if (!storage::readFile(kPath, blob)) return;

  int start = 0;
  while (start < (int)blob.length() && listCount < kMaxReminders) {
    int end = blob.indexOf('\n', start);
    if (end < 0) end = blob.length();
    parseLine(blob.substring(start, end));
    start = end + 1;
  }
  Serial.printf("[rem] loaded %d\n", listCount);
}

int count() { return listCount; }

const Reminder* at(int index) {
  if (index < 0 || index >= listCount) return nullptr;
  return &list[index];
}

bool add(const char* text, int hour, int minute, uint8_t days) {
  if (!text || !*text) return false;
  if (listCount >= kMaxReminders) return false;
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return false;

  Reminder& r = list[listCount];
  r.id = nextId++;
  r.hour = (uint8_t)hour;
  r.minute = (uint8_t)minute;
  r.days = days;
  r.enabled = true;
  r.lastFiredUtc = 0;
  strncpy(r.text, text, kMaxTextLen - 1);
  r.text[kMaxTextLen - 1] = '\0';
  for (char* p = r.text; *p; p++) {
    if (*p == '\t' || *p == '\n' || *p == '\r') *p = ' ';
  }

  listCount++;
  save();
  return true;
}

bool removeAt(int index) {
  if (index < 0 || index >= listCount) return false;
  for (int i = index; i < listCount - 1; i++) list[i] = list[i + 1];
  listCount--;
  save();
  return true;
}

void setEnabled(int index, bool enabled) {
  if (index < 0 || index >= listCount) return;
  list[index].enabled = enabled;
  save();
}

time_t nextDueUtc() {
  if (!rtc::timeIsValid()) return 0;  // cannot schedule against an unset clock

  const time_t now = time(nullptr);
  time_t soonest = 0;
  for (int i = 0; i < listCount; i++) {
    const time_t at = nextOccurrence(list[i], now);
    if (at == 0) continue;
    if (soonest == 0 || at < soonest) soonest = at;
  }
  return soonest;
}

int dueNow() {
  if (!rtc::timeIsValid()) return -1;
  const time_t now = time(nullptr);

  // Asked directly: is the local clock inside this reminder's window today?
  // Working forward from "the next occurrence" and stepping back a period does
  // not survive weekday masks — the previous occurrence may be six days ago.
  int secondsOfDay = 0;
  int weekday = 0;
  localParts(now, &secondsOfDay, &weekday);

  for (int i = 0; i < listCount; i++) {
    const Reminder& r = list[i];
    if (!r.enabled) continue;
    if (r.lastFiredUtc != 0 && now - r.lastFiredUtc < kSuppressSec) continue;
    if (r.days != kOnce && (r.days & (1 << weekday)) == 0) continue;

    const int target = r.hour * 3600 + r.minute * 60;
    const int delta = secondsOfDay - target;
    if (delta >= 0 && delta <= kDueWindowSec) return i;
  }
  return -1;
}

void markFired(int index) {
  if (index < 0 || index >= listCount) return;
  list[index].lastFiredUtc = time(nullptr);
  // A one-shot has done its job.
  if (list[index].days == kOnce) list[index].enabled = false;
  save();
}

uint32_t secondsUntilNext() {
  const time_t due = nextDueUtc();
  if (due == 0) return 0;
  const time_t now = time(nullptr);
  if (due <= now) return 1;
  return (uint32_t)(due - now);
}

bool save() {
  if (!storage::available()) return false;

  String blob;
  blob.reserve((size_t)listCount * (kMaxTextLen + 40));
  for (int i = 0; i < listCount; i++) {
    const Reminder& r = list[i];
    blob += r.enabled ? '1' : '0';
    blob += '\t';
    blob += String(r.id);
    blob += '\t';
    blob += String(r.hour);
    blob += '\t';
    blob += String(r.minute);
    blob += '\t';
    blob += String(r.days);
    blob += '\t';
    blob += String((uint32_t)r.lastFiredUtc);
    blob += '\t';
    blob += r.text;
    blob += '\n';
  }
  return storage::writeFileAtomic(kPath, blob);
}

}  // namespace reminders
}  // namespace services
