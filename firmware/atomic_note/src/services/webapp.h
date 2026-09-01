// The configuration web app.
//
// Served both from the setup portal and from the LAN once joined, so the same
// pages do first-time provisioning and everyday editing.
//
// This is the device's keyboard. Everything that needs text — WiFi passwords,
// task text, link URLs, the device name — is entered here, because two buttons
// cannot do it and never will.
#pragma once

namespace services {
namespace webapp {

void begin();
void stop();

// Serves pending requests. Cheap when idle; call from the main loop.
void update();

bool running();

}  // namespace webapp
}  // namespace services
