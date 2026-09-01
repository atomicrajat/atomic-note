#include "tasks.h"

#include "storage.h"

namespace services {
namespace tasks {
namespace {

constexpr const char* kDir = "/atomic";
constexpr const char* kPath = "/atomic/tasks.tsv";

Task list[kMaxTasks];
int listCount = 0;
uint32_t nextId = 1;

// One task per line: done<TAB>id<TAB>text
//
// Tab-separated with the free text LAST, so a task can contain any character
// except a tab or a newline. The board's reference firmware used comma-
// separated fields with text in the middle, which silently corrupted any entry
// containing a comma — worth not repeating.
void parseLine(const String& line) {
  if (listCount >= kMaxTasks) return;
  if (line.length() < 4) return;

  const int firstTab = line.indexOf('\t');
  if (firstTab < 0) return;
  const int secondTab = line.indexOf('\t', firstTab + 1);
  if (secondTab < 0) return;

  Task& t = list[listCount];
  t.done = (line.charAt(0) == '1');
  t.id = (uint32_t)line.substring(firstTab + 1, secondTab).toInt();

  String text = line.substring(secondTab + 1);
  text.trim();
  if (text.length() == 0) return;

  strncpy(t.text, text.c_str(), kMaxTextLen - 1);
  t.text[kMaxTextLen - 1] = '\0';

  if (t.id >= nextId) nextId = t.id + 1;
  listCount++;
}

}  // namespace

void begin() {
  listCount = 0;
  nextId = 1;

  if (!storage::available()) {
    Serial.println("[tasks] no card — list is session-only");
    return;
  }
  storage::ensureDir(kDir);

  String blob;
  if (!storage::readFile(kPath, blob)) {
    Serial.println("[tasks] no task file yet");
    return;
  }

  int start = 0;
  while (start < (int)blob.length() && listCount < kMaxTasks) {
    int end = blob.indexOf('\n', start);
    if (end < 0) end = blob.length();
    parseLine(blob.substring(start, end));
    start = end + 1;
  }
  Serial.printf("[tasks] loaded %d\n", listCount);
}

int count() { return listCount; }

const Task* at(int index) {
  if (index < 0 || index >= listCount) return nullptr;
  return &list[index];
}

bool add(const char* text) {
  if (!text || !*text) return false;
  if (listCount >= kMaxTasks) return false;

  Task& t = list[listCount];
  t.id = nextId++;
  t.done = false;
  strncpy(t.text, text, kMaxTextLen - 1);
  t.text[kMaxTextLen - 1] = '\0';

  // Tabs and newlines would break the record format on the way back in.
  for (char* p = t.text; *p; p++) {
    if (*p == '\t' || *p == '\n' || *p == '\r') *p = ' ';
  }

  listCount++;
  save();
  return true;
}

void toggleDone(int index) {
  if (index < 0 || index >= listCount) return;
  list[index].done = !list[index].done;
  save();
}

void removeAt(int index) {
  if (index < 0 || index >= listCount) return;
  for (int i = index; i < listCount - 1; i++) list[i] = list[i + 1];
  listCount--;
  save();
}

void clearCompleted() {
  int write = 0;
  for (int read = 0; read < listCount; read++) {
    if (list[read].done) continue;
    if (write != read) list[write] = list[read];
    write++;
  }
  listCount = write;
  save();
}

int remainingCount() {
  int n = 0;
  for (int i = 0; i < listCount; i++) {
    if (!list[i].done) n++;
  }
  return n;
}

bool save() {
  if (!storage::available()) return false;

  String blob;
  blob.reserve((size_t)listCount * (kMaxTextLen + 16));
  for (int i = 0; i < listCount; i++) {
    blob += list[i].done ? '1' : '0';
    blob += '\t';
    blob += String(list[i].id);
    blob += '\t';
    blob += list[i].text;
    blob += '\n';
  }
  return storage::writeFileAtomic(kPath, blob);
}

}  // namespace tasks
}  // namespace services
