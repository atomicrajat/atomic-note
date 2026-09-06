#include "console.h"

#include "../config.h"

#include <Arduino.h>
#include <SD_MMC.h>

#include "../board/haptics.h"
#include "motion.h"
#include "range.h"
#include "../board/pins.h"
#include "../board/power.h"
#include "environment.h"
#include "i2c_bus.h"
#include "rtc.h"
#include "sleep.h"
#include "../display/epaper.h"
#include "../links.h"
#include <HTTPClient.h>

#include "battery_log.h"
#include "network.h"
#include "../audio/es8311.h"
#include "../audio/player.h"
#include "../audio/recorder.h"
#include "notes.h"
#include "reminders.h"
#include "settings.h"
#include "storage.h"
#include "tasks.h"
#include <qrcode.h>

namespace services {
namespace console {
namespace {

char buffer[64];
size_t used = 0;
void (*onRedraw)() = nullptr;
const gfx::Canvas* screenCanvas = nullptr;
bool (*openScreen)(const char*) = nullptr;

// Set by the encode callback so the caller can report the grid size.
int lastQrModules = 0;
void captureQrSize(esp_qrcode_handle_t qr) {
  lastQrModules = esp_qrcode_get_size(qr);
}

void printTime() {
  if (!rtc::timeIsValid()) {
    Serial.println("clock not set (run: time <unix epoch>)");
    return;
  }
  const time_t now = time(nullptr);
  struct tm utc;
  gmtime_r(&now, &utc);

  char text[40];
  strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &utc);
  Serial.printf("utc    %s\n", text);

  struct tm local;
  if (rtc::localNow(&local)) {
    strftime(text, sizeof(text), "%a %Y-%m-%d %H:%M:%S", &local);
    Serial.printf("local  %s (utc%+d min)\n", text, rtc::utcOffsetMinutes());
  }
  Serial.printf("source %s\n",
                rtc::hasChip() ? "PCF85063" : "internal (lost on power cut)");
}

void handle(char* line) {
  // Split off the first word.
  char* arg = strchr(line, ' ');
  if (arg) {
    *arg = '\0';
    arg++;
    while (*arg == ' ') arg++;
  }

  if (strcmp(line, "time") == 0) {
    if (!arg || !*arg) {
      Serial.println("usage: time <unix epoch seconds, UTC>");
      return;
    }
    const time_t epoch = (time_t)strtoll(arg, nullptr, 10);
    if (!rtc::setSystemUtc(epoch)) {
      Serial.println("refused: timestamp is implausibly old");
      return;
    }
    Serial.println("clock set");
    printTime();

  } else if (strcmp(line, "tz") == 0) {
    if (!arg || !*arg) {
      Serial.printf("utc offset is %d minutes\n", rtc::utcOffsetMinutes());
      return;
    }
    rtc::setUtcOffsetMinutes((int)strtol(arg, nullptr, 10));
    printTime();

  } else if (strcmp(line, "now") == 0) {
    printTime();

  } else if (strcmp(line, "rtc") == 0) {
    // Re-probe and read the chip directly, bypassing the system clock. Used to
    // check whether the RTC survives its rail being cut.
    rtc::begin();
    if (!rtc::hasChip()) {
      Serial.println("no chip on the bus (is the audio rail up?)");
      return;
    }
    struct tm utc;
    if (!rtc::readUtc(&utc)) {
      Serial.println("chip present but time invalid (oscillator was stopped)");
      return;
    }
    char text[32];
    strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &utc);
    Serial.printf("chip utc %s\n", text);

  } else if (strcmp(line, "env") == 0) {
    const environment::Reading r = environment::measure();
    if (!r.valid) {
      Serial.println("sensor read failed");
      return;
    }
    Serial.printf("%.1f C  %.0f%% RH\n", r.celsius, r.humidity);

  } else if (strcmp(line, "batt") == 0) {
    Serial.printf("%.2f V  %d%%\n", power::batteryVolts(),
                  power::batteryPercent());

  } else if (strcmp(line, "scan") == 0) {
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
      if (!i2c::probe(addr)) continue;
      Serial.printf("  0x%02X\n", addr);
      found++;
    }
    Serial.printf("%d device(s)\n", found);

  } else if (config::kDevTools && strcmp(line, "shot") == 0) {
    // Dump the framebuffer so a screenshot can be taken without photographing
    // the panel. Real pixels, not a mockup — which matters for documenting a
    // 200x200 display, where a photograph of e-paper under room lighting looks
    // nothing like what the layout actually is.
    //
    // Streamed as hex a row at a time rather than base64 in one string: 5000
    // bytes is a large allocation to make on a whim, and a row per line means
    // a truncated transfer is obvious rather than silently corrupt.
    if (!screenCanvas) {
      Serial.println("no canvas");
      return;
    }
    const int w = screenCanvas->width();
    const int h = screenCanvas->height();
    const int stride = (w + 7) / 8;
    const uint8_t* bits = screenCanvas->bits();
    if (!bits) {
      Serial.println("no framebuffer");
      return;
    }
    Serial.printf("SHOT %d %d\n", w, h);
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < stride; x++) {
        Serial.printf("%02X", bits[y * stride + x]);
      }
      Serial.println();
    }
    Serial.println("ENDSHOT");

  } else if (config::kDevTools && strcmp(line, "screen") == 0) {
    // Open a screen by name, so every screen can be captured without anyone
    // pressing buttons in the right order forty times.
    if (!openScreen) {
      Serial.println("no screen hook");
      return;
    }
    if (!openScreen(arg && *arg ? arg : nullptr)) {
      Serial.println("unknown screen");
    }

  } else if (config::kDevTools && strcmp(line, "dist") == 0) {
    // Live distance readings, for checking the sensor against a ruler. A
    // measuring tool has to be verified against something known, not just
    // observed to produce numbers.
    if (!range::available()) {
      Serial.println("no VL53L0X (try: tof)");
      return;
    }
    const int samples = (arg && *arg) ? atoi(arg) : 20;
    for (int i = 0; i < samples && i < 200; i++) {
      const uint16_t mm = range::readMm();
      if (mm == 0) {
        Serial.println("  out of range");
      } else {
        Serial.printf("  %4u mm   %5.1f cm   %5.2f in\n", (unsigned)mm,
                      mm / 10.0f, mm / 25.4f);
      }
      delay(150);
    }

  } else if (config::kDevTools && strcmp(line, "tof") == 0) {
    // Which time-of-flight part is on 0x29?
    //
    // The VL53L0X and the VL53L1X share an address and share nothing else —
    // different register widths, different init sequences, different ranging
    // API. Identifying it by reading its ID is the only way to know which
    // driver to run, and the two families even disagree about how wide a
    // register address is:
    //
    //   VL53L0X   8-bit register  0xC0        -> 0xEE
    //   VL53L1X   16-bit register 0x010F/0x0110 -> 0xEA 0xCC
    constexpr uint8_t kToF = 0x29;
    if (!i2c::probe(kToF)) {
      Serial.println("nothing at 0x29");
      return;
    }

    uint8_t id8 = 0;
    const bool got8 = i2c::readRegister(kToF, 0xC0, &id8, 1);
    Serial.printf("8-bit  reg 0xC0  -> %s 0x%02X\n", got8 ? "ok" : "FAIL", id8);

    uint8_t model[2] = {0, 0};
    const uint8_t reg16[2] = {0x01, 0x0F};
    bool got16 = i2c::write(kToF, reg16, 2) && i2c::read(kToF, model, 2);
    Serial.printf("16-bit reg 0x010F -> %s 0x%02X 0x%02X\n",
                  got16 ? "ok" : "FAIL", model[0], model[1]);

    if (got8 && id8 == 0xEE) {
      Serial.println("=> VL53L0X");
    } else if (got16 && model[0] == 0xEA && model[1] == 0xCC) {
      Serial.println("=> VL53L1X");
    } else if (got16 && model[0] == 0xEB && model[1] == 0xAA) {
      Serial.println("=> VL53L3CX / L4CD family");
    } else {
      Serial.println("=> unrecognised, see the raw bytes above");
    }

  } else if (config::kDevTools && strcmp(line, "motion") == 0) {
    // Bring-up aid for the accelerometer. Prints live readings so a shake
    // threshold can be chosen from what the part actually reports rather than
    // from what the datasheet implies — the same reason `buzz` exists.
    if (!motion::available()) {
      Serial.println("no MPU6050 (try: scan)");
      return;
    }
    const int samples = (arg && *arg) ? atoi(arg) : 40;
    Serial.printf("reading 0x%02X, %d samples\n", motion::address(), samples);
    float peak = 0.0f;
    for (int i = 0; i < samples && i < 400; i++) {
      float x, y, z;
      if (!motion::read(x, y, z)) {
        Serial.println("  read failed");
        break;
      }
      const float g = sqrtf(x * x + y * y + z * z);
      if (g > peak) peak = g;
      Serial.printf("  x=%+.2f y=%+.2f z=%+.2f  |a|=%.2f g\n", x, y, z, g);
      delay(50);
    }
    Serial.printf("peak %.2f g\n", peak);

  } else if (config::kDevTools && strcmp(line, "buzz") == 0) {
    // Bring-up aid for the haptic motor. An eccentric-mass motor needs tens
    // of milliseconds to spin up, so "I felt nothing" is ambiguous between a
    // pulse that was too short and a pin with nothing on it. Sweeping the
    // duration by hand settles which.
    const uint32_t ms = (arg && *arg) ? (uint32_t)atoi(arg) : 200;
    if (ms == 0 || ms > 3000) {
      Serial.println("usage: buzz [ms]   (1-3000, default 200)");
      return;
    }
    Serial.printf("buzzing GPIO %d for %lu ms\n", (int)pins::HAPTIC,
                  (unsigned long)ms);
    haptics::test(ms);
    Serial.println("done");

  } else if (config::kDevTools && strcmp(line, "rail") == 0) {
    // Bring-up aid: some peripherals only appear on the bus once their rail is
    // up, so being able to toggle them without reflashing saves a cycle.
    if (!arg || !*arg) {
      Serial.println("usage: rail epd|audio on|off");
      return;
    }
    char* state = strchr(arg, ' ');
    if (state) {
      *state = '\0';
      state++;
    }
    const bool on = state && strcmp(state, "on") == 0;
    if (strcmp(arg, "epd") == 0) {
      power::epdRail(on);
    } else if (strcmp(arg, "audio") == 0) {
      power::audioRail(on);
    } else {
      Serial.println("usage: rail epd|audio on|off");
      return;
    }
    Serial.printf("%s rail %s\n", arg, on ? "on" : "off");

  } else if (config::kDevTools && strcmp(line, "rot") == 0) {
    // Bring-up: step through every way the panel could be mounted. With no
    // argument it advances by one, so it can be driven blind from a terminal.
    static int index = -1;
    if (arg && *arg) {
      index = (int)strtol(arg, nullptr, 10) % epaper::kOrientationCount;
    } else {
      index = (index + 1) % epaper::kOrientationCount;
    }
    const epaper::Orientation o = epaper::kOrientations[index];
    epaper::setOrientation(o);
    Serial.printf("orientation %d: %d turns%s\n", index, o.quarterTurns,
                  o.mirror ? " + mirror" : "");
    if (onRedraw) onRedraw();

  } else if (strcmp(line, "links") == 0) {
    // Encode every link and report how it came out. Catches an over-long URL
    // here rather than as a blank screen on the device.
    for (int i = 0; i < links::kLinkCount; i++) {
      lastQrModules = 0;
      esp_qrcode_config_t cfg = {};
      cfg.display_func = captureQrSize;
      cfg.max_qrcode_version = 8;
      cfg.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
      const esp_err_t err = esp_qrcode_generate(&cfg, links::kLinks[i].url);

      if (err != ESP_OK || lastQrModules <= 0) {
        Serial.printf("  %-12s FAILED  %s\n", links::kLinks[i].label,
                      links::kLinks[i].url);
        continue;
      }
      // The screen fits the code into the space above the label.
      const int scale = 158 / (lastQrModules + 4);
      Serial.printf("  %-12s %2dx%-2d  %dpx/module %s  %s\n",
                    links::kLinks[i].label, lastQrModules, lastQrModules,
                    scale, scale >= 3 ? "ok " : "TIGHT", links::kLinks[i].url);
    }

  } else if (strcmp(line, "task") == 0) {
    // Until the web app lands in phase 5 this is the only way to create a
    // task — two buttons cannot enter text.
    if (!arg || !*arg) {
      Serial.printf("%d task(s), %d left%s\n", tasks::count(),
                    tasks::remainingCount(),
                    storage::available() ? "" : " (no card: not saved)");
      for (int i = 0; i < tasks::count(); i++) {
        const tasks::Task* t = tasks::at(i);
        Serial.printf("  %2d [%c] %s\n", i, t->done ? 'x' : ' ', t->text);
      }
      return;
    }
    if (!tasks::add(arg)) {
      Serial.println("could not add (list full or empty text)");
      return;
    }
    Serial.printf("added: %s\n", arg);
    if (onRedraw) onRedraw();

  } else if (strcmp(line, "taskclear") == 0) {
    tasks::clearCompleted();
    Serial.printf("%d remaining\n", tasks::count());
    if (onRedraw) onRedraw();

  } else if (strcmp(line, "sd") == 0) {
    if (!storage::available()) {
      Serial.println("no card mounted");
      return;
    }
    Serial.printf("%llu MB used of %llu MB\n",
                  storage::usedBytes() / (1024ULL * 1024ULL),
                  storage::totalBytes() / (1024ULL * 1024ULL));

  } else if (strcmp(line, "wifi") == 0) {
    // Credentials are entered here or in the web app — never compiled in, and
    // never written to a file in the source tree. They live in NVS only.
    if (!arg || !*arg || strcmp(arg, "status") == 0) {
      Serial.printf("state %d, %d saved network(s)\n", (int)network::state(),
                    settings::networkCount());
      for (int i = 0; i < settings::networkCount(); i++) {
        Serial.printf("  %d %s\n", i, settings::network(i).ssid.c_str());
      }
      const String ip = network::ipAddress();
      if (ip.length()) Serial.printf("  ip %s\n", ip.c_str());
      return;
    }

    char* rest = strchr(arg, ' ');
    if (rest) {
      *rest = '\0';
      rest++;
      while (*rest == ' ') rest++;
    }

    if (strcmp(arg, "add") == 0) {
      if (!rest || !*rest) {
        Serial.println("usage: wifi add <ssid> <password>");
        return;
      }
      char* pass = strchr(rest, ' ');
      if (pass) {
        *pass = '\0';
        pass++;
      }
      settings::addNetwork(rest, pass ? pass : "");
      Serial.printf("saved \"%s\"\n", rest);
    } else if (strcmp(arg, "join") == 0) {
      network::join();
      Serial.println("joining...");
    } else if (strcmp(arg, "portal") == 0) {
      network::startPortal();
    } else if (strcmp(arg, "off") == 0) {
      network::stop();
      Serial.println("radio off");
    } else if (strcmp(arg, "sync") == 0) {
      Serial.println(network::syncTime() ? "clock synced" : "sync failed");
    } else if (strcmp(arg, "forget") == 0) {
      settings::clearNetworks();
      Serial.println("all networks forgotten");
    } else {
      Serial.println("usage: wifi status|add|join|portal|sync|off|forget");
    }

  } else if (strcmp(line, "rem") == 0) {
    if (!arg || !*arg) {
      Serial.printf("%d reminder(s), next in %us\n", reminders::count(),
                    (unsigned)reminders::secondsUntilNext());
      for (int i = 0; i < reminders::count(); i++) {
        const reminders::Reminder* r = reminders::at(i);
        Serial.printf("  %d [%c] %02d:%02d days=0x%02X %s\n", i,
                      r->enabled ? 'x' : ' ', r->hour, r->minute, r->days,
                      r->text);
      }
      return;
    }
    // rem <HH:MM> <text>
    char* text = strchr(arg, ' ');
    if (!text) {
      Serial.println("usage: rem HH:MM <message>");
      return;
    }
    *text = '\0';
    text++;
    char* colon = strchr(arg, ':');
    if (!colon) {
      Serial.println("usage: rem HH:MM <message>");
      return;
    }
    *colon = '\0';
    const int hour = atoi(arg);
    const int minute = atoi(colon + 1);
    if (!reminders::add(text, hour, minute, reminders::kEveryDay)) {
      Serial.println("could not add");
      return;
    }
    Serial.printf("set %02d:%02d %s (next in %us)\n", hour, minute, text,
                  (unsigned)reminders::secondsUntilNext());

  } else if (strcmp(line, "codec") == 0) {
    // codec            dump the registers that matter for capture
    // codec <reg> <val>  poke one, in hex
    if (!arg || !*arg) {
      const uint8_t regs[] = {0x0D, 0x0E, 0x12, 0x14, 0x16, 0x17, 0x1B, 0x1C};
      for (uint8_t r : regs) {
        uint8_t v = 0;
        audio::es8311::peekRegister(r, &v);
        Serial.printf("  0x%02X = 0x%02X\n", r, v);
      }
      return;
    }
    char* valText = strchr(arg, ' ');
    if (!valText) {
      Serial.println("usage: codec <reg hex> <value hex>");
      return;
    }
    *valText = '\0';
    valText++;
    const uint8_t reg = (uint8_t)strtol(arg, nullptr, 16);
    const uint8_t val = (uint8_t)strtol(valText, nullptr, 16);
    audio::es8311::pokeRegister(reg, val);
    Serial.printf("0x%02X <- 0x%02X\n", reg, val);

  } else if (strcmp(line, "note") == 0) {
    // Record a real note, index entry and all, without needing the button.
    // Used for testing the transcription path end to end.
    const int seconds = (arg && *arg) ? atoi(arg) : 4;
    const int number = notes::nextNumber();
    const String path = notes::wavPath(number);

    if (!audio::recorder::start(path.c_str())) {
      Serial.println("could not start");
      return;
    }
    Serial.printf("recording note %03d for %ds...\n", number, seconds);
    const uint32_t until = millis() + (uint32_t)seconds * 1000;
    while (millis() < until) {
      if (!audio::recorder::pump()) break;
    }
    const uint32_t ms = audio::recorder::elapsedMs();
    if (!audio::recorder::stop()) {
      Serial.println("too short, discarded");
      return;
    }
    notes::add(number, "Note", ms);
    Serial.printf("saved note %03d (%ums)\n", number, (unsigned)ms);

  } else if (strcmp(line, "mic") == 0) {
    // Record a few seconds, report the signal level, then play it back.
    // Level is the point: a working I2S link with a powered-down ADC produces
    // a perfectly valid file of zeros, which looks identical to success
    // everywhere except here.
    const int seconds = (arg && *arg) ? atoi(arg) : 3;
    const char* path = "/atomic/mictest.wav";

    if (!audio::recorder::start(path)) {
      Serial.println("could not start recording");
      return;
    }
    Serial.printf("recording %ds...\n", seconds);
    const uint32_t until = millis() + (uint32_t)seconds * 1000;
    while (millis() < until) {
      if (!audio::recorder::pump()) break;
    }
    audio::recorder::stop();

    // Read it back and measure peak amplitude.
    File f = SD_MMC.open(path, FILE_READ);
    if (!f) {
      Serial.println("cannot reopen recording");
      return;
    }
    f.seek(44);
    int16_t peak = 0;
    int64_t energy = 0;
    long samples = 0;
    long clipped = 0;
    long jumps = 0;
    int16_t previous = 0;
    int16_t buf[512];
    while (f.available()) {
      const int n = f.read((uint8_t*)buf, sizeof(buf));
      if (n <= 0) break;
      for (int i = 0; i < n / 2; i++) {
        const int16_t raw = buf[i];
        const int16_t v = raw < 0 ? (int16_t)-raw : raw;
        if (v > peak) peak = v;
        if (v > 32000) clipped++;
        // A discontinuity larger than half full scale between neighbouring
        // samples is not speech — it is a dropped DMA block. That is what
        // distinguishes a buffer overrun from ordinary clipping.
        if (samples > 0 && abs((int)raw - (int)previous) > 16000) jumps++;
        previous = raw;
        energy += (int64_t)v;
        samples++;
      }
    }
    f.close();
    Serial.printf("peak %d  mean %ld  clipped %ld (%.2f%%)  jumps %ld\n", peak,
                  samples ? (long)(energy / samples) : 0L, clipped,
                  samples ? (100.0 * clipped / samples) : 0.0, jumps);
    if (clipped * 200 > samples) Serial.println("  -> CLIPPING: reduce gain");
    if (jumps > 8) Serial.println("  -> DROPOUTS: capture is not keeping up");
    if (peak < 2000) Serial.println("  -> too quiet");

    Serial.println("playing back...");
    if (audio::player::start(path)) {
      while (audio::player::pump()) {
      }
      audio::player::stop();
      Serial.println("playback done");
    }

  } else if (strcmp(line, "speak") == 0) {
    // Exercises the whole spoken-answer path from the console: fetch the
    // synthesised WAV and play it. Worth having as a command because the
    // alternative is holding a button and guessing which half broke.
    if (!arg || !*arg) {
      Serial.println("usage: speak <text>");
      return;
    }
    String base = network::resolved(settings::companionUrl());
    base.trim();
    while (base.endsWith("/")) base.remove(base.length() - 1);
    if (base.length() == 0) {
      Serial.println("no companion URL set");
      return;
    }
    if (!network::isJoined()) {
      Serial.println("not on wifi");
      return;
    }

    HTTPClient http;
    http.setTimeout(30000);
    if (!http.begin(base + "/speak")) {
      Serial.println("bad URL");
      return;
    }
    http.addHeader("Content-Type", "text/plain");
    const int code = http.POST(String(arg));
    Serial.printf("http %d, content-length %d\n", code, http.getSize());
    if (code != 200) {
      http.end();
      return;
    }

    const char* path = "/atomic/speaktest.wav";
    File out = SD_MMC.open(path, FILE_WRITE);
    if (!out) {
      Serial.println("cannot open file for write");
      http.end();
      return;
    }
    const int written = http.writeToStream(&out);
    out.close();
    http.end();
    Serial.printf("wrote %d bytes\n", written);

    File check = SD_MMC.open(path, FILE_READ);
    if (check) {
      Serial.printf("on card: %u bytes\n", (unsigned)check.size());
      check.close();
    }

    if (audio::player::start(path)) {
      Serial.println("playing...");
      while (audio::player::pump()) {
      }
      audio::player::stop();
      Serial.println("playback done");
    } else {
      Serial.println("player::start FAILED");
    }

  } else if (strcmp(line, "batttest") == 0) {
    if (arg && strcmp(arg, "on") == 0) {
      battery_log::start();
      Serial.printf("battery test started, sampling every %u min\n",
                    (unsigned)(battery_log::kIntervalSec / 60));
      Serial.println("unplug USB now, then leave it alone");
    } else if (arg && strcmp(arg, "off") == 0) {
      battery_log::stop();
      Serial.println("battery test stopped");
    } else if (arg && strcmp(arg, "dump") == 0) {
      String blob;
      if (storage::readFile("/atomic/battery.csv", blob, 32768)) {
        Serial.println(blob);
      } else {
        Serial.println("no readings yet");
      }
    } else {
      Serial.printf("battery test %s\n",
                    battery_log::running() ? "RUNNING" : "off");
      Serial.println("usage: batttest on|off|dump");
    }

  } else if (strcmp(line, "sleep") == 0) {
    enterDeepSleep();

  } else if (strcmp(line, "help") == 0) {
    Serial.println(
        "time <epoch> | tz <min> | now | rtc | rot | links | wifi | note | mic | codec | rem | task | taskclear | sd | env | batt | scan | rail | sleep");

  } else if (*line) {
    Serial.printf("unknown command: %s (try help)\n", line);
  }
}

}  // namespace

void setRedrawHook(void (*hook)()) { onRedraw = hook; }

void setCanvas(const gfx::Canvas* canvas) { screenCanvas = canvas; }

void setScreenHook(bool (*hook)(const char*)) { openScreen = hook; }

void poll() {
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      buffer[used] = '\0';
      handle(buffer);
      used = 0;
      continue;
    }
    // Overlong lines are truncated rather than allowed to overflow; the next
    // newline still resets cleanly.
    if (used < sizeof(buffer) - 1) buffer[used++] = c;
  }
}

}  // namespace console
}  // namespace services
