#include "environment.h"

#include <Arduino.h>

#include "../board/pins.h"
#include "i2c_bus.h"

namespace services {
namespace environment {
namespace {

// ── SHTC3 commands (16-bit, MSB first) ────────────────────────────────────
constexpr uint16_t CMD_WAKEUP = 0x3517;
constexpr uint16_t CMD_SLEEP = 0xB098;
// Normal power, temperature first, clock stretching disabled. We poll instead
// of stretching so a wedged sensor cannot hang the bus.
constexpr uint16_t CMD_MEASURE = 0x7866;
constexpr uint16_t CMD_READ_ID = 0xEFC8;

constexpr uint8_t kMeasureDelayMs = 13;  // datasheet max is 12.1 ms

bool present = false;

bool sendCommand(uint16_t cmd) {
  const uint8_t bytes[2] = {(uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF)};
  return i2c::write(pins::ADDR_SHTC3, bytes, sizeof(bytes));
}

// CRC-8, polynomial 0x31, initialised to 0xFF — as specified for this part.
uint8_t crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

void wake() {
  sendCommand(CMD_WAKEUP);
  delayMicroseconds(300);  // datasheet: 240 us to leave sleep
}

void sleep() { sendCommand(CMD_SLEEP); }

}  // namespace

bool begin() {
  present = false;
  if (!i2c::probe(pins::ADDR_SHTC3)) {
    // The sensor does not ACK while asleep, so a bare probe is inconclusive.
    // Wake it and try once more before declaring it missing.
    wake();
    if (!i2c::probe(pins::ADDR_SHTC3)) {
      Serial.println("[env] SHTC3 not responding");
      return false;
    }
  }

  wake();
  if (!sendCommand(CMD_READ_ID)) {
    sleep();
    return false;
  }
  uint8_t raw[3] = {};
  if (!i2c::read(pins::ADDR_SHTC3, raw, sizeof(raw))) {
    sleep();
    Serial.println("[env] SHTC3 ID read failed");
    return false;
  }
  sleep();

  if (crc8(raw, 2) != raw[2]) {
    Serial.println("[env] SHTC3 ID checksum mismatch");
    return false;
  }

  present = true;
  Serial.printf("[env] SHTC3 ready (id 0x%04X)\n",
                (unsigned)((raw[0] << 8) | raw[1]));
  return true;
}

Reading measure() {
  Reading result = {0.0f, 0.0f, false};
  if (!present) return result;

  wake();
  if (!sendCommand(CMD_MEASURE)) {
    sleep();
    return result;
  }
  delay(kMeasureDelayMs);

  uint8_t raw[6] = {};
  if (!i2c::read(pins::ADDR_SHTC3, raw, sizeof(raw))) {
    sleep();
    return result;
  }
  sleep();

  if (crc8(&raw[0], 2) != raw[2] || crc8(&raw[3], 2) != raw[5]) {
    Serial.println("[env] SHTC3 checksum mismatch");
    return result;
  }

  const uint16_t rawT = (uint16_t)((raw[0] << 8) | raw[1]);
  const uint16_t rawH = (uint16_t)((raw[3] << 8) | raw[4]);

  // Conversions straight from the datasheet.
  result.celsius = -45.0f + 175.0f * (float)rawT / 65535.0f;
  result.humidity = 100.0f * (float)rawH / 65535.0f;
  result.valid = true;
  return result;
}

}  // namespace environment
}  // namespace services
