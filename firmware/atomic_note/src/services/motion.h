// MPU6050 accelerometer, when one is attached.
//
// An optional part on the spare I2C lines, so everything here reports failure
// rather than assuming: `begin()` returning false is a normal state, and the
// features that need motion are hidden rather than broken when it does.
//
// Only the accelerometer is read. The gyro is powered and available, but
// nothing yet asks "how fast is it turning" — and reading six bytes instead of
// twelve on every poll is worth having for a part that is polled at 30 Hz.
#pragma once

#include <Arduino.h>

namespace services {
namespace motion {

// Probe both addresses the part can use, check WHO_AM_I, wake it and set the
// range. False when nothing answered.
bool begin();

// True once begin() has found a device. Cheap; safe to call in a draw path.
bool available();

// Which address answered, for the Settings screen. 0 when none did.
uint8_t address();

// Acceleration in g, one sample. False if the read failed.
bool read(float& x, float& y, float& z);

// Magnitude of the acceleration vector, in g. About 1.0 sitting still, since
// gravity never stops. 0 on a failed read.
float magnitude();

}  // namespace motion
}  // namespace services
