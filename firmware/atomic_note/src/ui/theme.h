// Design tokens.
//
// One place for the numbers that make screens look like they belong to the same
// device. Screens should reference these rather than inventing their own radii
// and margins — that is what keeps a dozen features from drifting apart.
#pragma once

namespace ui {
namespace theme {

// ── Corner rounding ───────────────────────────────────────────────────────
// The house style is RECTANGULAR WITH A SMALL FILLET. Shapes should still read
// as rectangles; the radius only knocks the hard point off the corner. Nothing
// here is a pill or a stadium — resist raising these numbers much. On a 200px
// panel a radius past ~10 starts reading as oval.
constexpr int kScreenRadius = 7;  // matches the case cutout — kept deliberately tight
constexpr int kCardRadius = 5;    // list rows, panels
constexpr int kChipRadius = 3;    // small tags, badges, button nubs
constexpr int kBannerRadius = 6;  // large full-width blocks

// Keep content clear of the corner arcs.
constexpr int kMargin = 14;

// Line weights.
constexpr int kStrokeThin = 1;
constexpr int kStrokeBold = 2;

// ── Copy style ────────────────────────────────────────────────────────────
// Title Case for headers and anything naming the product or a feature —
// "Atomic Note", "Note List", "Pomodoro". Sentence case for body text and
// button hints. No all-lowercase styling.

}  // namespace theme
}  // namespace ui
