#include "rtc.h"

#include <Arduino.h>
#include <sys/time.h>

#include "../board/pins.h"
#include "i2c_bus.h"
#include "settings.h"

namespace services {
namespace rtc {
namespace {

// ── PCF85063 registers ────────────────────────────────────────────────────
constexpr uint8_t REG_CONTROL_1 = 0x00;
constexpr uint8_t REG_SECONDS = 0x04;  // bit 7 is the oscillator-stop flag

constexpr uint8_t OS_FLAG = 0x80;

// Anything before this is a clock that was never set. 2024-01-01.
constexpr time_t kPlausibleEpoch = 1704067200;

bool present = false;

uint8_t bcdToDec(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
uint8_t decToBcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

// Days since 1970-01-01 for a proleptic Gregorian y/m/d.
// newlib has no timegm(), and mktime() would apply the process timezone, so
// the conversion is done arithmetically instead — no globals touched.
// (Howard Hinnant's days_from_civil.)
int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = (unsigned)(year - era * 400);             // [0, 399]
  const unsigned doy =
      (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;  // [0, 365]
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;    // [0, 146096]
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

}  // namespace

bool begin() {
  present = i2c::probe(pins::ADDR_RTC);
  if (!present) {
    // Expected on board revisions without the chip populated. Timekeeping
    // falls back to the ESP32 internal RTC.
    Serial.println("[rtc] no PCF85063 — using internal RTC");
    return false;
  }
  // Control_1 = 0: normal mode, 24-hour, oscillator running.
  const uint8_t control = 0x00;
  i2c::writeRegister(pins::ADDR_RTC, REG_CONTROL_1, &control, 1);
  Serial.println("[rtc] PCF85063 present");
  return true;
}

bool hasChip() { return present; }

bool setSystemUtc(time_t epoch) {
  if (epoch < kPlausibleEpoch) return false;

  struct timeval tv = {};
  tv.tv_sec = epoch;
  settimeofday(&tv, nullptr);

  // Mirror into the chip when there is one, so the time survives a power cut.
  if (present) {
    struct tm utc;
    gmtime_r(&epoch, &utc);
    writeUtc(utc);
  }
  return true;
}

bool readUtc(struct tm* out) {
  if (!present || !out) return false;

  uint8_t raw[7] = {};
  if (!i2c::readRegister(pins::ADDR_RTC, REG_SECONDS, raw, sizeof(raw))) {
    return false;
  }
  if (raw[0] & OS_FLAG) return false;  // oscillator stopped — time is garbage

  const int year = bcdToDec(raw[6]) + 2000;
  const int month = bcdToDec(raw[5] & 0x1F);
  const int day = bcdToDec(raw[3] & 0x3F);
  const int hour = bcdToDec(raw[2] & 0x3F);
  const int minute = bcdToDec(raw[1] & 0x7F);
  const int second = bcdToDec(raw[0] & 0x7F);

  // Reject anything the chip could not legitimately hold — a bad read looks
  // like a valid struct otherwise.
  if (year < 2024 || year > 2099) return false;
  if (month < 1 || month > 12) return false;
  if (day < 1 || day > 31) return false;
  if (hour > 23 || minute > 59 || second > 59) return false;

  memset(out, 0, sizeof(struct tm));
  out->tm_year = year - 1900;
  out->tm_mon = month - 1;
  out->tm_mday = day;
  out->tm_hour = hour;
  out->tm_min = minute;
  out->tm_sec = second;
  out->tm_wday = bcdToDec(raw[4] & 0x07);
  return true;
}

bool writeUtc(const struct tm& utc) {
  if (!present) return false;
  const int year = utc.tm_year + 1900;
  if (year < 2000 || year > 2099) return false;

  uint8_t raw[7];
  raw[0] = decToBcd((uint8_t)utc.tm_sec);  // clears OS_FLAG as a side effect
  raw[1] = decToBcd((uint8_t)utc.tm_min);
  raw[2] = decToBcd((uint8_t)utc.tm_hour);
  raw[3] = decToBcd((uint8_t)utc.tm_mday);
  raw[4] = decToBcd((uint8_t)utc.tm_wday);
  raw[5] = decToBcd((uint8_t)(utc.tm_mon + 1));
  raw[6] = decToBcd((uint8_t)(year - 2000));
  return i2c::writeRegister(pins::ADDR_RTC, REG_SECONDS, raw, sizeof(raw));
}

time_t utcToEpoch(struct tm utc) {
  const int64_t days =
      daysFromCivil(utc.tm_year + 1900, (unsigned)(utc.tm_mon + 1),
                    (unsigned)utc.tm_mday);
  return (time_t)(days * 86400 + utc.tm_hour * 3600 + utc.tm_min * 60 +
                  utc.tm_sec);
}

bool timeIsValid() {
  // Whichever source is authoritative, the question is the same: is the
  // system clock past the point where it could still be an unset counter?
  return time(nullptr) >= kPlausibleEpoch;
}

bool syncSystemFromChip() {
  if (!present) return false;
  struct tm utc;
  if (!readUtc(&utc)) return false;
  const time_t epoch = utcToEpoch(utc);
  if (epoch < kPlausibleEpoch) return false;

  struct timeval tv = {};
  tv.tv_sec = epoch;
  settimeofday(&tv, nullptr);
  return true;
}

bool syncChipFromSystem() {
  const time_t now = time(nullptr);
  if (now < kPlausibleEpoch) return false;
  struct tm utc;
  gmtime_r(&now, &utc);
  return writeUtc(utc);
}

// The offset lives in settings, which the web app owns. Keeping a second
// copy here would let the two drift apart.
int utcOffsetMinutes() { return settings::utcOffsetMinutes(); }

void setUtcOffsetMinutes(int minutes) {
  settings::setUtcOffsetMinutes(minutes);
}

bool localNow(struct tm* out) {
  if (!out) return false;
  const time_t now = time(nullptr);
  if (now < kPlausibleEpoch) return false;
  const time_t local = now + (time_t)settings::utcOffsetMinutes() * 60;
  // gmtime_r on a pre-offset epoch gives local wall-clock fields without
  // involving the C library's timezone handling at all.
  gmtime_r(&local, out);
  return true;
}

}  // namespace rtc
}  // namespace services
