#include "i2c_bus.h"

#include <Arduino.h>
#include <Wire.h>

#include "../board/pins.h"

namespace services {
namespace i2c {
namespace {

constexpr uint32_t kClockHz = 100000;  // both devices are happy at 100 kHz
bool ready = false;

}  // namespace

bool begin() {
  if (ready) return true;
  ready = Wire.begin(pins::I2C_SDA, pins::I2C_SCL, kClockHz);
  if (!ready) {
    Serial.println("[i2c] bus init failed");
    return false;
  }
  Wire.setTimeOut(50);
  return true;
}

bool probe(uint8_t address) {
  if (!ready) return false;
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool write(uint8_t address, const uint8_t* data, size_t len) {
  if (!ready) return false;
  Wire.beginTransmission(address);
  if (len > 0 && Wire.write(data, len) != len) return false;
  return Wire.endTransmission() == 0;
}

bool read(uint8_t address, uint8_t* out, size_t len) {
  if (!ready) return false;
  const size_t got = Wire.requestFrom(address, (uint8_t)len);
  if (got != len) return false;
  for (size_t i = 0; i < len; i++) out[i] = (uint8_t)Wire.read();
  return true;
}

bool readRegister(uint8_t address, uint8_t reg, uint8_t* out, size_t len) {
  if (!ready) return false;
  Wire.beginTransmission(address);
  Wire.write(reg);
  // Repeated start — do not release the bus between write and read.
  if (Wire.endTransmission(false) != 0) return false;
  return read(address, out, len);
}

bool writeRegister(uint8_t address, uint8_t reg, const uint8_t* data,
                   size_t len) {
  if (!ready) return false;
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (len > 0 && Wire.write(data, len) != len) return false;
  return Wire.endTransmission() == 0;
}

}  // namespace i2c
}  // namespace services
