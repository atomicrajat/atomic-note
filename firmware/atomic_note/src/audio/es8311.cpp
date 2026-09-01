#include "es8311.h"

#include "../board/pins.h"
#include "../services/i2c_bus.h"

namespace audio {
namespace es8311 {
namespace {

// The codec answers at 0x18 on the shared I2C bus.
constexpr uint8_t kAddress = 0x18;

// ── Registers ─────────────────────────────────────────────────────────────
constexpr uint8_t REG_RESET = 0x00;
constexpr uint8_t REG_CLK01 = 0x01;
constexpr uint8_t REG_CLK02 = 0x02;
constexpr uint8_t REG_CLK03 = 0x03;
constexpr uint8_t REG_CLK04 = 0x04;
constexpr uint8_t REG_CLK05 = 0x05;
constexpr uint8_t REG_CLK06 = 0x06;
constexpr uint8_t REG_CLK07 = 0x07;
constexpr uint8_t REG_CLK08 = 0x08;
constexpr uint8_t REG_SDP_IN = 0x09;   // I2S -> DAC
constexpr uint8_t REG_SDP_OUT = 0x0A;  // ADC -> I2S
constexpr uint8_t REG_SYS0B = 0x0B;
constexpr uint8_t REG_SYS0C = 0x0C;
constexpr uint8_t REG_SYS0D = 0x0D;  // analogue master power
constexpr uint8_t REG_SYS0E = 0x0E;  // ADC modulator + PGA power
constexpr uint8_t REG_SYS10 = 0x10;
constexpr uint8_t REG_SYS11 = 0x11;
constexpr uint8_t REG_SYS12 = 0x12;  // DAC enable
constexpr uint8_t REG_SYS13 = 0x13;
constexpr uint8_t REG_SYS14 = 0x14;  // mic select + PGA gain
constexpr uint8_t REG_ADC16 = 0x16;  // ANALOGUE mic gain, 0-7 = 0..42 dB
constexpr uint8_t REG_ADC17 = 0x17;  // DIGITAL ADC volume
constexpr uint8_t REG_ADC1B = 0x1B;
constexpr uint8_t REG_ADC1C = 0x1C;
constexpr uint8_t REG_DAC31 = 0x31;  // mute
constexpr uint8_t REG_DAC32 = 0x32;  // volume
constexpr uint8_t REG_DAC37 = 0x37;  // ramp rate
constexpr uint8_t REG_GPIO44 = 0x44;

// Clock coefficients for MCLK = 256 x sample rate. Only the rates this device
// uses are listed; add a row from the datasheet before using another.
struct Coeff {
  uint32_t rate;
  uint8_t preDiv;
  uint8_t preMulti;
  uint8_t adcDiv;
  uint8_t dacDiv;
  uint8_t fsMode;
  uint8_t lrckH;
  uint8_t lrckL;
  uint8_t bclkDiv;
  uint8_t adcOsr;
  uint8_t dacOsr;
};

constexpr Coeff kCoeffs[] = {
    // 16 kHz with a 4.096 MHz MCLK — what voice notes and UI tones both use.
    {16000, 1, 1, 1, 1, 0, 0x00, 0xFF, 0x04, 0x10, 0x20},
};
constexpr int kCoeffCount = sizeof(kCoeffs) / sizeof(kCoeffs[0]);

bool detected = false;
bool ampEnabled = false;
int volumePercent = 70;

bool writeReg(uint8_t reg, uint8_t value) {
  return services::i2c::writeRegister(kAddress, reg, &value, 1);
}

bool readReg(uint8_t reg, uint8_t* value) {
  return services::i2c::readRegister(kAddress, reg, value, 1);
}

// Read, mask, set, write — several registers carry unrelated bits that must
// survive.
bool updateReg(uint8_t reg, uint8_t keepMask, uint8_t setBits) {
  uint8_t current = 0;
  if (!readReg(reg, &current)) return false;
  return writeReg(reg, (uint8_t)((current & keepMask) | setBits));
}

const Coeff* coeffFor(uint32_t rate) {
  for (int i = 0; i < kCoeffCount; i++) {
    if (kCoeffs[i].rate == rate) return &kCoeffs[i];
  }
  return nullptr;
}

bool configureClock(const Coeff& c) {
  bool ok = true;

  // REG02: pre-divider in [7:5], pre-multiplier in [4:3]. Low bits are clock
  // state that must be preserved.
  uint8_t multiBits = 0;
  switch (c.preMulti) {
    case 2: multiBits = 1; break;
    case 4: multiBits = 2; break;
    case 8: multiBits = 3; break;
    default: multiBits = 0; break;
  }
  ok &= updateReg(REG_CLK02, 0x07,
                  (uint8_t)(((c.preDiv - 1) << 5) | (multiBits << 3)));

  ok &= writeReg(REG_CLK05,
                 (uint8_t)(((c.adcDiv - 1) << 4) | (c.dacDiv - 1)));
  ok &= updateReg(REG_CLK03, 0x80, (uint8_t)((c.fsMode << 6) | c.adcOsr));
  ok &= updateReg(REG_CLK04, 0x80, c.dacOsr);
  ok &= updateReg(REG_CLK07, 0xC0, c.lrckH);
  ok &= writeReg(REG_CLK08, c.lrckL);

  // Divider values below 19 are stored minus one; above that they are literal.
  const uint8_t bclk = c.bclkDiv < 19 ? (uint8_t)(c.bclkDiv - 1) : c.bclkDiv;
  ok &= updateReg(REG_CLK06, 0xE0, bclk);
  return ok;
}

}  // namespace

bool begin(uint32_t sampleRate) {
  detected = false;

  const Coeff* coeff = coeffFor(sampleRate);
  if (!coeff) {
    Serial.printf("[es8311] no clock coefficients for %u Hz\n",
                  (unsigned)sampleRate);
    return false;
  }

  if (!services::i2c::probe(kAddress)) {
    Serial.println("[es8311] codec not on the bus (is the peripheral rail up?)");
    return false;
  }

  // The chip's first I2C write after power-up is unreliable, so the noise
  // immunity setting is written twice. This is a documented quirk, not
  // superstition.
  writeReg(REG_GPIO44, 0x08);
  if (!writeReg(REG_GPIO44, 0x08)) {
    Serial.println("[es8311] codec did not accept a write");
    return false;
  }

  bool ok = true;
  ok &= writeReg(REG_CLK01, 0x30);
  ok &= writeReg(REG_CLK02, 0x00);
  ok &= writeReg(REG_CLK03, 0x10);
  ok &= writeReg(REG_CLK04, 0x10);
  ok &= writeReg(REG_CLK05, 0x00);
  ok &= writeReg(REG_SYS0B, 0x00);
  ok &= writeReg(REG_SYS0C, 0x00);
  ok &= writeReg(REG_SYS10, 0x1F);
  ok &= writeReg(REG_SYS11, 0x7F);
  ok &= writeReg(REG_RESET, 0x80);

  // Slave mode: the ESP32 drives BCLK and LRCK. Bit 6 clear selects slave.
  ok &= updateReg(REG_RESET, 0xBF, 0x00);

  // Clock source is the external MCLK on GPIO 14, not inverted.
  ok &= writeReg(REG_CLK01, 0x3F);
  ok &= updateReg(REG_CLK06, 0xDF, 0x00);  // SCLK not inverted

  ok &= writeReg(REG_SYS13, 0x10);
  ok &= writeReg(REG_ADC1B, 0x0A);
  ok &= writeReg(REG_ADC1C, 0x6A);
  // Internal reference from ADCL + DACR.
  ok &= writeReg(REG_GPIO44, 0x58);

  ok &= configureClock(*coeff);

  // 16-bit I2S on both directions: format in [1:0] = 00, length in [4:2] = 011.
  constexpr uint8_t kI2s16Bit = 0x0C;
  ok &= writeReg(REG_SDP_IN, kI2s16Bit);
  ok &= writeReg(REG_SDP_OUT, kI2s16Bit);

  // ── Bring the analogue side out of standby ──────────────────────────────
  // These were the difference between a codec that clocks correctly and one
  // that actually captures. REG17 in particular defaults to zero, so the ADC
  // is muted until it is written — the chip reports no error, it just records
  // silence.
  // ── Microphone gain ─────────────────────────────────────────────────────
  // Gain belongs in REG16, the ANALOGUE mic scale, ahead of the converter.
  // REG17 is DIGITAL volume applied after it, and raising that instead
  // amplifies the noise floor along with the signal and clips the peaks —
  // audibly, as crackle. Analogue gain first is the rule for any ADC.
  //
  // 0x07 is 42 dB, the maximum, and is what this board's reference firmware
  // asks for. REG17 stays at its nominal 0 dB.
  ok &= writeReg(REG_ADC16, 0x07);
  ok &= writeReg(REG_ADC17, 0xBF);
  ok &= writeReg(REG_SYS0E, 0x02);  // power up ADC modulator and PGA
  ok &= writeReg(REG_SYS12, 0x00);  // power up the DAC

  // Selects the analogue microphone input. The gain does NOT live here — that
  // is REG16 above.
  ok &= writeReg(REG_SYS14, 0x1A);
  ok &= writeReg(REG_SYS0D, 0x01);  // analogue master power on

  ok &= writeReg(REG_DAC37, 0x08);  // gentle volume ramp, avoids clicks
  ok &= writeReg(REG_DAC31, 0x00);  // unmuted

  if (!ok) {
    Serial.println("[es8311] configuration incomplete");
    return false;
  }

  detected = true;
  setVolume(volumePercent);
  Serial.printf("[es8311] ready at %u Hz\n", (unsigned)sampleRate);
  return true;
}

bool present() { return detected; }

void setVolume(int percent) {
  volumePercent = constrain(percent, 0, 100);
  if (!detected) return;
  // Register 0x32 is 0x00 (silent) to 0xFF. The useful range starts around
  // 0x40; below that the output is inaudible rather than merely quiet, so the
  // scale is compressed into the top of the register.
  const uint8_t reg =
      volumePercent == 0 ? 0x00 : (uint8_t)(0x40 + (volumePercent * 0xBF) / 100);
  writeReg(REG_DAC32, reg);
}

int volume() { return volumePercent; }

void setMicGain(int percent) {
  if (!detected) return;
  // Maps onto the eight analogue steps (0, 6, 12 ... 42 dB). Deliberately not
  // the digital control: see the note in begin().
  const int clamped = constrain(percent, 0, 100);
  writeReg(REG_ADC16, (uint8_t)((clamped * 7) / 100));
}

void mute(bool on) {
  if (!detected) return;
  writeReg(REG_DAC31, on ? 0x60 : 0x00);
}

void setAmplifier(bool on) {
  pinMode(pins::AUDIO_PA, OUTPUT);
  digitalWrite(pins::AUDIO_PA, on ? HIGH : LOW);
  ampEnabled = on;
}

bool amplifierOn() { return ampEnabled; }

bool pokeRegister(uint8_t reg, uint8_t value) { return writeReg(reg, value); }

bool peekRegister(uint8_t reg, uint8_t* value) { return readReg(reg, value); }

}  // namespace es8311
}  // namespace audio
