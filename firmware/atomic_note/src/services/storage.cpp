#include "storage.h"

#include <SD_MMC.h>

#include "../board/pins.h"

namespace services {
namespace storage {
namespace {

bool mounted = false;

}  // namespace

bool begin() {
  if (mounted) return true;

  SD_MMC.setPins(pins::SD_CLK, pins::SD_CMD, pins::SD_D0);
  // 1-bit mode: only D0 is wired on this board. The `true` argument is what
  // selects it — 4-bit would fail here regardless of the card.
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("[sd] no card (device runs without one)");
    mounted = false;
    return false;
  }

  const uint8_t type = SD_MMC.cardType();
  if (type == CARD_NONE) {
    Serial.println("[sd] slot empty");
    SD_MMC.end();
    mounted = false;
    return false;
  }

  mounted = true;
  Serial.printf("[sd] mounted, %llu MB total, %llu MB used\n",
                totalBytes() / (1024ULL * 1024ULL),
                usedBytes() / (1024ULL * 1024ULL));
  return true;
}

bool available() { return mounted; }

uint64_t totalBytes() { return mounted ? SD_MMC.totalBytes() : 0; }
uint64_t usedBytes() { return mounted ? SD_MMC.usedBytes() : 0; }

bool ensureDir(const char* path) {
  if (!mounted) return false;
  if (SD_MMC.exists(path)) return true;
  return SD_MMC.mkdir(path);
}

bool exists(const char* path) {
  return mounted && SD_MMC.exists(path);
}

bool remove(const char* path) {
  return mounted && SD_MMC.remove(path);
}

bool readFile(const char* path, String& out, size_t maxLen) {
  out = "";
  if (!mounted) return false;

  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return false;

  out.reserve(min((size_t)f.size(), maxLen));
  while (f.available() && out.length() < maxLen) {
    out += (char)f.read();
  }
  f.close();
  return true;
}

bool writeFileAtomic(const char* path, const String& content) {
  if (!mounted) return false;

  // Temp file beside the target, then rename. A rename is the closest thing to
  // an atomic swap FAT offers; writing in place would leave the file truncated
  // if power went at the wrong moment.
  String tmp = String(path) + ".tmp";
  if (SD_MMC.exists(tmp.c_str())) SD_MMC.remove(tmp.c_str());

  File f = SD_MMC.open(tmp.c_str(), FILE_WRITE);
  if (!f) return false;

  const size_t written = f.print(content);
  f.flush();
  f.close();

  if (written != content.length()) {
    // Short write means the card is full or failing. Leave the old file alone.
    SD_MMC.remove(tmp.c_str());
    Serial.printf("[sd] short write on %s (%u of %u)\n", path,
                  (unsigned)written, (unsigned)content.length());
    return false;
  }

  if (SD_MMC.exists(path)) SD_MMC.remove(path);
  return SD_MMC.rename(tmp.c_str(), path);
}

}  // namespace storage
}  // namespace services
