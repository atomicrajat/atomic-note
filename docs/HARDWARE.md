# Hardware notes

ESP32-S3 (8MB flash, OPI PSRAM) with a 1.54" 200×200 monochrome e-paper panel,
ES8311 duplex audio codec, microSD, PCF85063 RTC, and SHTC3 temp/humidity.

Verified against the board at bring-up: PSRAM reports 8 MB free, panel
initializes, framebuffer allocates from PSRAM.

## Pin map

| Function | GPIO | Notes |
|---|---|---|
| EPD RST | 9 | |
| EPD DC | 10 | |
| EPD CS | 11 | |
| EPD SCK | 12 | |
| EPD MOSI | 13 | |
| EPD BUSY | 8 | HIGH while the panel is refreshing |
| EPD rail enable | 6 | **active LOW** |
| Audio rail enable | 42 | **active LOW** |
| Battery latch | 17 | **active HIGH — assert first thing in `setup()`** |
| Battery ADC | 4 | ADC1, 11 dB atten, ×2 divider |
| Button A (BOOT) | 0 | active LOW, pullup; also the boot strap pin |
| Button B | 18 | active LOW, pullup |
| I2C SDA | 47 | PCF85063 @ `0x51`, SHTC3 @ `0x70` |
| I2C SCL | 48 | |
| SD CLK / CMD / D0 | 39 / 41 / 40 | SD_MMC, 1-bit mode |
| I2S MCLK / BCLK / WS | 14 / 15 / 38 | ES8311 |
| I2S DOUT / DIN | 45 / 16 | |
| Audio PA enable | 46 | |
| Haptic motor | 3 | added, not on the board — see below |

Add-ons hang off the same I2C bus as the RTC and the environment sensor:

| Part | Address | Notes |
|---|---|---|
| MPU6050 | `0x68` / `0x69` | AD0 low / high; `WHO_AM_I` = `0x68` |
| VL53L0X | `0x29` | ID reg `0xC0` = `0xEE` |
| ES8311 codec | `0x18` | built in, on the same bus |

`0x29` alone does not identify the part: the VL53L0X and VL53L1X share it and
share nothing else — different register widths, different init, different API.
The `tof` console command reads both ID schemes and says which is present.

## Bring-up commands

`tof`, `motion`, `dist`, `buzz`, `rail`, `rot`, `shot` and `screen` live behind
`config::kDevTools` in `src/config.h` and are **compiled out by default**. None
is a product feature — a shipped device has no reason to dump its framebuffer
or drive its motor on command — but they are how every threshold in this
project was chosen, against real hardware rather than a datasheet. Turn the
flag on, flash, measure, turn it off.

The MPU6050 is configured for **±8 g**, not the ±2 g default. A deliberate
shake peaks well past 2 g, and at the default range every sample in the
interesting part of the motion clips at the rail — the sensor reports the same
number for a firm shake and a violent one, which is the distinction a shake
detector needs. Clone parts returning `WHO_AM_I` of `0x70`–`0x98` are accepted;
rejecting them would mean rejecting most boards actually sold.

## There is no user LED

Worth stating, because it is the first thing you look for. The reference
firmware — written by someone with the schematic — defines pins for the panel,
both rails, the battery latch and ADC, both buttons, I2C and SD, and **no
LED**. Neither does the vendor's board definition. Any LED on the board is
wired to the charger IC and is not software-controllable.

## What is actually free

Only five pins, once everything is accounted for:

| Claimed by | GPIO |
|---|---|
| Firmware (see the map above) | 0, 4, 6, 8–18, 38–42, 45–48 |
| SPI flash | 26–32 |
| **Octal** PSRAM (`PSRAM=opi`) | 33–37 |
| Native USB D− / D+ | 19, 20 |
| UART0 — boot chatter, even unused | 43, 44 |
| **Free** | **1, 2, 3, 5, 7** |

GPIO 22–25 do not exist on the S3. All five free pins are in the RTC domain,
so they can hold a level through deep sleep.

**GPIO 3 now drives a vibration motor.** It is a strapping pin (JTAG source
select) sampled at reset, which is why nothing touches it before
`haptics::begin()`; a motor pulling it toward ground at reset selects the
default anyway.

**A pin cannot drive a motor directly.** An ESP32-S3 GPIO is rated 40 mA
absolute and a vibration motor pulls 60–100 mA, so the module must carry its
own transistor and flyback diode. The common breakouts do; a bare motor
soldered to the pin will damage it.

## Things that will bite you

**The battery latch.** GPIO 17 must be driven HIGH within the first few
milliseconds of boot. The power button only holds the rail up while physically
pressed; the firmware takes over from there. Miss it and the board dies the
instant the user lets go. This is why `power::latch()` is the first statement in
`setup()`, before even `Serial.begin()`.

**Rail enables are inverted.** LOW turns a rail *on*.

**The "audio" rail also powers the RTC.** GPIO 42 is named for the codec, but
it feeds every peripheral on that supply — including the PCF85063. Proven by
I2C scan:

```
audio rail OFF -> 0x18, 0x70          (codec, SHTC3)
audio rail ON  -> 0x18, 0x51, 0x70    (codec, RTC, SHTC3)
```

So the rail must be up *before* I2C init, even in builds that never use audio.
Leaving it off makes the RTC look depopulated, which is exactly the wrong
conclusion — the chip is fitted and fine.

**The RTC keeps time with that rail cut.** Its backup supply is wired to the
battery. Verified by setting the clock, cutting the rail for 15 s, restoring
it, and reading the chip: the time had advanced correctly. The rail is needed
only to *talk* to the chip, not to keep it running — so deep sleep can drop it
freely without losing time.

**Do not toggle rails at runtime on a USB-powered board.** Cutting the audio
rail while running knocks out the USB CDC. Harmless on battery, but it will end
your debug session.

**Button A is the BOOT strap.** It must be released during reset or the chip
enters download mode. Fine in practice, but it means A cannot be used as a
"hold during power-on" gesture.

**Deep sleep drops the USB CDC.** The board disappears from `/dev/cu.*` while
asleep and cannot be flashed. Press either button to wake it. Screens used
during bring-up should override `blocksSleep()`.

**The e-paper BUSY line is asserted HIGH**, opposite to some other panels.

## Display orientation

The SSD1681 can reverse its Y scan but cannot cleanly mirror X — bit order
within a byte is fixed in hardware. So the transform is applied while streaming
the framebuffer, in `epaper::writeFramebuffer()`. Four combinations are exposed
as `epaper::kOrientations`; the correct index is set once and then left alone.

## Audio

The ES8311 is a single duplex codec: microphone and speaker share one I2S
peripheral and therefore one sample rate. Recording at 16 kHz means capturing
stereo frames and taking every other sample for mono.

Espressif's `esp_codec_dev` (Apache-2.0) is the intended driver rather than
poking ES8311 registers directly.

## Unused by the reference firmware

The **SHTC3** temperature/humidity sensor is populated and on the I2C bus, but
the board's own reference firmware never reads it. It drives our dashboard.
