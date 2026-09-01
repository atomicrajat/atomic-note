#include "dashboard.h"

#include <Arduino.h>

#include "../board/power.h"
#include "../display/epaper.h"
#include "../display/numerals.h"
#include "../services/network.h"
#include "../services/rtc.h"
#include "../services/sleep.h"
#include "../ui/router.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"

namespace apps {

namespace widgets = ui::widgets;
namespace theme = ui::theme;

namespace {

// Re-read the sensors this often. Room temperature does not move fast, and each
// reading costs ~15 ms of blocking I2C.
constexpr uint32_t kSensorIntervalMs = 60000;

// ── Layout ────────────────────────────────────────────────────────────────
// An instrument-panel frame with cut corners, and four bands inside it:
// identity, date, clock, readings. The band boundaries are all derived, so
// changing one height moves everything below it correctly.
constexpr int kFrameInset = 3;
constexpr int kFrameChamfer = 15;
constexpr int kFrameThickness = 2;

// Content keeps clear of the frame and of the chamfered corners.
constexpr int kPad = 13;
constexpr int kContentLeft = kPad;
constexpr int kContentRight = 200 - kPad;
constexpr int kContentWidth = kContentRight - kContentLeft;

constexpr int kTitleY = 12;
constexpr int kTitleH = 16;

constexpr int kDateY = 34;
constexpr int kDateH = 34;
// Wide enough for the longest date the format can produce — "26/12/26" runs
// about 90 px at kTitle, plus padding. Narrowing this clips December.
constexpr int kDateW = 106;

constexpr int kClockY = 78;
constexpr int kClockH = 58;
constexpr int kDigitHeight = 54;
constexpr int kMeridiemGap = 6;

constexpr int kRuleY = 142;
constexpr int kRuleH = 3;

constexpr int kReadingY = 152;

const char* kWeekdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

}  // namespace

void Dashboard::onEnter(ui::Router& router) {
  refreshSensors();
  shownMinute_ = -1;  // force a paint on entry
}

void Dashboard::refreshSensors() {
  env_ = services::environment::measure();
  batteryPercent_ = power::batteryPercent();
  lastSensorMs_ = millis();
}

void Dashboard::onTick(ui::Router& router) {
  bool needsPaint = false;

  if (millis() - lastSensorMs_ >= kSensorIntervalMs) {
    refreshSensors();
    needsPaint = true;
  }

  struct tm local;
  if (services::rtc::localNow(&local)) {
    if (local.tm_min != shownMinute_) needsPaint = true;
    if (!clockSet_) needsPaint = true;
  } else if (clockSet_) {
    needsPaint = true;  // clock was lost
  }

  // The WiFi indicator has to react to the radio coming and going.
  if (services::network::isJoined() != shownWifi_) needsPaint = true;

  if (needsPaint) router.invalidate();
}

void Dashboard::draw(gfx::Canvas& canvas) {
  struct tm local;
  clockSet_ = services::rtc::localNow(&local);

  widgets::hudFrame(canvas, kFrameInset, kFrameInset, 200 - 2 * kFrameInset,
                    200 - 2 * kFrameInset, kFrameChamfer, kFrameThickness);

  // ── Identity row ────────────────────────────────────────────────────────
  // Name, a rule filling the slack, then the battery hard right. The rule is
  // what makes the row read as an instrument header rather than a caption; it
  // is measured rather than fixed so the name can change length freely.
  // The name is set in the micro face at 2x rather than 9pt bold: it is the
  // least important thing in the row, and at 9pt it crowded the indicators.
  // It also matches the battery gauge's digits, which use the same face.
  constexpr gfx::Font kNameFont = gfx::Font::kMicro;
  constexpr int kNameScale = 2;
  canvas.text(kContentLeft, kTitleY + 3, "ATOMIC NOTE", kNameFont, gfx::kBlack,
              kNameScale);
  const int nameW = canvas.textWidth("ATOMIC NOTE", kNameFont, kNameScale);

  // Battery reads like a phone's: the percentage sits inside the cell, with the
  // charge level filling behind it.
  constexpr int kGaugeW = 42;
  constexpr int kGaugeH = 19;
  const int gaugeX = kContentRight - kGaugeW - 3;  // 3 = terminal nub
  widgets::batteryGauge(canvas, gaugeX, kTitleY - 2, kGaugeW, kGaugeH,
                        batteryPercent_);

  // WiFi, only while there is actually a link. An always-present icon that
  // changes state is easy to misread at a glance; presence IS the signal.
  shownWifi_ = services::network::isJoined();
  int indicatorLeft = gaugeX;
  if (shownWifi_) {
    constexpr int kWifiW = 18;
    indicatorLeft = gaugeX - kWifiW - 8;
    widgets::iconWifi(canvas, indicatorLeft, kTitleY - 2, gfx::kBlack,
                      kWifiW);
  }

  // The rule takes whatever is left between the name and the indicators.
  const int ruleStart = kContentLeft + nameW + 8;
  const int ruleEnd = indicatorLeft - 8;
  if (ruleEnd > ruleStart) {
    canvas.rect(ruleStart, kTitleY + 6, ruleEnd - ruleStart, 4, gfx::kBlack);
  }

  // ── Date block, inverted ────────────────────────────────────────────────
  canvas.roundRect(kContentLeft, kDateY, kDateW, kDateH, theme::kCardRadius,
                   gfx::kBlack);

  char dateText[16];
  if (clockSet_) {
    snprintf(dateText, sizeof(dateText), "%d/%d/%02d", local.tm_mday,
             local.tm_mon + 1, (local.tm_year + 1900) % 100);
  } else {
    snprintf(dateText, sizeof(dateText), "--/--/--");
  }
  // No "DATE" caption — the d/m/y format identifies itself, and dropping the
  // word lets the value fill the block at a readable size instead of sitting
  // under a label that explains nothing.
  //
  // textInBox centres on the ink. Centring by line height instead reserves
  // descender space that digits never use, which is what left this sitting
  // high in the block.
  canvas.textInBox(kContentLeft, kDateY, kDateW, kDateH, dateText,
                   gfx::Font::kTitle, gfx::kWhite);

  // Weekday and meridiem occupy the space the reference gave to labels we do
  // not need — the sensor's part number and the word "TIME" tell the reader
  // nothing they cannot already see.
  const int rightColX = kContentLeft + kDateW + 12;
  if (clockSet_) {
    canvas.text(rightColX, kDateY + 1, kWeekdays[local.tm_wday % 7],
                gfx::Font::kTitle, gfx::kBlack);
    canvas.text(rightColX, kDateY + 21, (local.tm_hour < 12) ? "AM" : "PM",
                gfx::Font::kLabel, gfx::kBlack);
  }

  // ── Clock ───────────────────────────────────────────────────────────────
  char timeText[8];
  if (clockSet_) {
    int hour12 = local.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    snprintf(timeText, sizeof(timeText), "%d:%02d", hour12, local.tm_min);
    shownMinute_ = local.tm_min;
  } else {
    snprintf(timeText, sizeof(timeText), "--:--");
    shownMinute_ = -1;
  }

  const gfx::numerals::Metrics m = gfx::numerals::metricsFor(kDigitHeight);
  const int clockW = gfx::numerals::widthOf(timeText, m);
  const int clockX = kContentLeft + (kContentWidth - clockW) / 2;
  const int clockTop = kClockY + (kClockH - kDigitHeight) / 2;
  gfx::numerals::draw(canvas, clockX, clockTop, timeText, m, gfx::kBlack);

  // ── Rule ────────────────────────────────────────────────────────────────
  canvas.rect(kContentLeft, kRuleY, kContentWidth, kRuleH, gfx::kBlack);

  // ── Readings ────────────────────────────────────────────────────────────
  // Icon plus number, no word label. A thermometer and a droplet say which is
  // which faster than "TEMP" and "HUM" do, and leave room for a bigger value.
  if (env_.valid) {
    char value[10];

    widgets::iconThermometer(canvas, kContentLeft + 2, kReadingY, gfx::kBlack);
    snprintf(value, sizeof(value), "%.1f", env_.celsius);
    canvas.text(kContentLeft + 20, kReadingY + 6, value, gfx::Font::kTitle,
                gfx::kBlack);
    const int tempW = canvas.textWidth(value, gfx::Font::kTitle);
    canvas.circleOutline(kContentLeft + 26 + tempW, kReadingY + 11, 3, 2,
                         gfx::kBlack);

    const int humX = kContentLeft + 96;
    widgets::iconDroplet(canvas, humX, kReadingY + 2, gfx::kBlack);
    snprintf(value, sizeof(value), "%.0f", env_.humidity);
    canvas.text(humX + 18, kReadingY + 6, value, gfx::Font::kTitle,
                gfx::kBlack);
    const int humW = canvas.textWidth(value, gfx::Font::kTitle);
    canvas.text(humX + 21 + humW, kReadingY + 9, "%", gfx::Font::kLabel,
                gfx::kBlack);
  } else {
    canvas.textAligned(kContentLeft, kReadingY + 8, kContentWidth,
                       "SENSOR OFFLINE", gfx::Font::kBody, gfx::kBlack,
                       gfx::Align::kCenter);
  }

  widgets::buttonMarkers(canvas);
}

bool Dashboard::onEvent(ui::Router& router, input::Button button,
                        input::Event event) {
  // Hold A: start recording immediately. Handled before the click check so the
  // gesture fires while the button is still down, and the recorder screen can
  // capture for as long as it stays down.
  //
  // This is the deliberate exception to "hold A selects": on the root screen
  // there is nothing to select, and push-to-talk has to live on a hold.
  if (button == input::Button::kA && event == input::Event::kLongPress) {
    if (recorder_) {
      router.push(recorder_);
      return true;
    }
    return false;
  }

  // Hold B is back everywhere, and back from the ROOT means "put it away" —
  // there is no earlier screen to return to, so it sleeps instead. That also
  // keeps the manual sleep that tapping B used to do.
  if (button == input::Button::kB && event == input::Event::kLongPress) {
    services::enterDeepSleep();
    return true;
  }

  // The dashboard is the root: there is no list to step through and nothing
  // to go back to, so a tap does the only useful thing and opens the menu.
  // Either button, because at the root there is no "wrong" one.
  if (event != input::Event::kClick) return false;

  if (menu_) {
    router.push(menu_);
  } else {
    refreshSensors();
    router.invalidate(epaper::Refresh::kFull);
  }
  return true;
}

}  // namespace apps
