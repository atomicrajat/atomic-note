#include "network.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
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

// mDNS is a multicast round trip, so it is cached rather than repeated on
// every request. Short, because the whole point is to follow an address that
// moves; a minute of staleness costs one failed request at most, and the next
// one re-resolves.
constexpr uint32_t kResolveTtlMs = 60000;
constexpr uint32_t kResolveTimeoutMs = 2000;

bool mdnsStarted = false;
String resolvedHost;
IPAddress resolvedIp;
uint32_t resolvedAtMs = 0;

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

String resolved(const String& url) {
  // Split out the host: everything after "//" up to the port or the path.
  const int schemeEnd = url.indexOf("//");
  const int hostStart = (schemeEnd >= 0) ? schemeEnd + 2 : 0;
  int hostEnd = hostStart;
  while (hostEnd < (int)url.length() && url[hostEnd] != ':' &&
         url[hostEnd] != '/') {
    hostEnd++;
  }
  String host = url.substring(hostStart, hostEnd);
  if (!host.endsWith(".local")) return url;

  const uint32_t now = millis();
  const bool fresh = resolvedAtMs != 0 && host == resolvedHost &&
                     now - resolvedAtMs < kResolveTtlMs;

  if (!fresh) {
    if (!isJoined()) return url;

    // Started lazily. Beginning mDNS also publishes our own name, which is a
    // small bonus and not the reason: the query side needs the responder up.
    if (!mdnsStarted) {
      String self = settings::deviceName();
      self.toLowerCase();
      self.replace(' ', '-');
      mdnsStarted = MDNS.begin(self.c_str());
      if (!mdnsStarted) {
        Serial.println("[mdns] responder failed to start");
      }
    }

    IPAddress found;
    if (mdnsStarted) {
      // queryHost() wants the label without the ".local" suffix.
      found = MDNS.queryHost(host.substring(0, host.length() - 6).c_str(),
                             kResolveTimeoutMs);
    }

    if (found != IPAddress((uint32_t)0)) {
      Serial.printf("[mdns] %s is %s\n", host.c_str(),
                    found.toString().c_str());
      resolvedHost = host;
      resolvedIp = found;
      resolvedAtMs = now;
    } else if (resolvedAtMs == 0 || host != resolvedHost) {
      // Never resolved this name. Hand the URL back unchanged rather than
      // inventing an address.
      Serial.printf("[mdns] no answer for %s\n", host.c_str());
      return url;
    }
    // Otherwise fall through on the stale address. A host that answered a few
    // minutes ago and is quiet right now is far more likely to be busy than
    // to have moved, and trying the old address beats not trying at all.
  }

  return url.substring(0, hostStart) + resolvedIp.toString() +
         url.substring(hostEnd);
}

}  // namespace network
}  // namespace services
