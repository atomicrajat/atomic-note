#include "battery_log.h"

#include <SD_MMC.h>
#include <time.h>

#include "../board/power.h"
#include "settings.h"
#include "storage.h"

namespace services {
namespace battery_log {
namespace {

constexpr const char* kPath = "/atomic/battery.csv";

}  // namespace

bool running() { return settings::batteryTest(); }

void start() {
  settings::setBatteryTest(true);

  // Start a fresh file. Appending to an old run would produce a curve with a
  // recharge hidden in the middle of it.
  if (storage::available()) {
    if (SD_MMC.exists(kPath)) SD_MMC.remove(kPath);
    storage::ensureDir("/atomic");
    File f = SD_MMC.open(kPath, FILE_WRITE);
    if (f) {
      f.println("unix,volts,percent");
      f.close();
    }
  }
}

void stop() { settings::setBatteryTest(false); }

bool sample() {
  if (!storage::available()) return false;

  const float v = power::batteryVolts();
  const int pct = power::batteryPercent();

  // Append, and flush by closing. A test that loses its readings to a dead
  // battery at the end has measured nothing.
  File f = SD_MMC.open(kPath, FILE_APPEND);
  if (!f) return false;
  f.printf("%lu,%.3f,%d\n", (unsigned long)time(nullptr), v, pct);
  f.close();
  return true;
}

uint32_t secondsUntilNext() { return running() ? kIntervalSec : 0; }

}  // namespace battery_log
}  // namespace services
