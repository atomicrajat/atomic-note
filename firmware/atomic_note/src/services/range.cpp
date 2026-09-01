#include "range.h"

#include <VL53L0X.h>
#include <Wire.h>

#include "i2c_bus.h"

namespace services {
namespace range {
namespace {

VL53L0X sensor;
bool ready = false;

// The part reports 8190 mm (and sets a status bit) when nothing is in view.
// Treating that as a distance would show "8.19 m" on a device that can see
// two metres.
constexpr uint16_t kNoTarget = 8190;

}  // namespace

bool begin() {
  ready = false;
  if (!i2c::begin()) return false;
  if (!i2c::probe(0x29)) return false;

  sensor.setBus(&Wire);
  // Generous: init reads calibration out of the sensor's NVM, and a timeout
  // here shows up as a sensor that "sometimes works".
  sensor.setTimeout(500);
  if (!sensor.init()) {
    Serial.println("[range] VL53L0X init failed");
    return false;
  }

  // 33 ms per measurement — the library's default budget. Longer would be
  // more accurate and slower; on a panel that takes 500 ms to draw, the
  // measurement is never the thing you are waiting for.
  sensor.setMeasurementTimingBudget(33000);
  sensor.startContinuous();

  ready = true;
  Serial.println("[range] VL53L0X ready");
  return true;
}

bool available() { return ready; }

uint16_t readMm() {
  if (!ready) return 0;
  const uint16_t mm = sensor.readRangeContinuousMillimeters();
  if (sensor.timeoutOccurred()) return 0;
  if (mm == 0 || mm >= kNoTarget || mm > kMaxMm) return 0;
  return mm;
}

}  // namespace range
}  // namespace services
