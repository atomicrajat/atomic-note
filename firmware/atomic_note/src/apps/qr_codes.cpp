#include "qr_codes.h"

#include <Arduino.h>
#include <qrcode.h>

#include "../display/epaper.h"
#include "../services/settings.h"
#include "../ui/router.h"
#include "../ui/widgets.h"

namespace apps {

namespace widgets = ui::widgets;
namespace settings = services::settings;

namespace {

// Uses the QR encoder already bundled with the ESP32 core rather than an
// Arduino library. Two reasons: the core's header is also called qrcode.h and
// wins the include path, so a library of the same name cannot be reached from
// here at all; and this way the project needs no extra install step.
//
// Its API is callback-based — esp_qrcode_generate() encodes, then hands the
// result to display_func. The canvas therefore has to reach that callback
// through a file-static, which is safe because rendering is single-threaded
// and synchronous inside draw().
gfx::Canvas* renderTarget = nullptr;
bool renderOk = false;

constexpr int kLabelH = 34;   // label plus the page dots
constexpr int kTopPad = 8;
constexpr int kQuietModules = 2;  // margin required for a scanner to lock on

void renderToCanvas(esp_qrcode_handle_t qr) {
  if (!renderTarget) return;
  gfx::Canvas& canvas = *renderTarget;

  const int modules = esp_qrcode_get_size(qr);
  if (modules <= 0) return;

  // Largest WHOLE-pixel module that fits. Whole pixels matter: a fractional
  // scale aliases the modules unevenly and costs more failed scans than the
  // extra size wins.
  const int available =
      min(canvas.width(), canvas.height() - kLabelH - kTopPad);
  const int scale = available / (modules + 2 * kQuietModules);
  if (scale < 1) return;  // cannot render this legibly; leave renderOk false

  const int codeSize = modules * scale;
  const int quiet = kQuietModules * scale;
  const int originX = (canvas.width() - codeSize) / 2;
  const int originY = kTopPad + quiet;

  // Clear the quiet zone explicitly — the code is unscannable without it.
  canvas.rect(originX - quiet, originY - quiet, codeSize + 2 * quiet,
              codeSize + 2 * quiet, gfx::kWhite);

  for (int y = 0; y < modules; y++) {
    for (int x = 0; x < modules; x++) {
      if (!esp_qrcode_get_module(qr, x, y)) continue;
      canvas.rect(originX + x * scale, originY + y * scale, scale, scale,
                  gfx::kBlack);
    }
  }
  renderOk = true;
}

}  // namespace

void QrCodes::onEnter(ui::Router& router) {
  // Full refresh: a QR is dense high-contrast detail, and ghosting from the
  // previous screen is exactly what stops a scanner locking on.
  router.invalidate(epaper::Refresh::kFull);
}

void QrCodes::draw(gfx::Canvas& canvas) {
  if (settings::linkCount() <= 0) {
    canvas.textInBox(0, 90, canvas.width(), 14, "No links configured",
                     gfx::Font::kBody, gfx::kBlack);
    widgets::buttonMarkers(canvas);
    return;
  }

  const services::settings::Link& link = services::settings::link(cursor_);

  renderTarget = &canvas;
  renderOk = false;
  esp_qrcode_config_t cfg = {};
  cfg.display_func = renderToCanvas;
  // Cap the version so a long URL fails loudly here rather than silently
  // producing modules too fine for a phone to resolve off this panel.
  cfg.max_qrcode_version = 8;
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_MED;

  const esp_err_t err = esp_qrcode_generate(&cfg, link.url.c_str());
  renderTarget = nullptr;

  if (err != ESP_OK || !renderOk) {
    canvas.textInBox(0, 80, canvas.width(), 16, "Link too long",
                     gfx::Font::kLabel, gfx::kBlack);
    canvas.textInBox(0, 104, canvas.width(), 14, link.label.c_str(), gfx::Font::kBody,
                     gfx::kBlack);
    widgets::buttonMarkers(canvas);
    return;
  }

  // Label and a dot per link, so it is obvious more are behind this one.
  const int labelY = canvas.height() - kLabelH + 2;
  canvas.textInBox(0, labelY, canvas.width(), 16, link.label.c_str(),
                   gfx::Font::kLabel, gfx::kBlack);

  if (settings::linkCount() > 1) {
    const int gap = 10;
    const int startX = (canvas.width() - (settings::linkCount() - 1) * gap) / 2;
    const int dotY = labelY + 24;
    for (int i = 0; i < settings::linkCount(); i++) {
      const int x = startX + i * gap;
      if (i == cursor_) {
        canvas.circle(x, dotY, 3, gfx::kBlack);
      } else {
        canvas.circleOutline(x, dotY, 3, 1, gfx::kBlack);
      }
    }
  }

  widgets::buttonMarkers(canvas);
}

bool QrCodes::onEvent(ui::Router& router, input::Button button,
                      input::Event event) {
  if (event != input::Event::kClick) return false;

  const int total = settings::linkCount();
  if (total <= 0) return false;
  cursor_ = (cursor_ + (button == input::Button::kB ? 1 : -1) + total) % total;
  router.invalidate(epaper::Refresh::kFull);
  return true;
}

}  // namespace apps
