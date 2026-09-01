#include "motion.h"

#include <math.h>

#include "i2c_bus.h"

namespace services {
namespace motion {
namespace {

// The part answers at 0x68 with AD0 low and 0x69 with it high. Breakout boards
// differ on which they tie it to, so both are tried.
constexpr uint8_t kAddresses[] = {0x68, 0x69};

constexpr uint8_t kRegWhoAmI = 0x75;
constexpr uint8_t kRegPwrMgmt1 = 0x6B;
constexpr uint8_t kRegAccelConfig = 0x1C;
constexpr uint8_t kRegAccelXOut = 0x3B;

// WHO_AM_I returns 0x68 on a genuine MPU6050 regardless of which address it
// is answering on. Clones return 0x70, 0x72 or 0x98 and are otherwise
// register-compatible, so they are accepted too — rejecting them would mean
// most of the boards actually sold.
constexpr uint8_t kKnownIds[] = {0x68, 0x70, 0x71, 0x72, 0x73, 0x98};

// +/-8 g rather than the default +/-2 g.
//
// A deliberate shake peaks well above 2 g, and at the default range every
// sample in the interesting part of the motion clips at the rail — the sensor
// reports the same number for a firm shake and a violent one, which is exactly
// the distinction a shake detector needs.
constexpr uint8_t kAccelConfig8G = 0x10;
constexpr float kLsbPerG = 4096.0f;  // at +/-8 g

uint8_t found = 0;

}  // namespace

bool begin() {
  found = 0;
  if (!i2c::begin()) return false;

  for (uint8_t address : kAddresses) {
    if (!i2c::probe(address)) continue;

    uint8_t id = 0;
    if (!i2c::readRegister(address, kRegWhoAmI, &id, 1)) continue;

    bool known = false;
    for (uint8_t candidate : kKnownIds) {
      if (id == candidate) known = true;
    }
    if (!known) {
      Serial.printf("[motion] 0x%02X answered with id 0x%02X, ignoring\n",
                    address, id);
      continue;
    }

    // Out of sleep. The part boots with the sleep bit set and returns zeros
    // until it is cleared, which reads as "attached but perfectly still".
    const uint8_t wake = 0x00;
    if (!i2c::writeRegister(address, kRegPwrMgmt1, &wake, 1)) continue;
    delay(10);

    if (!i2c::writeRegister(address, kRegAccelConfig, &kAccelConfig8G, 1)) {
      continue;
    }

    found = address;
    Serial.printf("[motion] MPU6050 at 0x%02X (id 0x%02X)\n", address, id);
    return true;
  }
  return false;
}

bool available() { return found != 0; }

uint8_t address() { return found; }

bool read(float& x, float& y, float& z) {
  if (!found) return false;

  uint8_t raw[6];
  if (!i2c::readRegister(found, kRegAccelXOut, raw, sizeof(raw))) return false;

  // Big-endian signed 16-bit per axis, high byte first.
  const int16_t rx = (int16_t)((raw[0] << 8) | raw[1]);
  const int16_t ry = (int16_t)((raw[2] << 8) | raw[3]);
  const int16_t rz = (int16_t)((raw[4] << 8) | raw[5]);

  x = rx / kLsbPerG;
  y = ry / kLsbPerG;
  z = rz / kLsbPerG;
  return true;
}

float magnitude() {
  float x, y, z;
  if (!read(x, y, z)) return 0.0f;
  return sqrtf(x * x + y * y + z * z);
}

}  // namespace motion
}  // namespace services
