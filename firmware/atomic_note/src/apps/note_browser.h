// Browsing recorded notes.
//
// Two screens because one list of everything is not how you look for a voice
// note — you remember roughly what it was about, not when it was. So: pick a
// tag, then walk that tag's notes.
#pragma once

#include "../ui/screen.h"

namespace apps {

class NoteList;
class NoteDetail;

// Tags, plus "All", each with a count.
class NoteTags : public ui::Screen {
 public:
  const char* name() const override { return "Notes"; }

  void setList(NoteList* list) { list_ = list; }

  void onEnter(ui::Router& router) override;
  void draw(gfx::Canvas& canvas) override;
  bool onEvent(ui::Router& router, input::Button button,
               input::Event event) override;

 private:
  NoteList* list_ = nullptr;
  int cursor_ = 0;
};

// The notes carrying one tag, newest first. A plays, hold-B steps back.
class NoteList : public ui::Screen {
 public:
  const char* name() const override { return "Note list"; }

  // Null means every note.
  void setFilter(const char* tag);

  void setDetail(NoteDetail* detail) { detail_ = detail; }

  void onEnter(ui::Router& router) override;
  void onExit() override;
  void draw(gfx::Canvas& canvas) override;
  bool onEvent(ui::Router& router, input::Button button,
               input::Event event) override;

  uint32_t tickIntervalMs() const override { return 20; }
  void onTick(ui::Router& router) override;

  Idle idlePolicy() const override;

 private:
  static constexpr int kRowsPerPage = 3;

  NoteDetail* detail_ = nullptr;
  String filter_;
  bool filtered_ = false;
  int cursor_ = 0;
  int playingIndex_ = -1;

  const char* filterTag() const;
};

// One note: its transcript, and playback.
//
// A separate screen rather than a mode inside the list, so that holding A
// still means "back" here as it does everywhere else. Cramming read, play,
// next, previous and back onto two buttons in one screen is one action too
// many, and the one that got squeezed out was the way back.
class NoteDetail : public ui::Screen {
 public:
  const char* name() const override { return "Note"; }

  void setNote(int index) { index_ = index; }

  void onEnter(ui::Router& router) override;
  void onExit() override;
  void draw(gfx::Canvas& canvas) override;
  bool onEvent(ui::Router& router, input::Button button,
               input::Event event) override;

  uint32_t tickIntervalMs() const override { return 20; }
  void onTick(ui::Router& router) override;

  Idle idlePolicy() const override;

 private:
  static constexpr int kLinesPerPage = 6;

  int index_ = -1;
  int page_ = 0;
  int totalLines_ = 0;
  bool playing_ = false;
};

}  // namespace apps
