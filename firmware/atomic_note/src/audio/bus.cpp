#include "bus.h"

#include <ESP_I2S.h>

#include "../board/pins.h"
#include "es8311.h"

namespace audio {
namespace bus {
namespace {

I2SClass i2s;
bool started = false;

}  // namespace

bool begin() {
  if (started) return true;

  if (!es8311::begin(kSampleRate)) return false;

  i2s.setPins(pins::I2S_BCLK, pins::I2S_WS, pins::I2S_DOUT, pins::I2S_DIN,
              pins::I2S_MCLK);

  // begin() opens the transmit side; configureRX adds capture on the same
  // peripheral. Both must use identical clocking — the codec has one clock
  // domain, so a mismatch here is silence rather than a warning.
  if (!i2s.begin(I2S_MODE_STD, kSampleRate, I2S_DATA_BIT_WIDTH_16BIT,
                 I2S_SLOT_MODE_STEREO)) {
    Serial.println("[bus] I2S transmit init failed");
    return false;
  }
  if (!i2s.configureRX(kSampleRate, I2S_DATA_BIT_WIDTH_16BIT,
                       I2S_SLOT_MODE_STEREO)) {
    Serial.println("[bus] I2S capture init failed");
    return false;
  }

  started = true;
  es8311::setAmplifier(false);
  Serial.println("[bus] codec link up (duplex, 16 kHz)");
  return true;
}

bool ready() { return started; }

size_t write(const void* data, size_t bytes) {
  if (!started) return 0;
  return i2s.write((const uint8_t*)data, bytes);
}

size_t read(void* data, size_t bytes) {
  if (!started) return 0;
  return i2s.readBytes((char*)data, bytes);
}

}  // namespace bus
}  // namespace audio
