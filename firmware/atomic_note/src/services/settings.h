// Persistent configuration, in NVS.
//
// NVS rather than the SD card, deliberately: the card is optional and
// removable, and WiFi credentials have to survive without one. NVS lives in
// flash and is wear-levelled, so it also survives a card swap.
//
// Everything here is editable from the web app in this phase. Nothing above
// this layer should hardcode a value that a user might reasonably want to
// change.
#pragma once

#include <Arduino.h>

namespace services {
namespace settings {

constexpr int kMaxNetworks = 4;
constexpr int kMaxLinks = 6;
// Eight, not six. The real cost of a tag is not the array slot — it is that
// every tag is one more press to cycle past on a two-button device, so the
// list should stay short for that reason rather than this one. Seven are
// defined; the spare leaves room to add one without a reflash.
constexpr int kMaxTags = 8;

struct Network {
  String ssid;
  String password;
};

struct Link {
  String label;
  String url;
};

void begin();

// ── WiFi ──────────────────────────────────────────────────────────────────
// Several networks so the device works at home and elsewhere without being
// reprovisioned each time. Joining is tried in order.
int networkCount();
const Network& network(int index);
bool addNetwork(const char* ssid, const char* password);
bool removeNetwork(int index);
void clearNetworks();

// ── Time ──────────────────────────────────────────────────────────────────
int utcOffsetMinutes();
void setUtcOffsetMinutes(int minutes);

// ── Links ─────────────────────────────────────────────────────────────────
// Seeded from the compiled-in defaults the first time the device boots, then
// owned by the user.
int linkCount();
const Link& link(int index);
bool setLink(int index, const char* label, const char* url);
bool addLink(const char* label, const char* url);
bool removeLink(int index);

// ── Note tags ─────────────────────────────────────────────────────────────
// Chosen right after recording, so the list is short on purpose: cycling
// through more than a handful with one button is worse than a wrong tag.
//
// The tag is not just a label. The companion picks how to read the transcript
// from it — Buy becomes a checklist, Work becomes people and commitments,
// Expense becomes an amount, a merchant and a payment medium. That is why the
// list is capped: every entry has to earn a distinct reading, and six is
// already as many as two buttons should ask anyone to cycle through.
int tagCount();
String tag(int index);
bool setTag(int index, const char* name);
bool addTag(const char* name);
bool removeTag(int index);

// ── Device ────────────────────────────────────────────────────────────────
String deviceName();
void setDeviceName(const char* name);

// Base URL of the transcription companion, e.g. http://your-mac.local:8710
// When set, notes are transcribed on their own whenever the device already
// happens to have a network — no menu trip needed.
//
// Stored exactly as typed. A ".local" host is resolved at the point of use by
// network::resolved(), so this getter is the value to show in a form and the
// wrong thing to hand to HTTPClient.
bool autoSync();
void setAutoSync(bool on);

String companionUrl();
void setCompanionUrl(const char* url);

int gameBestScore();
void setGameBestScore(int score);

int expenseBudget();
void setExpenseBudget(int amount);

void saveExpenseData(float month, float daily[7]);
bool loadExpenseData(float& month, float daily[7]);

bool soundEnabled();
void setSoundEnabled(bool on);

// Vibration cues, on the motor wired to GPIO 3. A SEPARATE preference from
// sound rather than a fallback for it: the useful combination is often sound
// off and buzz on — in a meeting, or in a pocket — and tying one to the other
// would make that unreachable. Defaults on; with no motor attached it drives
// a pin nobody is listening to, which costs nothing.
bool hapticsEnabled();
void setHapticsEnabled(bool on);

// Speaker volume, 0-100, applied to the codec at boot and whenever changed.
//
// One volume for everything the speaker does — spoken answers, note playback
// and the UI cues — because they all come out of the same DAC and a device
// with three separate loudness settings is a device nobody adjusts.
//
// Defaults to 85 rather than the codec's own 70: a spoken answer at 70 is
// audible on a desk and not much more, and the thing you most want to hear
// from across a room is the one thing the screen cannot show you.
int volume();
void setVolume(int percent);

// Read answers out loud on the Ask screen. Off by default: the speaker is
// loud, the device is small enough to be somewhere it should not be, and a
// setting that surprises you the first time is a bad default.
bool speakAnswers();
void setSpeakAnswers(bool on);

// A battery drain measurement is in progress. Lives in NVS because the test
// spans deep sleeps, which reset the chip.
bool batteryTest();
void setBatteryTest(bool on);

// Persist everything. Called automatically by the mutators.
void save();

}  // namespace settings
}  // namespace services
