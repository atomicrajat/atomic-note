// Pin map for the ESP32-S3 / 1.54" 200x200 e-paper board.
//
// These are physical facts about the PCB, not design choices. Verified against
// the board's own reference firmware and the ES8311 codec board descriptor.
#pragma once

#include <driver/gpio.h>

namespace pins {

// ── e-Paper panel (SSD1681 over SPI) ──────────────────────────────────────
constexpr gpio_num_t EPD_RST  = GPIO_NUM_9;
constexpr gpio_num_t EPD_DC   = GPIO_NUM_10;
constexpr gpio_num_t EPD_CS   = GPIO_NUM_11;
constexpr gpio_num_t EPD_SCK  = GPIO_NUM_12;
constexpr gpio_num_t EPD_MOSI = GPIO_NUM_13;
constexpr gpio_num_t EPD_BUSY = GPIO_NUM_8;   // HIGH while the panel is busy

// ── Power rails ───────────────────────────────────────────────────────────
// The two rail enables are ACTIVE LOW. The battery latch is ACTIVE HIGH and
// must be asserted within the first few ms of boot or the board powers itself
// off again the moment the user releases the button.
constexpr gpio_num_t RAIL_EPD    = GPIO_NUM_6;
constexpr gpio_num_t RAIL_AUDIO  = GPIO_NUM_42;
constexpr gpio_num_t BATT_LATCH  = GPIO_NUM_17;

// ── Battery sense ─────────────────────────────────────────────────────────
// On ADC1. The divider halves the cell voltage, so measured mV * 2 = VBAT.
constexpr gpio_num_t BATT_ADC = GPIO_NUM_4;

// ── Buttons (both active LOW, need pullups) ───────────────────────────────
// A doubles as the BOOT strap pin, so it must be released during reset.
// Vibration motor, via a module with its own driver transistor.
//
// GPIO 3 is a strapping pin (JTAG source select) sampled at reset, and is left
// untouched until haptics::begin() runs. It is also one of only five pins this
// board leaves free — 1, 2, 3, 5 and 7 — once the panel, both rails, the
// battery, both buttons, I2C, SD, I2S, native USB, the SPI flash and the OCTAL
// PSRAM (which takes 33-37) have had theirs.
constexpr gpio_num_t HAPTIC = GPIO_NUM_3;

constexpr gpio_num_t BTN_A = GPIO_NUM_0;    // "action"  — select / record
constexpr gpio_num_t BTN_B = GPIO_NUM_18;   // "nav"     — next / menu

// ── I2C bus ───────────────────────────────────────────────────────────────
constexpr gpio_num_t I2C_SDA = GPIO_NUM_47;
constexpr gpio_num_t I2C_SCL = GPIO_NUM_48;

constexpr uint8_t ADDR_RTC   = 0x51;   // PCF85063
constexpr uint8_t ADDR_SHTC3 = 0x70;   // temperature + humidity

// ── SD card (SD_MMC, 1-bit mode) ──────────────────────────────────────────
constexpr int SD_CLK = 39;
constexpr int SD_CMD = 41;
constexpr int SD_D0  = 40;

// ── Audio: ES8311 duplex codec over I2S ───────────────────────────────────
constexpr int I2S_MCLK = 14;
constexpr int I2S_BCLK = 15;
constexpr int I2S_WS   = 38;
constexpr int I2S_DOUT = 45;
constexpr int I2S_DIN  = 16;
constexpr int AUDIO_PA = 46;   // speaker amplifier enable

}  // namespace pins
