#include "notes.h"

#include <SD_MMC.h>

#include "storage.h"

namespace services {
namespace notes {
namespace {

constexpr const char* kDir = "/atomic/notes";
constexpr const char* kIndexPath = "/atomic/notes/index.tsv";

Note list[kMaxNotes];
int listCount = 0;
int highestNumber = 0;

// number \t created \t durationMs \t hasText \t tag
//
// Tab-separated with the free text last, for the same reason as the task file:
// the reference firmware's comma-separated format silently corrupted any tag
// containing a comma.
void parseLine(const String& line) {
  if (listCount >= kMaxNotes) return;

  int field = 0;
  int start = 0;
  String parts[4];
  while (field < 4) {
    const int tab = line.indexOf('\t', start);
    if (tab < 0) break;
    parts[field++] = line.substring(start, tab);
    start = tab + 1;
  }
  // Files written before transcripts existed have one fewer field. Accept
  // them rather than discarding a user's recordings on an upgrade.
  if (field < 3) return;
  const bool legacy = (field == 3);

  String tag = line.substring(start);
  tag.trim();

  Note& n = list[listCount];
  n.number = parts[0].toInt();
  n.createdUtc = (time_t)parts[1].toInt();
  n.durationMs = (uint32_t)parts[2].toInt();
  n.hasText = legacy ? false : (parts[3] == "1");
  strncpy(n.tag, tag.length() ? tag.c_str() : "Note", kMaxTagLen - 1);
  n.tag[kMaxTagLen - 1] = '\0';

  if (n.number > highestNumber) highestNumber = n.number;
  listCount++;
}

bool matches(const Note& n, const char* tag) {
  return tag == nullptr || strcmp(n.tag, tag) == 0;
}

}  // namespace

void begin() {
  listCount = 0;
  highestNumber = 0;

  if (!storage::available()) {
    Serial.println("[notes] no card — recording unavailable");
    return;
  }
  storage::ensureDir("/atomic");
  storage::ensureDir(kDir);

  String blob;
  if (!storage::readFile(kIndexPath, blob, 16384)) {
    Serial.println("[notes] no index yet");
    return;
  }

  int start = 0;
  while (start < (int)blob.length() && listCount < kMaxNotes) {
    int end = blob.indexOf('\n', start);
    if (end < 0) end = blob.length();
    parseLine(blob.substring(start, end));
    start = end + 1;
  }
  Serial.printf("[notes] loaded %d\n", listCount);
}

int count() { return listCount; }

const Note* at(int index) {
  if (index < 0 || index >= listCount) return nullptr;
  return &list[index];
}

int nextNumber() { return highestNumber + 1; }

String wavPath(int number) {
  char buf[48];
  snprintf(buf, sizeof(buf), "%s/note_%03d.wav", kDir, number);
  return String(buf);
}

String textPath(int number) {
  char buf[48];
  snprintf(buf, sizeof(buf), "%s/note_%03d.txt", kDir, number);
  return String(buf);
}

bool setTranscript(int index, const String& text) {
  if (index < 0 || index >= listCount) return false;
  if (!storage::available()) return false;

  if (!storage::writeFileAtomic(textPath(list[index].number).c_str(), text)) {
    return false;
  }
  list[index].hasText = true;
  save();
  return true;
}

String transcript(int index) {
  if (index < 0 || index >= listCount) return String("");
  if (!list[index].hasText) return String("");
  String out;
  storage::readFile(textPath(list[index].number).c_str(), out, 4096);
  return out;
}

int countWithoutText() {
  int n = 0;
  for (int i = 0; i < listCount; i++) {
    if (!list[i].hasText) n++;
  }
  return n;
}

int indexOfUntranscribed(int position) {
  int seen = 0;
  for (int i = 0; i < listCount; i++) {
    if (list[i].hasText) continue;
    if (seen == position) return i;
    seen++;
  }
  return -1;
}

bool add(int number, const char* tag, uint32_t durationMs) {
  if (listCount >= kMaxNotes) return false;

  Note& n = list[listCount];
  n.number = number;
  n.createdUtc = time(nullptr);
  n.durationMs = durationMs;
  n.hasText = false;
  strncpy(n.tag, (tag && *tag) ? tag : "Note", kMaxTagLen - 1);
  n.tag[kMaxTagLen - 1] = '\0';
  for (char* p = n.tag; *p; p++) {
    if (*p == '\t' || *p == '\n' || *p == '\r') *p = ' ';
  }

  if (number > highestNumber) highestNumber = number;
  listCount++;
  save();
  return true;
}

bool setTag(int index, const char* tag) {
  if (index < 0 || index >= listCount) return false;
  strncpy(list[index].tag, (tag && *tag) ? tag : "Note", kMaxTagLen - 1);
  list[index].tag[kMaxTagLen - 1] = '\0';
  save();
  return true;
}

bool removeAt(int index) {
  if (index < 0 || index >= listCount) return false;

  // Delete the audio too — an orphaned WAV is invisible but still fills the
  // card.
  const String path = wavPath(list[index].number);
  if (SD_MMC.exists(path.c_str())) SD_MMC.remove(path.c_str());
  const String text = textPath(list[index].number);
  if (SD_MMC.exists(text.c_str())) SD_MMC.remove(text.c_str());

  for (int i = index; i < listCount - 1; i++) list[i] = list[i + 1];
  listCount--;
  save();
  return true;
}

int countWithTag(const char* tag) {
  if (tag == nullptr) return listCount;
  int n = 0;
  for (int i = 0; i < listCount; i++) {
    if (matches(list[i], tag)) n++;
  }
  return n;
}

int indexOfFiltered(const char* tag, int visiblePosition) {
  if (visiblePosition < 0) return -1;

  // Walk backwards: the index is append-ordered, and the list is presented
  // newest first.
  int seen = 0;
  for (int i = listCount - 1; i >= 0; i--) {
    if (!matches(list[i], tag)) continue;
    if (seen == visiblePosition) return i;
    seen++;
  }
  return -1;
}

bool save() {
  if (!storage::available()) return false;

  String blob;
  blob.reserve((size_t)listCount * 48);
  for (int i = 0; i < listCount; i++) {
    blob += String(list[i].number);
    blob += '\t';
    blob += String((uint32_t)list[i].createdUtc);
    blob += '\t';
    blob += String(list[i].durationMs);
    blob += '\t';
    blob += list[i].hasText ? '1' : '0';
    blob += '\t';
    blob += list[i].tag;
    blob += '\n';
  }
  return storage::writeFileAtomic(kIndexPath, blob);
}

}  // namespace notes
}  // namespace services
