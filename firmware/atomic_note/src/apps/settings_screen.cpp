#include "settings_screen.h"

#include <Arduino.h>
#include <SD_MMC.h>

#include "../audio/es8311.h"
#include "../audio/sound.h"
#include "../board/haptics.h"
#include "../board/power.h"
#include "../display/epaper.h"
#include "../services/notes.h"
#include "../services/sensors.h"
#include "../services/settings.h"
#include "../services/storage.h"
#include "../ui/router.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"

namespace apps {

namespace widgets = ui::widgets;
namespace theme = ui::theme;
namespace storage = services::storage;
namespace settings = services::settings;
namespace sensors = services::sensors;

namespace {

constexpr int kPad = 13;
// 28 rather than 26: a row that has to stack a label above its value needs
// room for two lines, and 5 x 28 from y=34 still clears the page dots at 182.
constexpr int kRowH = 28;

// Bytes as something read at a glance. One decimal below ten units and none
// above, because "12.4 GB" is noise where "12 GB" is the answer.
String humanBytes(uint64_t bytes) {
  const char* units[] = {"B", "KB", "MB", "GB"};
  double value = (double)bytes;
  int unit = 0;
  while (value >= 1024.0 && unit < 3) {
    value /= 1024.0;
    unit++;
  }
  char buf[16];
  if (unit == 0 || value >= 10.0) {
    snprintf(buf, sizeof(buf), "%d %s", (int)(value + 0.5), units[unit]);
  } else {
    snprintf(buf, sizeof(buf), "%.1f %s", value, units[unit]);
  }
  return String(buf);
}

}  // namespace

void SettingsScreen::addHeading(const char* label) {
  if (rowCount_ >= kMaxRows) return;
  Row& r = rows_[rowCount_++];
  r.kind = Kind::kHeading;
  snprintf(r.label, sizeof(r.label), "%s", label);
  r.value[0] = '\0';
  r.toggle = Toggle::kNone;
  r.on = false;
}

void SettingsScreen::addInfo(const char* label, const String& value) {
  if (rowCount_ >= kMaxRows) return;
  Row& r = rows_[rowCount_++];
  r.kind = Kind::kInfo;
  snprintf(r.label, sizeof(r.label), "%s", label);
  snprintf(r.value, sizeof(r.value), "%s", value.c_str());
  r.toggle = Toggle::kNone;
  r.on = false;
}

void SettingsScreen::addToggle(const char* label, Toggle which, bool on) {
  if (rowCount_ >= kMaxRows) return;
  Row& r = rows_[rowCount_++];
  r.kind = Kind::kToggle;
  snprintf(r.label, sizeof(r.label), "%s", label);
  r.value[0] = '\0';
  r.toggle = which;
  r.on = on;
}

void SettingsScreen::addChoice(const char* label, Toggle which,
                               const String& value) {
  if (rowCount_ >= kMaxRows) return;
  Row& r = rows_[rowCount_++];
  r.kind = Kind::kChoice;
  snprintf(r.label, sizeof(r.label), "%s", label);
  snprintf(r.value, sizeof(r.value), "%s", value.c_str());
  r.toggle = which;
  r.on = false;
}

void SettingsScreen::rebuild() {
  rowCount_ = 0;

  // ── Storage ─────────────────────────────────────────────────────────────
  addHeading("STORAGE");
  if (!storage::available()) {
    addInfo("Card", "none");
  } else {
    const uint64_t total = storage::totalBytes();
    uint64_t used = storage::usedBytes();
    if (used > total) used = total;

    if (total == 0) {
      addInfo("Card", "unreadable");
    } else {
      // Free rather than used: "how much room is left" is the question, and a
      // bar that empties as the card fills reads the wrong way round for it.
      const uint64_t freeBytes = total - used;
      const int freePct = (int)((freeBytes * 100ULL) / total);

      char pct[8];
      snprintf(pct, sizeof(pct), "%d%%", freePct);
      addInfo("FREE", String(pct));
      // "7.4/7.6 GB", not "7.4 GB of 7.6 GB": the units repeat and the row is
      // 156px wide.
      addInfo("CARD", humanBytes(freeBytes) + "/" + humanBytes(total));

      // What the recordings themselves take, which is the figure that answers
      // "can I keep using this" — the rest of the card may be anything.
      uint64_t notesBytes = 0;
      const int noteTotal = services::notes::count();
      for (int i = 0; i < noteTotal; i++) {
        const services::notes::Note* n = services::notes::at(i);
        if (!n) continue;
        File f = SD_MMC.open(services::notes::wavPath(n->number).c_str(),
                             FILE_READ);
        if (!f) continue;
        notesBytes += f.size();
        f.close();
      }
      char count[8];
      snprintf(count, sizeof(count), "%d", noteTotal);
      // A plain comma, not a middle dot. The body face is FreeSans9pt7b, which
  // covers ASCII 0x20-0x7E and nothing else — U+00B7 draws as a blank, so the
  // row read "33  7.8 MB" with an unexplained gap in it. Caught by looking at
  // a captured screenshot; on the panel at arm's length it is invisible.
  addInfo("NOTES", String(count) + ", " + humanBytes(notesBytes));
    }
  }

  // ── Sensors ─────────────────────────────────────────────────────────────
  // Detected, not configured. Add-ons are listed with what they are FOR,
  // because that is what decides which apps become available once those
  // exist; built-in parts are shown too so the bus is fully accounted for.
  addHeading("SENSORS");
  sensors::detect();
  if (sensors::addonCount() == 0) {
    addInfo("ATTACHED", "none");
  }
  for (int i = 0; i < sensors::count(); i++) {
    const sensors::Entry* e = sensors::at(i);
    if (!e || e->builtin) continue;
    addInfo(sensors::kindLabel(e->kind), String(e->name));
  }
  for (int i = 0; i < sensors::count(); i++) {
    const sensors::Entry* e = sensors::at(i);
    if (!e || !e->builtin) continue;
    addInfo("BUILT IN", String(e->name));
  }

  // ── Options ─────────────────────────────────────────────────────────────
  addHeading("OPTIONS");
  addToggle("Sound", Toggle::kSound, settings::soundEnabled());
  char vol[8];
  snprintf(vol, sizeof(vol), "%d%%", settings::volume());
  addChoice("Volume", Toggle::kVolume, String(vol));
  addToggle("Vibrate", Toggle::kHaptics, settings::hapticsEnabled());
  // "Speak answers" is 123px against the 112px a toggle row leaves beside its
  // pill, so it was being cut to "Speak answ...". "Read aloud" says the same
  // thing and fits.
  addToggle("Read aloud", Toggle::kSpeak, settings::speakAnswers());
  addToggle("Auto sync", Toggle::kAutoSync, settings::autoSync());

  // ── Power ───────────────────────────────────────────────────────────────
  addHeading("POWER");
  // Percent and volts on separate rows. Together they measure 159px against
  // a 156px row — three pixels over, which is exactly the kind of overflow
  // that is invisible in the source and obvious on the panel.
  char pctText[8];
  snprintf(pctText, sizeof(pctText), "%d%%", power::batteryPercent());
  addInfo("BATTERY", String(pctText));

  char voltsText[10];
  snprintf(voltsText, sizeof(voltsText), "%.2fV", power::batteryVolts());
  addInfo("VOLTAGE", String(voltsText));

  // Two rows, not one. "265 KB · 8.0 MB PSRAM" is 21 characters and overflows
  // the row on its own, before a label is anywhere near it.
  addInfo("HEAP", humanBytes(ESP.getFreeHeap()));
  addInfo("PSRAM", humanBytes(ESP.getFreePsram()));

  if (!selectable(cursor_)) step(1);
}

bool SettingsScreen::selectable(int index) const {
  return index >= 0 && index < rowCount_ &&
         rows_[index].kind != Kind::kHeading;
}

void SettingsScreen::step(int delta) {
  if (rowCount_ == 0) return;
  // Walk past headings rather than letting the cursor rest on one. At most
  // one full lap, so a list of nothing but headings terminates.
  for (int i = 0; i < rowCount_; i++) {
    cursor_ = (cursor_ + delta + rowCount_) % rowCount_;
    if (selectable(cursor_)) return;
  }
}

void SettingsScreen::onEnter(ui::Router& router) {
  cursor_ = 0;
  rebuild();
  router.invalidate(epaper::Refresh::kFull);
}

void SettingsScreen::draw(gfx::Canvas& canvas) {
  const int w = canvas.width();
  const int width = w - 2 * kPad - 6;

  widgets::hudFrame(canvas, 3, 3, w - 6, canvas.height() - 6, 15, 2);

  canvas.text(kPad, 12, "Settings", gfx::Font::kLabel, gfx::kBlack);
  const int titleW = canvas.textWidth("Settings", gfx::Font::kLabel);
  canvas.rect(kPad + titleW + 8, 18, width - titleW - 8, 4, gfx::kBlack);

  const int page = cursor_ / kRowsPerPage;
  const int first = page * kRowsPerPage;
  const int shown = min(kRowsPerPage, rowCount_ - first);

  for (int i = 0; i < shown; i++) {
    const int index = first + i;
    const Row& r = rows_[index];
    const int y = 34 + i * kRowH;

    if (r.kind == Kind::kHeading) {
      canvas.text(kPad, y + 8, r.label, gfx::Font::kMicro, gfx::kBlack, 2);
      const int lw = canvas.textWidth(r.label, gfx::Font::kMicro, 2);
      canvas.rect(kPad + lw + 6, y + 11, width - lw - 6, 2, gfx::kBlack);
      continue;
    }

    const bool selected = (index == cursor_);
    if (selected) {
      canvas.roundRect(kPad - 3, y, width + 6, kRowH - 2, theme::kCardRadius,
                       gfx::kBlack);
    }
    const gfx::Ink ink = selected ? gfx::kWhite : gfx::kBlack;

    const int innerX = kPad + 6;
    const int innerW = width - 12;

    if (r.kind == Kind::kToggle) {
      // A pill that is filled when on and outlined when off, so the state
      // reads from across a desk without looking for the word.
      const char* text = r.on ? "ON" : "OFF";
      const int tw = canvas.textWidth(text, gfx::Font::kMicro, 2);
      const int bw = tw + 12;
      const int bx = kPad + width - bw;
      const int by = y + (kRowH - 2 - 14) / 2;
      if (r.on) {
        canvas.roundRect(bx, by, bw, 14, 4, ink);
        canvas.textInBox(bx, by + 4, bw, 8, text, gfx::Font::kMicro,
                         selected ? gfx::kBlack : gfx::kWhite,
                         gfx::Align::kCenter, 2);
      } else {
        canvas.roundRectOutline(bx, by, bw, 14, 4, 1, ink);
        canvas.textInBox(bx, by + 4, bw, 8, text, gfx::Font::kMicro, ink,
                         gfx::Align::kCenter, 2);
      }

      // The label gets whatever the pill leaves, and is cut with an ellipsis
      // rather than being allowed to run underneath it.
      canvas.textInBox(innerX, y, bx - innerX - 8, kRowH - 2, r.label,
                       gfx::Font::kBody, ink, gfx::Align::kLeft);
      continue;
    }

    // ── Info rows ─────────────────────────────────────────────────────────
    // Label and value were previously drawn into two boxes that started at
    // the same x — one left-aligned, one right-aligned — so anything long
    // ran straight through the other. "Memory / 265 KB · 8.0 MB PSRAM" and
    // "MOTION / MPU6050/MPU9250 IMU" both overflow a 156px row.
    //
    // Measured first, then laid out. Side by side while they fit, which is
    // the common case and the most compact; stacked when they do not, with
    // the label shrunk to the small caps face above its value. Nothing
    // scrolls: a partial refresh on this panel is about half a second, so
    // moving text would crawl and leave ghosting behind it.
    // The label is set in small caps and the value in the body face. That is
    // a hierarchy — the value is the thing you came to read — and it is also
    // what makes the row fit: FreeSans9pt is wide enough that a body-face
    // label and a body-face value together overflow 156px for anything
    // longer than "Battery / 87%".
    const int labelW = canvas.textWidth(r.label, gfx::Font::kMicro, 2);
    const int valueW = canvas.textWidth(r.value, gfx::Font::kBody);
    constexpr int kGap = 10;

    canvas.textInBox(innerX, y, innerW, kRowH - 2, r.label,
                     gfx::Font::kMicro, ink, gfx::Align::kLeft, 2);

    if (labelW + kGap + valueW <= innerW) {
      canvas.textInBox(innerX, y, innerW, kRowH - 2, r.value,
                       gfx::Font::kBody, ink, gfx::Align::kRight);
    } else {
      // Still too wide even so. Cut it with an ellipsis rather than let it
      // run back under the label — and never scroll it: a partial refresh on
      // this panel is about half a second, so moving text would crawl and
      // leave ghosting behind it. Every value here is kept short enough at
      // the source that this is a backstop, not the normal path.
      canvas.textInBox(innerX + labelW + kGap, y,
                       innerW - labelW - kGap, kRowH - 2, r.value,
                       gfx::Font::kBody, ink, gfx::Align::kRight);
    }
  }

  const int pages = (rowCount_ + kRowsPerPage - 1) / kRowsPerPage;
  if (pages > 1) {
    const int gap = 10;
    const int startX = (w - (pages - 1) * gap) / 2;
    const int y = canvas.height() - 18;
    for (int p = 0; p < pages; p++) {
      const int x = startX + p * gap;
      if (p == page) {
        canvas.circle(x, y, 3, gfx::kBlack);
      } else {
        canvas.circleOutline(x, y, 3, 1, gfx::kBlack);
      }
    }
  }
  widgets::buttonMarkers(canvas);
}

bool SettingsScreen::onEvent(ui::Router& router, input::Button button,
                             input::Event event) {
  if (event == input::Event::kClick) {
    step(button == input::Button::kB ? 1 : -1);
    router.invalidate(epaper::Refresh::kPartial);
    return true;
  }

  // Hold A is select everywhere else, so here it flips the row under the
  // cursor. On an info row there is nothing to flip, so it re-reads instead —
  // which is how a card swap or a newly attached sensor shows up.
  if (button == input::Button::kA && event == input::Event::kLongPress) {
    if (!selectable(cursor_)) return true;
    Row& r = rows_[cursor_];

    if (r.kind == Kind::kChoice) {
      if (r.toggle == Toggle::kVolume) {
        // Steps rather than a slider: two buttons cannot express a continuum,
        // and five levels covers the useful range of a small speaker.
        static const int kLevels[] = {50, 65, 75, 85, 100};
        constexpr int kCount = sizeof(kLevels) / sizeof(kLevels[0]);
        const int current = settings::volume();
        int next = kLevels[0];
        for (int i = 0; i < kCount; i++) {
          if (kLevels[i] == current) {
            next = kLevels[(i + 1) % kCount];
            break;
          }
          // A stored value that is not one of the steps — an older build, or
          // the web app — lands on the next step above it.
          if (kLevels[i] > current) {
            next = kLevels[i];
            break;
          }
        }
        settings::setVolume(next);
        audio::es8311::setVolume(next);
        snprintf(r.value, sizeof(r.value), "%d%%", next);
        // Play something at the new level. Choosing a volume you cannot hear
        // is guesswork, and the cue is already the right length to judge by.
        audio::sound::select();
      }
      router.invalidate(epaper::Refresh::kPartial);
      return true;
    }

    if (r.kind != Kind::kToggle) {
      rebuild();
      router.invalidate(epaper::Refresh::kFull);
      return true;
    }

    const bool next = !r.on;
    switch (r.toggle) {
      case Toggle::kSound: settings::setSoundEnabled(next); break;
      case Toggle::kHaptics:
        settings::setHapticsEnabled(next);
        // Buzz on the way on, so switching it on IS the test. Nothing here
        // knows whether a motor is attached, and this is the one moment the
        // user is asking that question.
        if (next) haptics::bump();
        break;
      case Toggle::kSpeak: settings::setSpeakAnswers(next); break;
      case Toggle::kAutoSync: settings::setAutoSync(next); break;
      default: return true;
    }
    r.on = next;
    router.invalidate(epaper::Refresh::kPartial);
    return true;
  }

  return false;  // hold B falls through to the router for Back
}

}  // namespace apps
