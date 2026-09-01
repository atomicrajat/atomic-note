// ES8311 codec control.
//
// A focused driver rather than a port of Espressif's esp_codec_dev: that
// library carries an abstraction layer for a whole family of codecs across
// several buses, and this board has one codec on one bus at one sample rate.
//
// The register VALUES here are hardware facts — the reset sequence and the
// clock-divider coefficients come from the ES8311 datasheet and match
// Espressif's Apache-2.0 driver, which is the reference implementation for
// this part. Getting them wrong produces silence with no diagnostic, so they
// are not guesses.
//
// The chip is a duplex codec: DAC and ADC share one I2S peripheral and
// therefore one sample rate. Playback and recording cannot run at different
// rates.
#pragma once

#include <Arduino.h>

namespace audio {
namespace es8311 {

// Brings the codec out of reset and configures it for `sampleRate`.
// Requires the I2C bus to be up and the peripheral rail powered.
bool begin(uint32_t sampleRate);

bool present();

// 0-100. Applied to the DAC output stage.
void setVolume(int percent);
int volume();

// Analogue mic gain, 0-100. Only affects recording.
void setMicGain(int percent);

void mute(bool on);

// Speaker amplifier enable. Kept separate from volume because the PA is what
// actually costs current — muting alone leaves it drawing.
// This is the SINGLE source of truth for the amplifier. Callers must not cache
// it: the recorder and the player both switch it, and a stale copy elsewhere
// silences everything that trusts the copy instead of the pin.
void setAmplifier(bool on);
bool amplifierOn();

// Direct register access, for bring-up. Sweeping gain settings live beats
// reflashing for each guess.
bool pokeRegister(uint8_t reg, uint8_t value);
bool peekRegister(uint8_t reg, uint8_t* value);

}  // namespace es8311
}  // namespace audio
