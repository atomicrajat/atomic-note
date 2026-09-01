#include "epaper.h"

#include <Arduino.h>
#include <SPI.h>

#include "../board/pins.h"
#include "canvas.h"

namespace epaper {
namespace {

// ── SSD1681 command set (subset we use) ───────────────────────────────────
constexpr uint8_t CMD_DRIVER_OUTPUT      = 0x01;
constexpr uint8_t CMD_DEEP_SLEEP         = 0x10;
constexpr uint8_t CMD_DATA_ENTRY_MODE    = 0x11;
constexpr uint8_t CMD_SW_RESET           = 0x12;
constexpr uint8_t CMD_TEMP_SENSOR        = 0x18;
constexpr uint8_t CMD_MASTER_ACTIVATE    = 0x20;
constexpr uint8_t CMD_UPDATE_CONTROL_2   = 0x22;
constexpr uint8_t CMD_WRITE_RAM_BW       = 0x24;
constexpr uint8_t CMD_WRITE_RAM_RED      = 0x26;
constexpr uint8_t CMD_BORDER_WAVEFORM    = 0x3C;
constexpr uint8_t CMD_SET_RAM_X_RANGE    = 0x44;
constexpr uint8_t CMD_SET_RAM_Y_RANGE    = 0x45;
constexpr uint8_t CMD_SET_RAM_X_COUNTER  = 0x4E;
constexpr uint8_t CMD_SET_RAM_Y_COUNTER  = 0x4F;

// Update sequences fed to CMD_UPDATE_CONTROL_2.
constexpr uint8_t SEQ_FULL    = 0xF7;
constexpr uint8_t SEQ_PARTIAL = 0xFF;

constexpr int kWidth = 200;
constexpr int kHeight = 200;

SPIClass spi(FSPI);
uint16_t partialsSinceFull = 0;
bool asleep = true;
Orientation currentOrientation = kDefaultOrientation;
void (*busyHook)() = nullptr;

// Read one pixel out of the canvas in its own coordinate space.
// A set bit means white, matching the framebuffer convention.
inline bool canvasPixel(const uint8_t* bits, int stride, int x, int y) {
  return (bits[y * stride + (x >> 3)] >> (7 - (x & 7))) & 1;
}

// The panel drives BUSY high while it is updating.
void waitIdle() {
  const uint32_t deadline = millis() + 12000;
  while (digitalRead(pins::EPD_BUSY) == HIGH) {
    if ((int32_t)(millis() - deadline) > 0) {
      Serial.println("[epd] BUSY stuck high — giving up on this frame");
      return;
    }
    if (busyHook) busyHook();
    delay(2);
  }
}

void writeCommand(uint8_t cmd) {
  digitalWrite(pins::EPD_DC, LOW);
  digitalWrite(pins::EPD_CS, LOW);
  spi.transfer(cmd);
  digitalWrite(pins::EPD_CS, HIGH);
}

void writeData(uint8_t data) {
  digitalWrite(pins::EPD_DC, HIGH);
  digitalWrite(pins::EPD_CS, LOW);
  spi.transfer(data);
  digitalWrite(pins::EPD_CS, HIGH);
}

void writeData(const uint8_t* data, size_t len) {
  digitalWrite(pins::EPD_DC, HIGH);
  digitalWrite(pins::EPD_CS, LOW);
  // writeBytes does not modify the buffer despite the non-const signature.
  spi.writeBytes(const_cast<uint8_t*>(data), len);
  digitalWrite(pins::EPD_CS, HIGH);
}

void hardwareReset() {
  digitalWrite(pins::EPD_RST, HIGH);
  delay(20);
  digitalWrite(pins::EPD_RST, LOW);
  delay(5);
  digitalWrite(pins::EPD_RST, HIGH);
  delay(20);
  waitIdle();
}

// Point the RAM window at the whole panel and park the cursor at its origin.
// Y runs bottom-to-top in the controller's address space, hence the reversed
// range and the counter starting at the last row.
void setFullWindow() {
  writeCommand(CMD_SET_RAM_X_RANGE);
  writeData(0x00);
  writeData((kWidth / 8) - 1);

  writeCommand(CMD_SET_RAM_Y_RANGE);
  writeData((kHeight - 1) & 0xFF);
  writeData(((kHeight - 1) >> 8) & 0xFF);
  writeData(0x00);
  writeData(0x00);

  writeCommand(CMD_SET_RAM_X_COUNTER);
  writeData(0x00);
  writeCommand(CMD_SET_RAM_Y_COUNTER);
  writeData((kHeight - 1) & 0xFF);
  writeData(((kHeight - 1) >> 8) & 0xFF);
}

void panelInit() {
  hardwareReset();

  writeCommand(CMD_SW_RESET);
  waitIdle();

  writeCommand(CMD_DRIVER_OUTPUT);
  writeData((kHeight - 1) & 0xFF);
  writeData(((kHeight - 1) >> 8) & 0xFF);
  writeData(0x00);

  writeCommand(CMD_DATA_ENTRY_MODE);
  writeData(0x01);  // X increment, Y decrement — matches the window above

  setFullWindow();

  writeCommand(CMD_BORDER_WAVEFORM);
  writeData(0x05);

  writeCommand(CMD_TEMP_SENSOR);
  writeData(0x80);  // use the panel's internal sensor to pick a waveform

  waitIdle();
  asleep = false;
}

// Stream the framebuffer to the panel, applying the orientation transform on
// the way out. CS is held low for the whole frame so this stays one logical
// transfer even though it is issued row by row.
void writeFramebuffer(const gfx::Canvas& canvas) {
  const int stride = canvas.stride();
  const int w = canvas.width();
  const int h = canvas.height();
  const uint8_t* bits = canvas.bits();

  uint8_t rowBuf[32];  // 200px / 8 = 25 bytes; 32 covers any panel this size
  if (stride > (int)sizeof(rowBuf)) return;

  digitalWrite(pins::EPD_DC, HIGH);
  digitalWrite(pins::EPD_CS, LOW);

  const uint8_t turns = currentOrientation.quarterTurns & 3;
  const bool mirror = currentOrientation.mirror;

  // Walk the PANEL in its own scan order and pull each pixel from wherever the
  // canvas keeps it. Doing it this way means any rotation costs the same, and
  // the canvas never has to know how the glass is mounted.
  //
  // ~40k iterations per frame, a couple of milliseconds — irrelevant next to
  // the panel's own 500 ms refresh.
  for (int py = 0; py < h; py++) {
    memset(rowBuf, 0, stride);
    for (int px = 0; px < w; px++) {
      int sx, sy;
      switch (turns) {
        case 1:  sx = py;         sy = h - 1 - px; break;  // 90 CW
        case 2:  sx = w - 1 - px; sy = h - 1 - py; break;  // 180
        case 3:  sx = w - 1 - py; sy = px;         break;  // 270 CW
        default: sx = px;         sy = py;         break;  // 0
      }
      if (mirror) sx = w - 1 - sx;
      if (canvasPixel(bits, stride, sx, sy)) {
        rowBuf[px >> 3] |= (uint8_t)(0x80 >> (px & 7));
      }
    }
    spi.writeBytes(rowBuf, stride);
  }

  digitalWrite(pins::EPD_CS, HIGH);
}

void runUpdate(uint8_t sequence) {
  writeCommand(CMD_UPDATE_CONTROL_2);
  writeData(sequence);
  writeCommand(CMD_MASTER_ACTIVATE);
  waitIdle();
}

}  // namespace

void setBusyHook(void (*hook)()) { busyHook = hook; }

bool begin() {
  pinMode(pins::EPD_CS, OUTPUT);
  pinMode(pins::EPD_DC, OUTPUT);
  pinMode(pins::EPD_RST, OUTPUT);
  pinMode(pins::EPD_BUSY, INPUT);
  digitalWrite(pins::EPD_CS, HIGH);

  spi.begin(pins::EPD_SCK, -1, pins::EPD_MOSI, -1);
  spi.setFrequency(20000000);
  spi.setDataMode(SPI_MODE0);
  spi.setBitOrder(MSBFIRST);

  panelInit();
  partialsSinceFull = 0;
  return true;
}

void present(const gfx::Canvas& canvas, Refresh mode) {
  if (asleep) panelInit();

  if (!canvas.bits()) return;

  bool full = (mode == Refresh::kFull);
  if (mode == Refresh::kAuto && partialsSinceFull >= kPartialsBeforeFull) {
    full = true;
  }

  setFullWindow();
  writeCommand(CMD_WRITE_RAM_BW);
  writeFramebuffer(canvas);

  if (full) {
    // Seed the "previous frame" bank too, so the next partial update has a
    // correct reference and does not ghost the frame before this one.
    setFullWindow();
    writeCommand(CMD_WRITE_RAM_RED);
    writeFramebuffer(canvas);
    runUpdate(SEQ_FULL);
    partialsSinceFull = 0;
  } else {
    runUpdate(SEQ_PARTIAL);
    partialsSinceFull++;
  }
}

void setOrientation(Orientation o) { currentOrientation = o; }

Orientation orientation() { return currentOrientation; }

void sleep() {
  if (asleep) return;
  writeCommand(CMD_DEEP_SLEEP);
  writeData(0x01);
  delay(10);
  asleep = true;
}

}  // namespace epaper
