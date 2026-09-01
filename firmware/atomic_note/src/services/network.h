// WiFi, in two modes.
//
// JOIN: try each saved network in turn. Used whenever the device needs the
// network for something — syncing time, reaching the companion service later.
//
// PORTAL: raise our own access point with a captive portal. This is how
// credentials get in without a keyboard: join the device's network from a
// phone, and its captive-portal prompt opens the config page automatically.
// No app to install, no credentials compiled into the firmware, and it works
// on a network the device has never seen.
//
// The radio is OFF unless something asked for it. WiFi is the largest single
// power draw on the board, so nothing here starts implicitly.
#pragma once

#include <Arduino.h>

namespace services {
namespace network {

enum class State : uint8_t {
  kOff,
  kJoining,
  kJoined,
  kPortal,   // our own AP is up
  kFailed,   // tried every saved network, none answered
};

void begin();

State state();
bool isJoined();

// Non-blocking. Call from the main loop; it drives connection attempts and
// serves the portal.
void update();

// Try the saved networks. Returns immediately — watch state().
void join();

// Raise the access point and serve the config portal.
void startPortal();

// Drop the radio entirely.
void stop();

String ipAddress();      // whichever mode is active
String connectedSsid();  // empty unless joined
String portalSsid();     // the AP name, when the portal is up

// Sync the clock from NTP. Requires a joined network. Blocking, with a
// timeout, because there is nothing useful to do until it lands.
bool syncTime(uint32_t timeoutMs = 8000);

}  // namespace network
}  // namespace services
