#include "network_screen.h"

#include <Arduino.h>

#include "../display/epaper.h"
#include "../services/network.h"
#include "../services/settings.h"
#include "../ui/router.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"

namespace apps {

namespace widgets = ui::widgets;
namespace net = services::network;
namespace settings = services::settings;

namespace {

constexpr int kPad = 13;

}  // namespace

void NetworkScreen::onEnter(ui::Router& router) {
  shownState_ = -1;
  syncedThisVisit_ = false;
  router.invalidate(epaper::Refresh::kFull);
}

void NetworkScreen::onExit() {
  // The radio deliberately SURVIVES leaving this screen. Connecting is an
  // explicit act, and a link that silently dropped the moment you navigated
  // away would make the dashboard's WiFi indicator meaningless.
  //
  // It is turned off by: tapping A here while connected, or the device
  // sleeping. Deep sleep is the common case, so the radio cannot be left on
  // for long by accident.
}

ui::Screen::Idle NetworkScreen::idlePolicy() const {
  const net::State s = net::state();
  const bool busy = s == net::State::kJoining || s == net::State::kPortal ||
                    s == net::State::kJoined;
  return busy ? Idle::kStay : Idle::kReturn;
}

void NetworkScreen::onTick(ui::Router& router) {
  const int now = (int)net::state();

  // The moment a link comes up, set the clock. This is the only reason most
  // sessions need the network at all.
  if (net::isJoined() && !syncedThisVisit_) {
    syncedThisVisit_ = true;
    net::syncTime();
    router.invalidate(epaper::Refresh::kFull);
    return;
  }

  if (now != shownState_) {
    shownState_ = now;
    router.invalidate(epaper::Refresh::kFull);
  }
}

void NetworkScreen::draw(gfx::Canvas& canvas) {
  const int left = kPad;
  const int right = canvas.width() - kPad - 6;
  const int width = right - left;

  widgets::hudFrame(canvas, 3, 3, canvas.width() - 6, canvas.height() - 6, 15,
                    2);

  canvas.text(left, 12, "WiFi", gfx::Font::kLabel, gfx::kBlack);
  const int titleW = canvas.textWidth("WiFi", gfx::Font::kLabel);
  if (right > left + titleW + 8) {
    canvas.rect(left + titleW + 8, 18, right - left - titleW - 8, 4,
                gfx::kBlack);
  }

  const net::State s = net::state();
  shownState_ = (int)s;

  const char* headline = "Off";
  const char* detail = "";
  String line2;
  String line3;

  switch (s) {
    case net::State::kJoining:
      headline = "Connecting";
      line2 = "Trying saved networks";
      break;

    case net::State::kJoined:
      headline = "Connected";
      line2 = net::connectedSsid();
      line3 = net::ipAddress();
      break;

    case net::State::kPortal:
      headline = "Setup mode";
      line2 = String("Join: ") + net::portalSsid();
      line3 = net::ipAddress();
      break;

    case net::State::kFailed:
      headline = "No network";
      line2 = settings::networkCount() > 0 ? "None reachable"
                                           : "Nothing saved yet";
      break;

    default:
      headline = "Radio off";
      line2 = String(settings::networkCount()) + " saved";
      break;
  }
  (void)detail;

  widgets::iconWifi(canvas, canvas.width() / 2 - 6, 44,
                    s == net::State::kJoined ? gfx::kBlack : gfx::kBlack);

  canvas.textInBox(left, 66, width, 20, headline, gfx::Font::kTitle,
                   gfx::kBlack);
  if (line2.length()) {
    canvas.textElided(left, 96, width, line2.c_str(), gfx::Font::kBody,
                      gfx::kBlack);
  }
  if (line3.length()) {
    // The address is what someone types into a browser, so it gets weight.
    canvas.textInBox(left, 114, width, 18, line3.c_str(), gfx::Font::kLabel,
                     gfx::kBlack);
  }

  if (s == net::State::kPortal) {
    canvas.textInBox(left, 142, width, 14,
                     "Open the page your phone offers", gfx::Font::kMicro,
                     gfx::kBlack, gfx::Align::kCenter, 2);
  } else if (s == net::State::kJoined) {
    canvas.textInBox(left, 142, width, 14, "HOLD A DISCONNECT", gfx::Font::kMicro,
                     gfx::kBlack, gfx::Align::kCenter, 2);
  } else if (s != net::State::kJoining) {
    canvas.textInBox(left, 142, width, 14, "HOLD A CONNECT   TAP SETUP",
                     gfx::Font::kMicro, gfx::kBlack, gfx::Align::kCenter, 2);
  }

  widgets::buttonMarkers(canvas);
}

bool NetworkScreen::onEvent(ui::Router& router, input::Button button,
                            input::Event event) {
  // A tap opens the setup portal — the secondary action, and the one you want
  // to be able to reach without committing to anything.
  if (event == input::Event::kClick) {
    net::startPortal();
    router.invalidate(epaper::Refresh::kFull);
    return true;
  }

  if (button == input::Button::kA && event == input::Event::kLongPress) {
    // Connected already: select disconnects. Same gesture, opposite of the
    // current state — the only sensible reading when there is one action.
    if (net::isJoined() || net::state() == net::State::kPortal) {
      net::stop();
      router.invalidate(epaper::Refresh::kFull);
      return true;
    }
    // Connect if there is anything to connect to; otherwise there is no honest
    // "connect" action, so fall through to setup rather than failing at them.
    if (settings::networkCount() > 0) {
      syncedThisVisit_ = false;
      net::join();
    } else {
      net::startPortal();
    }
    router.invalidate(epaper::Refresh::kFull);
    return true;
  }

  return false;  // hold B falls through to the Router as back
}

}  // namespace apps
