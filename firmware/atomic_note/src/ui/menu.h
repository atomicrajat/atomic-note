// A list of destinations, driven by two buttons.
//
// Generic on purpose: the main menu and the settings list are the same object
// with different entries. Adding a feature means adding one row to a table,
// not writing another screen.
//
// Grammar, shared by every list in the product. Matches the button roles the
// board's reference firmware established, so muscle memory carries over:
//   B (lower)  next item, wrapping
//   hold B     previous item
//   A (upper)  open the selected item
//   hold A     back (handled by the Router, works on every screen)
#pragma once

#include "screen.h"

namespace ui {

struct MenuEntry {
  const char* label;
  Screen* screen;  // nullptr renders the row disabled

  // Optional: whether this row is usable right now.
  //
  // A predicate rather than a sensor kind, so Menu stays what it is — a list
  // of destinations that knows nothing about hardware. An app that needs an
  // accelerometer supplies a function that asks about accelerometers; the menu
  // only asks "can this be opened".
  //
  // nullptr means always available, which is every row that does not depend on
  // something being plugged in.
  bool (*available)() = nullptr;
};

class Menu : public Screen {
 public:
  Menu(const char* title, const MenuEntry* entries, int count)
      : title_(title), entries_(entries), count_(count) {}

  const char* name() const override { return title_; }

  void onEnter(Router& router) override;
  void draw(gfx::Canvas& canvas) override;
  bool onEvent(Router& router, input::Button button,
               input::Event event) override;

 private:
  // Can the row at `index` be opened? False for an unimplemented row and for
  // one whose hardware is not attached.
  bool usable(int index) const;

  static constexpr int kRowsPerPage = 4;

  const char* title_;
  const MenuEntry* entries_;
  int count_;
  int cursor_ = 0;
};

}  // namespace ui
