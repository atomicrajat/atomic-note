// Shared I2C bus.
//
// Two devices live here — the PCF85063 RTC at 0x51 and the SHTC3 environment
// sensor at 0x70 — so the bus is brought up once and both drivers borrow it.
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace services {
namespace i2c {

bool begin();

// True if a device acknowledges its address.
bool probe(uint8_t address);

// Write raw bytes. Returns false on NACK or bus error.
bool write(uint8_t address, const uint8_t* data, size_t len);

// Write a register address, then read `len` bytes back (repeated start).
bool readRegister(uint8_t address, uint8_t reg, uint8_t* out, size_t len);

// Write to a register.
bool writeRegister(uint8_t address, uint8_t reg, const uint8_t* data,
                   size_t len);

// Read without first writing a register pointer — some devices (SHTC3) stream
// their result directly after a command.
bool read(uint8_t address, uint8_t* out, size_t len);

}  // namespace i2c
}  // namespace services
