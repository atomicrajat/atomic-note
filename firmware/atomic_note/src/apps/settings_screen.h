// Settings: what the device is, and the few things two buttons can change.
//
// Built as a list of ROWS rather than a fixed layout, because this screen is
// the one that will keep growing. Sensors are the next thing to land here —
// an IMU, a range finder, a gesture sensor — and each will bring a toggle and
// the apps it unlocks. A row model absorbs that; a hand-placed layout would
// need redrawing every time.
//
// Three kinds of row and nothing else:
//
//   heading   a section title, skipped by the cursor
//   info      something to read: storage, battery, a detected part
//   toggle    something to change, with hold-A
//
// Anything needing text entry stays in the web app. Two buttons cannot type,
// and a settings screen that pretends otherwise is a worse version of a page
// that already works.
#pragma once

#include "../ui/screen.h"

namespace apps {

class SettingsScreen : public ui::Screen {
 public:
  const char* name() const override { return "Settings"; }

  void onEnter(ui::Router& router) override;
  void draw(gfx::Canvas& canvas) override;
  bool onEvent(ui::Router& router, input::Button button,
               input::Event event) override;

 private:
  // A fourth kind, added when volume needed more than on and off. The row
  // model exists precisely so a new control is a new case here rather than a
  // new screen.
  enum class Kind : uint8_t { kHeading, kInfo, kToggle, kChoice };

  // Which setting a toggle row edits. An enum rather than a function pointer
  // so the table stays a plain aggregate and the switch is in one place.
  enum class Toggle : uint8_t { kNone, kSound, kHaptics, kSpeak, kAutoSync,
                                kVolume };

  struct Row {
    Kind kind;
    char label[18];
    char value[26];
    Toggle toggle;
    bool on;
  };

  // Rooms for the device rows, the toggles, and every sensor the bus can
  // report. Comfortably more than the current list needs.
  static constexpr int kMaxRows = 28;
  static constexpr int kRowsPerPage = 5;

  Row rows_[kMaxRows];
  int rowCount_ = 0;
  int cursor_ = 0;  // index into rows_, never resting on a heading

  // Sampled on entry, never in draw(): usedBytes() walks the allocation table
  // and an I2C sweep talks to hardware. draw() runs on every repaint.
  void rebuild();
  void addHeading(const char* label);
  void addInfo(const char* label, const String& value);
  void addToggle(const char* label, Toggle which, bool on);
  void addChoice(const char* label, Toggle which, const String& value);

  bool selectable(int index) const;
  void step(int delta);
};

}  // namespace apps
