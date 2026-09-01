#include "settings.h"

#include <Preferences.h>

#include "../links.h"

namespace services {
namespace settings {
namespace {

constexpr const char* kNamespace = "atomic";

Preferences prefs;

Network networks[kMaxNetworks];
int networkTotal = 0;

Link links_[kMaxLinks];
int linkTotal = 0;

String tags[kMaxTags];
int tagTotal = 0;

int utcOffset = 330;  // IST until told otherwise
String deviceNameValue = "Atomic Note";
bool soundOn = true;
bool hapticsOn = true;
int volumePercent = 85;
bool speakOn = false;
bool battTest = false;
int bestScore = 0;
String companion = "";
bool autoSyncOn = true;
int expenseBudgetVal = 10000;

// NVS keys are capped at 15 characters, so indexed keys are built short.
String keyFor(const char* prefix, int index) {
  return String(prefix) + String(index);
}

void loadNetworks() {
  networkTotal = prefs.getInt("netCount", 0);
  if (networkTotal > kMaxNetworks) networkTotal = kMaxNetworks;
  for (int i = 0; i < networkTotal; i++) {
    networks[i].ssid = prefs.getString(keyFor("ns", i).c_str(), "");
    networks[i].password = prefs.getString(keyFor("np", i).c_str(), "");
  }
}

void loadLinks() {
  linkTotal = prefs.getInt("lnkCount", -1);

  if (linkTotal < 0) {
    // First boot: seed from the compiled-in defaults so the QR screen is
    // useful before anyone opens the web app.
    linkTotal = min(links::kLinkCount, kMaxLinks);
    for (int i = 0; i < linkTotal; i++) {
      links_[i].label = links::kLinks[i].label;
      links_[i].url = links::kLinks[i].url;
    }
    return;
  }

  if (linkTotal > kMaxLinks) linkTotal = kMaxLinks;
  for (int i = 0; i < linkTotal; i++) {
    links_[i].label = prefs.getString(keyFor("ll", i).c_str(), "");
    links_[i].url = prefs.getString(keyFor("lu", i).c_str(), "");
  }
}

// Bumped when the default tag set gains an entry that existing devices should
// also get. Without a version, a new default is invisible to every device that
// has already booted once — their tag list came out of NVS and nothing ever
// revisits it.
constexpr int kTagSchemaVersion = 2;

void loadTags() {
  tagTotal = prefs.getInt("tagCount", -1);
  if (tagTotal < 0) {
    // First boot: a small default set covering what a voice note usually is.
    //
    // Buy and Expense are both about money and are deliberately separate.
    // Buy is intent — something wanted, in the future, with no amount. Expense
    // is a record — money already gone, with a number attached. The companion
    // treats them completely differently, and the distinction has to be made
    // at the moment of tagging because nothing downstream can recover it.
    const char* defaults[] = {"Note", "Idea", "Task", "Buy", "Work",
                              "Expense", "Books"};
    tagTotal = (int)(sizeof(defaults) / sizeof(defaults[0]));
    if (tagTotal > kMaxTags) tagTotal = kMaxTags;
    for (int i = 0; i < tagTotal; i++) tags[i] = defaults[i];
    prefs.putInt("tagver", kTagSchemaVersion);
    return;
  }
  if (tagTotal > kMaxTags) tagTotal = kMaxTags;
  for (int i = 0; i < tagTotal; i++) {
    tags[i] = prefs.getString(keyFor("tg", i).c_str(), "");
  }

  // One-time upgrade for a device provisioned before a default tag existed.
  //
  // Guarded by a stored version rather than by "is this tag missing", so
  // someone who deliberately deletes one does not have it reinstated on every
  // boot. Each name is added at most once, and only if there is room.
  if (prefs.getInt("tagver", 0) < kTagSchemaVersion) {
    const char* added[] = {"Expense", "Books"};
    for (const char* name : added) {
      bool present = false;
      for (int i = 0; i < tagTotal; i++) {
        if (tags[i] == name) present = true;
      }
      if (present || tagTotal >= kMaxTags) continue;
      tags[tagTotal++] = name;
      prefs.putInt("tagCount", tagTotal);
      prefs.putString(keyFor("tg", tagTotal - 1).c_str(), tags[tagTotal - 1]);
      Serial.printf("[settings] added the %s tag\n", name);
    }
    prefs.putInt("tagver", kTagSchemaVersion);
  }
}

}  // namespace

void begin() {
  if (!prefs.begin(kNamespace, false)) {
    Serial.println("[settings] NVS unavailable — using defaults");
    loadLinks();  // still seeds from the compiled-in list
    loadTags();
    return;
  }
  loadNetworks();
  loadLinks();
  loadTags();
  utcOffset = prefs.getInt("tz", 330);
  deviceNameValue = prefs.getString("name", "Atomic Note");
  soundOn = prefs.getBool("sound", true);
  hapticsOn = prefs.getBool("haptics", true);
  volumePercent = prefs.getInt("volume", 85);
  speakOn = prefs.getBool("speak", false);
  battTest = prefs.getBool("batttest", false);
  bestScore = prefs.getInt("best", 0);
  companion = prefs.getString("companion", "");
  if (companion.length() == 0) {
    companion = "http://192.168.1.61:8710";
  }
  autoSyncOn = prefs.getBool("autosync", true);
  expenseBudgetVal = prefs.getInt("budget", 10000);

  Serial.printf("[settings] %d network(s), %d link(s), utc%+d, companion: %s\n",
                networkTotal, linkTotal, utcOffset, companion.c_str());
}

void save() {
  prefs.putInt("netCount", networkTotal);
  for (int i = 0; i < networkTotal; i++) {
    prefs.putString(keyFor("ns", i).c_str(), networks[i].ssid);
    prefs.putString(keyFor("np", i).c_str(), networks[i].password);
  }

  prefs.putInt("lnkCount", linkTotal);
  for (int i = 0; i < linkTotal; i++) {
    prefs.putString(keyFor("ll", i).c_str(), links_[i].label);
    prefs.putString(keyFor("lu", i).c_str(), links_[i].url);
  }

  prefs.putInt("tagCount", tagTotal);
  for (int i = 0; i < tagTotal; i++) {
    prefs.putString(keyFor("tg", i).c_str(), tags[i]);
  }

  prefs.putInt("tz", utcOffset);
  prefs.putString("name", deviceNameValue);
  prefs.putBool("sound", soundOn);
  prefs.putBool("haptics", hapticsOn);
  prefs.putInt("volume", volumePercent);
  prefs.putBool("speak", speakOn);
  prefs.putBool("batttest", battTest);
  prefs.putInt("best", bestScore);
  prefs.putString("companion", companion);
  prefs.putBool("autosync", autoSyncOn);
  prefs.putInt("budget", expenseBudgetVal);
}

// ── WiFi ──────────────────────────────────────────────────────────────────

int networkCount() { return networkTotal; }

const Network& network(int index) {
  static Network empty;
  if (index < 0 || index >= networkTotal) return empty;
  return networks[index];
}

bool addNetwork(const char* ssid, const char* password) {
  if (!ssid || !*ssid) return false;

  // Re-provisioning a known network updates it rather than duplicating it,
  // which is what happens when someone changes their WiFi password.
  for (int i = 0; i < networkTotal; i++) {
    if (networks[i].ssid == ssid) {
      networks[i].password = password ? password : "";
      save();
      return true;
    }
  }

  if (networkTotal >= kMaxNetworks) {
    // Full: drop the oldest, since the newest is the one in front of the user.
    for (int i = 0; i < kMaxNetworks - 1; i++) networks[i] = networks[i + 1];
    networkTotal = kMaxNetworks - 1;
  }
  networks[networkTotal].ssid = ssid;
  networks[networkTotal].password = password ? password : "";
  networkTotal++;
  save();
  return true;
}

bool removeNetwork(int index) {
  if (index < 0 || index >= networkTotal) return false;
  for (int i = index; i < networkTotal - 1; i++) networks[i] = networks[i + 1];
  networkTotal--;
  save();
  return true;
}

void clearNetworks() {
  networkTotal = 0;
  save();
}

// ── Time ──────────────────────────────────────────────────────────────────

int utcOffsetMinutes() { return utcOffset; }

void setUtcOffsetMinutes(int minutes) {
  // Real offsets run from -12:00 to +14:00.
  if (minutes < -720 || minutes > 840) return;
  utcOffset = minutes;
  save();
}

// ── Links ─────────────────────────────────────────────────────────────────

int linkCount() { return linkTotal; }

const Link& link(int index) {
  static Link empty;
  if (index < 0 || index >= linkTotal) return empty;
  return links_[index];
}

bool setLink(int index, const char* label, const char* url) {
  if (index < 0 || index >= linkTotal) return false;
  links_[index].label = label ? label : "";
  links_[index].url = url ? url : "";
  save();
  return true;
}

bool addLink(const char* label, const char* url) {
  if (linkTotal >= kMaxLinks) return false;
  if (!url || !*url) return false;
  links_[linkTotal].label = (label && *label) ? label : "Link";
  links_[linkTotal].url = url;
  linkTotal++;
  save();
  return true;
}

bool removeLink(int index) {
  if (index < 0 || index >= linkTotal) return false;
  for (int i = index; i < linkTotal - 1; i++) links_[i] = links_[i + 1];
  linkTotal--;
  save();
  return true;
}

// ── Note tags ─────────────────────────────────────────────────────────────

int tagCount() { return tagTotal; }

String tag(int index) {
  if (index < 0 || index >= tagTotal) return String("Note");
  return tags[index];
}

bool setTag(int index, const char* name) {
  if (index < 0 || index >= tagTotal) return false;
  if (!name || !*name) return false;
  tags[index] = name;
  save();
  return true;
}

bool addTag(const char* name) {
  if (tagTotal >= kMaxTags) return false;
  if (!name || !*name) return false;
  tags[tagTotal++] = name;
  save();
  return true;
}

bool removeTag(int index) {
  if (index < 0 || index >= tagTotal) return false;
  for (int i = index; i < tagTotal - 1; i++) tags[i] = tags[i + 1];
  tagTotal--;
  save();
  return true;
}

// ── Device ────────────────────────────────────────────────────────────────

String deviceName() { return deviceNameValue; }

void setDeviceName(const char* name) {
  if (!name || !*name) return;
  deviceNameValue = name;
  save();
}

bool autoSync() { return autoSyncOn; }

void setAutoSync(bool on) {
  autoSyncOn = on;
  save();
}

String companionUrl() { return companion; }

void setCompanionUrl(const char* url) {
  companion = url ? url : "";
  companion.trim();
  save();
}

int gameBestScore() { return bestScore; }

int expenseBudget() { return expenseBudgetVal; }
void setExpenseBudget(int amount) {
  expenseBudgetVal = amount;
  save();
}

void saveExpenseData(float month, float daily[7]) {
  prefs.putFloat("exp_m", month);
  for (int i = 0; i < 7; i++) {
    prefs.putFloat(keyFor("exp_d", i).c_str(), daily[i]);
  }
  prefs.putBool("exp_v", true);
}

bool loadExpenseData(float& month, float daily[7]) {
  if (!prefs.getBool("exp_v", false)) return false;
  month = prefs.getFloat("exp_m", 0.0f);
  for (int i = 0; i < 7; i++) {
    daily[i] = prefs.getFloat(keyFor("exp_d", i).c_str(), 0.0f);
  }
  return true;
}

void setGameBestScore(int score) {
  if (score <= bestScore) return;
  bestScore = score;
  save();
}

bool soundEnabled() { return soundOn; }

void setSoundEnabled(bool on) {
  soundOn = on;
  save();
}

bool hapticsEnabled() { return hapticsOn; }

int volume() { return volumePercent; }

void setVolume(int percent) {
  volumePercent = constrain(percent, 0, 100);
  save();
}

void setHapticsEnabled(bool on) {
  hapticsOn = on;
  save();
}

bool batteryTest() { return battTest; }

void setBatteryTest(bool on) {
  battTest = on;
  save();
}

bool speakAnswers() { return speakOn; }

void setSpeakAnswers(bool on) {
  speakOn = on;
  save();
}

}  // namespace settings
}  // namespace services
