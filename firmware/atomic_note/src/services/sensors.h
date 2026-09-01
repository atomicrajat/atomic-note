// Which optional sensors are actually plugged in.
//
// The board exposes the I2C bus that the RTC and the environment sensor
// already share, so anything else added hangs off the same two pins. That
// makes detection cheap and honest: probe the addresses of the parts we know
// about and report what answered. No configuration, no "did you attach it"
// dialog — the hardware is asked directly.
//
// Two things this deliberately does NOT do:
//
//   It does not identify a part beyond its address. Several sensors share an
//   address (a VL53L0X and a BNO055 both live at 0x29), so a hit reports the
//   candidates rather than pretending to know which. Reading a WHO_AM_I
//   register would settle it and belongs with the driver for that part, not
//   here.
//
//   It does not enable anything. Detection says what is present; whether a
//   feature uses it is a separate decision, made in Settings.
#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace services {
namespace sensors {

// What a detected part is good for, which is what decides the apps it unlocks.
enum class Kind : uint8_t {
  kUnknown,
  kMotion,     // accelerometer / gyro / IMU
  kDistance,   // time-of-flight, ultrasonic
  kGesture,    // proximity and gesture
  kMagnetic,   // compass
  kEnvironment,
  kDisplay,
  kOther,
};

struct Entry {
  uint8_t address;
  const char* name;   // candidates, slash-separated where an address is shared
  Kind kind;
  bool builtin;       // already on the board, not something you added
};

// Probe the bus. Cheap — one address write per known part — but it does talk
// to hardware, so call it on entering a screen rather than while drawing.
void detect();

// Results of the last detect().
int count();
const Entry* at(int index);

// How many of the detected parts are add-ons rather than built in.
int addonCount();

// Is a part of this kind attached? What the Apps menu asks before offering
// a feature that needs one.
bool have(Kind kind);

const char* kindLabel(Kind kind);

}  // namespace sensors
}  // namespace services
