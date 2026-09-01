#include "network.h"

#include <DNSServer.h>
#include <WiFi.h>

#include "rtc.h"
#include "settings.h"
#include "webapp.h"

namespace services {
namespace network {
namespace {

// Long enough for a slow router to answer, short enough that walking through
// four saved networks does not feel like a hang.
constexpr uint32_t kJoinTimeoutMs = 9000;

constexpr uint8_t kPortalChannel = 1;
constexpr byte kDnsPort = 53;

State current = State::kOff;
int attemptIndex = 0;
uint32_t attemptStartedMs = 0;
DNSServer dns;
bool dnsRunning = false;

void beginAttempt(int index) {
  const settings::Network& net = settings::network(index);
  Serial.printf("[wifi] joining \"%s\"\n", net.ssid.c_str());
  WiFi.begin(net.ssid.c_str(), net.password.c_str());
  attemptStartedMs = millis();
}

}  // namespace

void begin() {
  WiFi.persistent(false);  // we own the credentials, not the SDK
  WiFi.mode(WIFI_OFF);
  current = State::kOff;
}

State state() { return current; }

bool isJoined() { return current == State::kJoined; }

void join() {
  if (settings::networkCount() == 0) {
    Serial.println("[wifi] no saved networks — use the portal");
    current = State::kFailed;
    return;
  }
  WiFi.mode(WIFI_STA);
  attemptIndex = 0;
  current = State::kJoining;
  beginAttempt(attemptIndex);
}

void startPortal() {
  WiFi.mode(WIFI_AP);

  const String ssid = settings::deviceName();
  // Open network on purpose: a password on the setup AP is one more thing to
  // communicate, and the portal only ever accepts configuration — it exposes
  // nothing that is not already on the device.
  WiFi.softAP(ssid.c_str());
  delay(300);

  // Catch every DNS lookup and answer with our own address. That is what makes
  // phones pop the "sign in to network" sheet instead of making the user type
  // an IP address.
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dnsRunning = dns.start(kDnsPort, "*", WiFi.softAPIP());

  webapp::begin();
  current = State::kPortal;
  Serial.printf("[wifi] portal up: \"%s\" at %s\n", ssid.c_str(),
                WiFi.softAPIP().toString().c_str());
}

void stop() {
  if (dnsRunning) {
    dns.stop();
    dnsRunning = false;
  }
  webapp::stop();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  current = State::kOff;
}

void update() {
  switch (current) {
    case State::kJoining: {
      if (WiFi.status() == WL_CONNECTED) {
        current = State::kJoined;
        Serial.printf("[wifi] joined %s as %s\n", WiFi.SSID().c_str(),
                      WiFi.localIP().toString().c_str());
        webapp::begin();  // the config app is reachable on the LAN too
        return;
      }
      if (millis() - attemptStartedMs < kJoinTimeoutMs) return;

      // This one did not answer; move to the next saved network.
      attemptIndex++;
      if (attemptIndex >= settings::networkCount()) {
        Serial.println("[wifi] no saved network reachable");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        current = State::kFailed;
        return;
      }
      beginAttempt(attemptIndex);
      return;
    }

    case State::kJoined:
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[wifi] link dropped");
        current = State::kFailed;
        return;
      }
      webapp::update();
      return;

    case State::kPortal:
      if (dnsRunning) dns.processNextRequest();
      webapp::update();
      return;

    default:
      return;
  }
}

String ipAddress() {
  if (current == State::kPortal) return WiFi.softAPIP().toString();
  if (current == State::kJoined) return WiFi.localIP().toString();
  return "";
}

String connectedSsid() {
  return current == State::kJoined ? WiFi.SSID() : String("");
}

String portalSsid() {
  return current == State::kPortal ? settings::deviceName() : String("");
}

bool syncTime(uint32_t timeoutMs) {
  if (current != State::kJoined) return false;

  // Ask for UTC and keep the offset in our own settings. Letting the C library
  // hold a timezone would put two sources of truth in play, and the RTC stores
  // UTC regardless.
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");

  const uint32_t deadline = millis() + timeoutMs;
  while (millis() < deadline) {
    const time_t now = time(nullptr);
    if (now > 1700000000) {
      rtc::setSystemUtc(now);  // mirrors into the RTC chip
      Serial.println("[wifi] clock synced from NTP");
      return true;
    }
    delay(120);
  }
  Serial.println("[wifi] NTP timed out");
  return false;
}

}  // namespace network
}  // namespace services
