// Atomic Note — ESP32-S3 + 1.54" e-paper personal device.
//
// setup() brings the board up in a fixed order that matters:
//   1. latch battery power    — the board cuts its own power without this
//   2. read the wake reason   — before the user releases the button
//   3. rails, then peripherals
//   4. hand control to the Router
//
// loop() is deliberately empty of logic. Everything lives in a Screen.

#include "src/apps/agenda_screen.h"
#include "src/apps/ask_screen.h"
#include "src/apps/calendar.h"
#include "src/apps/dashboard.h"
#include "src/apps/dice.h"
#include "src/apps/measure.h"
#include "src/apps/help.h"
#include "src/apps/network_screen.h"
#include "src/apps/note_browser.h"
#include "src/apps/record_screen.h"
#include "src/apps/pomodoro.h"
#include "src/apps/qr_codes.h"
#include "src/apps/sync_screen.h"
#include "src/apps/settings_screen.h"
#include "src/apps/usage_screen.h"
#include "src/apps/expense_screen.h"
#include "src/apps/reminder_alert.h"
#include "src/apps/status_banner.h"
#include "src/apps/task_list.h"
#include "src/audio/recorder.h"
#include "src/audio/es8311.h"
#include "src/audio/sound.h"
#include "src/board/haptics.h"
#include "src/board/pins.h"
#include "src/config.h"
#include "src/board/power.h"
#include "src/display/canvas.h"
#include "src/display/epaper.h"
#include "src/input/buttons.h"
#include "src/services/console.h"
#include "src/services/environment.h"
#include "src/services/i2c_bus.h"
#include "src/services/battery_log.h"
#include "src/services/reminders.h"
#include "src/services/rtc.h"
#include "src/services/network.h"
#include "src/services/notes.h"
#include "src/services/motion.h"
#include "src/services/range.h"
#include "src/services/sensors.h"
#include "src/services/settings.h"
#include "src/services/transcribe.h"
#include "src/services/storage.h"
#include "src/services/tasks.h"
#include "src/services/sleep.h"
#include "src/ui/menu.h"
#include "src/ui/router.h"

constexpr const char* kFirmwareVersion = "0.5.0-phase5";

namespace {

gfx::Canvas canvas(200, 200);
ui::Router router(canvas);

// One instance per feature, all statically allocated. The menu table below is
// the only place a new app has to be registered.
apps::Dashboard dashboardScreen;
apps::Pomodoro pomodoroScreen;
apps::StatusBanner statusBannerScreen;
apps::QrCodes qrCodesScreen;
apps::Help helpScreen;
apps::TaskList taskListScreen;
apps::Calendar calendarScreen;
apps::AgendaScreen agendaScreen;
apps::AskScreen askScreen;
apps::NetworkScreen networkScreen;
apps::ReminderAlert reminderAlertScreen;
apps::RecordScreen recordScreen;
apps::NoteTags noteTagsScreen;
apps::NoteList noteListScreen;
apps::NoteDetail noteDetailScreen;
apps::SyncScreen syncScreen;
apps::UsageScreen usageScreen;
apps::ExpenseScreen expenseScreen;
apps::SettingsScreen settingsScreen;
apps::Dice diceScreen;
apps::Measure measureScreen;

// ── Apps ──────────────────────────────────────────────────────────────────
// Features that depend on something being plugged in. They live in their own
// list rather than the main menu because the main menu is what the device
// always does, and this is what it can do TODAY given what is attached.
//
// A row is struck through when its hardware is absent rather than hidden: a
// menu that silently changes length is confusing, and one where a row comes
// alive when you plug a sensor in explains itself.
bool haveMotion() {
  return services::sensors::have(services::sensors::Kind::kMotion);
}

bool haveRange() {
  return services::sensors::have(services::sensors::Kind::kDistance);
}

const ui::MenuEntry kAppEntries[] = {
    {"Dice", &diceScreen, &haveMotion},
    {"Measure", &measureScreen, &haveRange},
};
ui::Menu appsScreen("Apps", kAppEntries,
                    sizeof(kAppEntries) / sizeof(kAppEntries[0]));

// Ordered by how often it is actually reached, not by when it was built.
//
// Every row costs a press to pass, and a tap on the dashboard lands on the
// first one — so the order IS the interface on a two-button device. Notes and
// Sync lead because capture is what this thing is for and reviewing what you
// said is what follows it. Ask and Agenda are the next most asked. Everything
// below is occasional, and Buttons last because you need it once.
//
// A null screen renders the row struck through — a visible placeholder for
// what is coming, rather than a row that silently does nothing.
const ui::MenuEntry kMenuEntries[] = {
    {"Notes", &noteTagsScreen},
    {"Sync", &syncScreen},
    {"Agenda", &agendaScreen},
    {"Ask", &askScreen},
    {"Apps", &appsScreen},
    {"Focus Timer", &pomodoroScreen},
    {"Status Sign", &statusBannerScreen},
    {"My Links", &qrCodesScreen},
    {"Tasks", &taskListScreen},
    {"Calendar", &calendarScreen},
    {"Expenses", &expenseScreen},
    {"Claude Usage", &usageScreen},
    {"WiFi", &networkScreen},
    {"Settings", &settingsScreen},
    {"Buttons", &helpScreen},
};
ui::Menu menuScreen("Menu", kMenuEntries,
                    sizeof(kMenuEntries) / sizeof(kMenuEntries[0]));

// Every screen the console can open by name, for `screen <name>` and the
// screenshot tool. Separate from the menu tables because it also has to reach
// screens the menu never shows — the dashboard is the root, and Record is only
// reachable by holding a button.
struct NamedScreen {
  const char* name;
  ui::Screen* screen;
};

const NamedScreen kNamedScreens[] = {
    {"dashboard", &dashboardScreen}, {"menu", &menuScreen},
    {"notes", &noteTagsScreen},      {"sync", &syncScreen},
    {"agenda", &agendaScreen},       {"ask", &askScreen},
    {"apps", &appsScreen},           {"dice", &diceScreen},
    {"measure", &measureScreen},     {"timer", &pomodoroScreen},
    {"banner", &statusBannerScreen}, {"links", &qrCodesScreen},
    {"tasks", &taskListScreen},      {"calendar", &calendarScreen},
    {"expenses", &expenseScreen},    {"usage", &usageScreen},
    {"wifi", &networkScreen},        {"settings", &settingsScreen},
    {"buttons", &helpScreen},        {"record", &recordScreen},
};

bool openScreenByName(const char* name) {
  if (name == nullptr) {
    Serial.print("screens:");
    for (const NamedScreen& entry : kNamedScreens) {
      Serial.printf(" %s", entry.name);
    }
    Serial.println();
    return true;
  }
  for (const NamedScreen& entry : kNamedScreens) {
    if (strcmp(entry.name, name) == 0) {
      router.go(entry.screen);
      Serial.printf("opened %s\n", entry.name);
      return true;
    }
  }
  return false;
}

// Draw a bare message straight to the panel, for failures that happen before
// the Router exists.
void fatal(const char* message) {
  Serial.printf("[fatal] %s\n", message);
  canvas.fill(gfx::kWhite);
  canvas.textAligned(0, 90, canvas.width(), message, gfx::Font::kLabel,
                     gfx::kBlack, gfx::Align::kCenter);
  epaper::present(canvas, epaper::Refresh::kFull);
  while (true) delay(1000);
}

}  // namespace

void setup() {
  // 1. Hold our own power on. Nothing else may come first.
  power::latch();

  Serial.begin(115200);
  // Native USB CDC BLOCKS on write when a host has the port open but is not
  // reading it — a terminal left connected and idle is enough. That stalls the
  // main loop, which looks like dead buttons and a frozen timer. A zero TX
  // timeout makes logging drop instead of wait: diagnostics must never be able
  // to hang the device.
  Serial.setTxTimeoutMs(0);
  delay(200);
  Serial.printf("\n=== Atomic Note %s ===\n", kFirmwareVersion);

  // 2. Latch the wake reason while the button is likely still held.
  const services::WakeReason wake = services::wakeReason();
  Serial.printf("[boot] wake reason: %d\n", (int)wake);

  // 3. Rails up, then peripherals.
  //
  // GPIO 42 is labelled the "audio" rail but actually feeds every peripheral
  // on that supply — including the PCF85063 RTC. Verified by I2C scan: 0x51
  // only answers once this rail is up. So it must be powered before I2C init,
  // even though audio itself is not used until phase 4.
  power::epdRail(true);
  power::audioRail(true);
  delay(150);  // let the rail settle before probing the bus

  input::begin();

  // Sensors and storage come up BEFORE the panel, so a scheduled wake can
  // decide whether it has anything to show without paying for a refresh.
  services::i2c::begin();
  services::rtc::begin();
  services::settings::begin();
  services::storage::begin();

  if (services::rtc::syncSystemFromChip()) {
    Serial.println("[boot] system clock set from RTC");
  } else {
    Serial.println("[boot] RTC not set — use: time <unix epoch>");
  }

  services::reminders::begin();

  // ── Scheduled-wake fast path ────────────────────────────────────────────
  // Reminders are reached by hopping: sleep ten minutes, wake, check, sleep
  // again. Most of those wakes have nothing to do, and a panel refresh costs
  // ~500 ms of blocking and a burst of current. So a timer wake with nothing
  // due returns to sleep here, before the display is ever powered.
  const int dueIndex = services::reminders::dueNow();
  if (wake == services::WakeReason::kTimer && dueIndex < 0) {
    // A drain measurement rides on these same hops: take the reading here,
    // before the panel or the radio have cost anything, then go back down.
    if (services::battery_log::running()) {
      services::battery_log::sample();
    }
    Serial.println("[boot] scheduled wake, nothing due — back to sleep");
    services::enterDeepSleep();
  }

  // Work that must continue while the panel blocks for ~500 ms:
  //   - button sampling, or a press made during a repaint is lost
  //   - audio capture, or the DMA overruns and the recording has a gap
  epaper::setBusyHook([]() {
    input::sample();
    audio::recorder::pumpIfActive();
  });

  if (!canvas.begin()) {
    Serial.println("[fatal] canvas allocation failed");
    while (true) delay(1000);
  }
  if (!epaper::begin()) fatal("PANEL FAIL");

  services::environment::begin();
  services::tasks::begin();
  services::notes::begin();
  services::network::begin();

  // Audio needs the peripheral rail, which is already up for the RTC.
  // Look for optional parts on the I2C bus once, at boot. The Apps menu asks
  // what is attached every time it draws, so this has to have run before the
  // first menu is shown — and probing a handful of addresses is fast enough
  // that there is no reason to defer it.
  services::sensors::detect();
  services::motion::begin();
  services::range::begin();
  Serial.printf("[boot] %d sensor(s) attached\n",
                services::sensors::addonCount());

  // The saved volume, applied once the codec is up. Everything the speaker
  // does goes through this one setting.
  audio::es8311::setVolume(services::settings::volume());

  audio::sound::begin();

  // The motor on GPIO 3. Claims the pin and drives it low; harmless with
  // nothing attached, so there is no detection and nothing to configure.
  haptics::begin();

  Serial.printf("[boot] free heap %u, psram %u\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());

  // Console commands that change what is on screen need a way to force a
  // repaint; the Router owns that decision, so hand it a hook.
  services::console::setRedrawHook(
      []() { router.invalidate(epaper::Refresh::kFull); });
  services::console::setCanvas(&canvas);
  services::console::setScreenHook(&openScreenByName);

  dashboardScreen.setMenu(&menuScreen);
  dashboardScreen.setRecorder(&recordScreen);
  noteTagsScreen.setList(&noteListScreen);
  noteListScreen.setDetail(&noteDetailScreen);

  // 4. Hand off. A due reminder takes precedence over the dashboard — it is
  // the reason the device woke.
  if (dueIndex >= 0) {
    reminderAlertScreen.setIndex(dueIndex);
    router.begin(&reminderAlertScreen);
  } else {
    router.begin(&dashboardScreen);
  }
  Serial.println("[boot] ready");
}

// Transcribe quietly whenever the device already has a network for some other
// reason. No connecting on its own — that would spend battery behind the
// user's back. Forced sync (the menu's Sync entry) is what deliberately brings
// the radio up.
void autoSyncStep() {
  if (!services::settings::autoSync()) return;
  if (!services::network::isJoined()) return;
  if (services::notes::countWithoutText() == 0) return;

  // Never while something is using the screen or the codec.
  ui::Screen* screen = router.current();
  if (screen && screen->blocksSleep()) return;

  static uint32_t nextAttemptMs = 0;
  if (millis() < nextAttemptMs) return;

  const int index = services::notes::indexOfUntranscribed(0);
  if (index < 0) return;

  String error;
  if (!services::transcribe::one(index, error)) {
    Serial.printf("[autosync] %s\n", error.c_str());
    // Back off rather than hammering an unreachable service.
    nextAttemptMs = millis() + 60000;
    return;
  }
  Serial.println("[autosync] transcribed one note");
  nextAttemptMs = millis() + 500;
  router.invalidate();
}

void loop() {
  // Bring-up heartbeat: proves the main loop is still running even when the
  // USB CDC has dropped, which distinguishes a hung device from a dead link.
  static uint32_t lastBeatMs = 0;
  if (config::kDevVerbose && millis() - lastBeatMs >= 5000) {
    lastBeatMs = millis();
    Serial.printf("[beat] up=%lus heap=%u\n",
                  (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap());
  }

  services::console::poll();
  services::network::update();
  autoSyncStep();
  router.update();
  delay(5);  // the router is non-blocking; this just yields to IDLE tasks
}
