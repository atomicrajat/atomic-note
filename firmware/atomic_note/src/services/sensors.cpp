#include "sensors.h"

#include "i2c_bus.h"

namespace services {
namespace sensors {
namespace {

// Parts worth looking for, with the addresses they use. Where an address is
// shared the name lists the candidates rather than guessing — a confident
// wrong label is worse than an honest ambiguous one.
const Entry kKnown[] = {
    {0x18, "ES8311 codec", Kind::kOther, true},
    {0x51, "PCF85063", Kind::kEnvironment, true},
    {0x70, "SHTC3", Kind::kEnvironment, true},

    // Names are kept to about a dozen characters: the Settings row that shows
    // them is 156px wide and shares it with a label. The KIND already says
    // what the part is for, so the name only has to identify it.
    {0x68, "MPU6050", Kind::kMotion, false},       // or MPU9250
    {0x69, "MPU6050b", Kind::kMotion, false},
    {0x6A, "LSM6DS", Kind::kMotion, false},
    {0x1E, "HMC5883", Kind::kMagnetic, false},
    {0x0D, "QMC5883", Kind::kMagnetic, false},
    {0x28, "BNO055", Kind::kMotion, false},
    {0x29, "VL53L0X", Kind::kDistance, false},  // BNO055 also lives here     // BNO055 also lives here
    {0x39, "APDS9960", Kind::kGesture, false},
    {0x53, "ADXL345", Kind::kMotion, false},
    {0x76, "BME280", Kind::kEnvironment, false},
    {0x77, "BME280b", Kind::kEnvironment, false},
    {0x48, "ADS1115", Kind::kOther, false},
    {0x3C, "SSD1306", Kind::kDisplay, false},
};
constexpr int kKnownCount = sizeof(kKnown) / sizeof(kKnown[0]);

Entry found[kKnownCount];
int foundCount = 0;

}  // namespace

void detect() {
  foundCount = 0;
  if (!i2c::begin()) return;

  for (int i = 0; i < kKnownCount; i++) {
    if (i2c::probe(kKnown[i].address)) {
      found[foundCount++] = kKnown[i];
    }
  }
}

int count() { return foundCount; }

const Entry* at(int index) {
  if (index < 0 || index >= foundCount) return nullptr;
  return &found[index];
}

bool have(Kind kind) {
  for (int i = 0; i < foundCount; i++) {
    if (found[i].kind == kind && !found[i].builtin) return true;
  }
  return false;
}

int addonCount() {
  int n = 0;
  for (int i = 0; i < foundCount; i++) {
    if (!found[i].builtin) n++;
  }
  return n;
}

const char* kindLabel(Kind kind) {
  switch (kind) {
    case Kind::kMotion: return "MOTION";
    case Kind::kDistance: return "DISTANCE";
    case Kind::kGesture: return "GESTURE";
    case Kind::kMagnetic: return "COMPASS";
    // "ENV", not "ENVIRONMENT": the Settings row is 156px wide and the
    // label shares it with the part name — "ENVIRONMENT BME280" measures
    // 172px and ran straight off the row.
    case Kind::kEnvironment: return "ENV";
    case Kind::kDisplay: return "DISPLAY";
    case Kind::kOther: return "OTHER";
    default: return "UNKNOWN";
  }
}

}  // namespace sensors
}  // namespace services
