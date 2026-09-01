// VL53L0X time-of-flight distance sensor, when one is attached.
//
// Unlike the other drivers here, this one wraps a library rather than being
// written from the datasheet. That is a deliberate exception: bringing a
// VL53L0X up means reading a SPAD count and aperture out of NVM, a reference
// calibration, and about eighty undocumented register writes that ST publishes
// only as code. A hand-rolled subset of that will happily return numbers —
// just not correct ones, and a measuring tool that is confidently wrong is
// worse than one that does not exist.
//
// Pololu's VL53L0X library (MIT) is the same shape as the ES8311 and PCF85063
// drivers written here — a thin layer over registers — and it shares the
// project's existing Wire bus, so nothing about the I2C setup changes.
#pragma once

#include <Arduino.h>

namespace services {
namespace range {

// Reliable ceiling for this part in its default mode. Beyond it the reading is
// reported as out of range rather than as a number that happens to be large.
constexpr uint16_t kMaxMm = 2000;

// Find and initialise the sensor. False when nothing answered, which is a
// normal state — the Measure app is struck through rather than broken.
bool begin();

bool available();

// Latest distance in millimetres, or 0 when out of range or unreadable. The
// VL53L0X reports 8190 with nothing in view, which is a signal rather than a
// measurement and is filtered out here.
uint16_t readMm();

}  // namespace range
}  // namespace services
