#include "webapp.h"

#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>

#include "settings.h"
#include "storage.h"
#include <SD_MMC.h>

#include "notes.h"
#include "reminders.h"
#include "tasks.h"

namespace services {
namespace webapp {
namespace {

WebServer server(80);
bool active = false;

// ── Page furniture ────────────────────────────────────────────────────────
// Inline CSS, no external anything: the portal serves a phone that has just
// joined an access point with no internet, so every byte has to come from here.
const char kStyle[] PROGMEM = R"CSS(
<style>
*{box-sizing:border-box}
body{margin:0;padding:20px;background:#eceae4;color:#14140f;
 font:16px/1.5 -apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}
.w{max-width:640px;margin:0 auto}
h1{font-size:26px;letter-spacing:-.02em;margin:0 0 4px}
.sub{color:#6d6a62;font-size:13px;text-transform:uppercase;
 letter-spacing:.1em;margin-bottom:22px}
.card{background:#fff;border:2px solid #14140f;border-radius:10px;
 padding:16px;margin-bottom:16px}
h2{font-size:13px;text-transform:uppercase;letter-spacing:.09em;
 margin:0 0 12px;color:#6d6a62}
label{display:block;font-size:13px;margin:10px 0 4px;color:#6d6a62}
input,select{width:100%;font:inherit;padding:10px;border:2px solid #14140f;
 border-radius:6px;background:#fff}
button{font:inherit;font-weight:600;padding:10px 16px;border:2px solid #14140f;
 border-radius:6px;background:#14140f;color:#fff;cursor:pointer;margin-top:12px}
button.ghost{background:#fff;color:#14140f}
.row{display:flex;gap:8px;align-items:center;padding:10px 0;
 border-top:1px solid #e2e0d9}
.row:first-of-type{border-top:0}
.row .grow{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;
 white-space:nowrap}
.done{text-decoration:line-through;color:#9b978d}
.msg{background:#14140f;color:#fff;padding:10px 14px;border-radius:6px;
 margin-bottom:16px;font-size:14px}
a{color:#14140f}
form.inline{display:flex;gap:8px}
form.inline input{flex:1}
form.inline button{margin-top:0}
.btn{display:inline-block;border:2px solid #14140f;border-radius:6px;
 padding:4px 10px;font-size:13px;text-decoration:none;white-space:nowrap}
.note{border-top:1px solid #e2e0d9;padding:10px 0}
.note:first-of-type{border-top:0}
.note .row{border-top:0;padding:0}
.tag{border:1px solid #14140f;border-radius:999px;padding:1px 8px;
 font-size:12px;white-space:nowrap}
.transcript{margin:8px 0 0;font-size:14px;color:#3b3a32;white-space:pre-wrap}
</style>
)CSS";

String esc(const String& raw) {
  String out;
  out.reserve(raw.length() + 8);
  for (size_t i = 0; i < raw.length(); i++) {
    const char c = raw[i];
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else if (c == '\'') out += "&#39;";
    else out += c;
  }
  return out;
}

String pageTop(const String& title) {
  String h = F("<!doctype html><html><head><meta charset='utf-8'>"
               "<meta name='viewport' content='width=device-width,"
               "initial-scale=1'><title>");
  h += esc(title);
  h += F("</title>");
  h += FPSTR(kStyle);
  h += F("</head><body><div class='w'>");
  return h;
}

String pageBottom() { return F("</div></body></html>"); }

void sendPage(const String& body) {
  server.send(200, "text/html", body + pageBottom());
}

void redirect(const char* to) {
  server.sendHeader("Location", to);
  server.send(303);
}

// ── Handlers ──────────────────────────────────────────────────────────────

void handleRoot() {
  String p = pageTop("Atomic Note");
  p += F("<h1>");
  p += esc(settings::deviceName());
  p += F("</h1><div class='sub'>configuration</div>");

  if (server.hasArg("msg")) {
    p += F("<div class='msg'>");
    p += esc(server.arg("msg"));
    p += F("</div>");
  }

  // WiFi
  p += F("<div class='card'><h2>WiFi</h2>");
  if (WiFi.status() == WL_CONNECTED) {
    p += F("<p>Connected to <b>");
    p += esc(WiFi.SSID());
    p += F("</b><br><span class='sub'>");
    p += esc(WiFi.localIP().toString());
    p += F("</span></p>");
  } else {
    p += F("<p>Not connected.</p>");
  }
  for (int i = 0; i < settings::networkCount(); i++) {
    p += F("<div class='row'><span class='grow'>");
    p += esc(settings::network(i).ssid);
    p += F("</span><a class='ghost' href='/wifi/forget?i=");
    p += String(i);
    p += F("'>Forget</a></div>");
  }
  p += F("<form action='/wifi/add' method='post'>"
         "<label>Network name</label><input name='ssid' required>"
         "<label>Password</label><input name='pass' type='password'>"
         "<button type='submit'>Save &amp; connect</button></form></div>");

  // Tasks
  p += F("<div class='card'><h2>Tasks</h2>");
  if (!storage::available()) {
    p += F("<p class='sub'>No SD card — tasks will not survive a reboot.</p>");
  }
  for (int i = 0; i < tasks::count(); i++) {
    const tasks::Task* t = tasks::at(i);
    p += F("<div class='row'><a class='ghost' href='/task/toggle?i=");
    p += String(i);
    p += F("'>");
    p += t->done ? F("&#9745;") : F("&#9744;");
    p += F("</a><span class='grow");
    p += t->done ? F(" done'>") : F("'>");
    p += esc(t->text);
    p += F("</span><a href='/task/delete?i=");
    p += String(i);
    p += F("'>&times;</a></div>");
  }
  p += F("<form class='inline' action='/task/add' method='post'>"
         "<input name='text' placeholder='New task' required>"
         "<button type='submit'>Add</button></form></div>");

  // Recordings
  p += F("<div class='card'><h2>Recordings</h2>");
  if (notes::count() == 0) {
    p += F("<p class='sub'>Nothing recorded yet. Hold the top button on the "
           "clock screen.</p>");
  }
  for (int v = 0; v < notes::count(); v++) {
    // Newest first, matching the device.
    const int i = notes::count() - 1 - v;
    const notes::Note* n = notes::at(i);
    p += F("<div class='note'><div class='row'><b>");
    char head[32];
    snprintf(head, sizeof(head), "%03d", n->number);
    p += head;
    p += F("</b><span class='tag'>");
    p += esc(n->tag);
    p += F("</span><span class='grow sub'>");
    char meta[24];
    snprintf(meta, sizeof(meta), "%lus", (unsigned long)(n->durationMs / 1000));
    p += meta;
    p += F("</span><a class='btn' href='/note/wav?n=");
    p += String(n->number);
    p += F("'>WAV</a>");
    if (n->hasText) {
      p += F("<a class='btn' href='/note/txt?n=");
      p += String(n->number);
      p += F("'>TXT</a>");
    }
    p += F("<a href='/note/delete?i=");
    p += String(i);
    p += F("' onclick=\"return confirm('Delete this recording?')\">&times;</a>"
           "</div>");
    if (n->hasText) {
      p += F("<p class='transcript'>");
      p += esc(notes::transcript(i));
      p += F("</p>");
    }
    p += F("</div>");
  }
  if (notes::count() > 0) {
    p += F("<div class='row'><a class='btn' href='/notes/all.txt'>"
           "Download all transcripts</a></div>");
  }
  p += F("</div>");

  // Reminders
  p += F("<div class='card'><h2>Reminders</h2>");
  for (int i = 0; i < reminders::count(); i++) {
    const reminders::Reminder* r = reminders::at(i);
    char when[10];
    snprintf(when, sizeof(when), "%02d:%02d", r->hour, r->minute);
    p += F("<div class='row'><a class='ghost' href='/rem/toggle?i=");
    p += String(i);
    p += F("'>");
    p += r->enabled ? F("&#9745;") : F("&#9744;");
    p += F("</a><b>");
    p += when;
    p += F("</b><span class='grow");
    p += r->enabled ? F("'>") : F(" done'>");
    p += esc(r->text);
    p += F("</span><a href='/rem/delete?i=");
    p += String(i);
    p += F("'>&times;</a></div>");
  }
  p += F("<form action='/rem/add' method='post'>"
         "<label>Time</label><input name='at' type='time' required>"
         "<label>Repeat</label><select name='days'>"
         "<option value='127'>Every day</option>"
         "<option value='62'>Weekdays</option>"
         "<option value='65'>Weekends</option>"
         "<option value='0'>Once</option></select>"
         "<label>Message</label><input name='text' maxlength='47' required>"
         "<button type='submit'>Add reminder</button></form></div>");

  // Links
  p += F("<div class='card'><h2>QR links</h2>");
  for (int i = 0; i < settings::linkCount(); i++) {
    p += F("<form action='/link/set' method='post'>"
           "<input type='hidden' name='i' value='");
    p += String(i);
    p += F("'><div class='row'><input name='label' value='");
    p += esc(settings::link(i).label);
    p += F("' style='max-width:34%'><input name='url' value='");
    p += esc(settings::link(i).url);
    p += F("'><button type='submit'>Save</button>"
           "<a href='/link/delete?i=");
    p += String(i);
    p += F("'>&times;</a></div></form>");
  }
  p += F("<form action='/link/add' method='post'>"
         "<label>Add a link</label>"
         "<input name='label' placeholder='Label'>"
         "<input name='url' placeholder='https://' required>"
         "<button type='submit'>Add link</button></form></div>");

  // Transcription
  p += F("<div class='card'><h2>Transcription</h2>"
         "<p class='sub'>Run companion/transcribe_server.py on a machine on "
         "this network, then paste the address it prints.</p>"
         "<form action='/companion' method='post'>"
         "<label>Service address</label>"
         "<input name='url' placeholder='http://192.168.1.20:8710' value='");
  p += esc(settings::companionUrl());
  p += F("'><label><input type='checkbox' name='auto' style='width:auto' ");
  if (settings::autoSync()) p += F("checked");
  p += F("> Transcribe automatically when online</label>"
         "<button type='submit'>Save</button></form>");
  {
    char pending[48];
    snprintf(pending, sizeof(pending), "%d note(s) awaiting transcription",
             notes::countWithoutText());
    p += F("<p class='sub'>");
    p += pending;
    p += F("</p>");
  }
  p += F("</div>");

  // Claude limits
  p += F("<div class='card'><h2>Claude limits</h2>"
         "<p class='sub'>Claude Code does not publish your limits, and does "
         "not save the figures it shows. Run <b>/usage</b> in Claude Code and "
         "copy the two numbers here once \u2014 the device then tracks them on "
         "its own.</p>"
         "<form action='/calibrate' method='post'>"
         "<label>Session (5hr) percent</label>"
         "<input name='spct' type='number' min='0' max='100' placeholder='93'>"
         "<label>Session resets in (minutes)</label>"
         "<input name='smin' type='number' min='0' max='300' placeholder='32'>"
         "<label>Weekly percent</label>"
         "<input name='wpct' type='number' min='0' max='100' placeholder='18'>"
         "<label>Weekly resets in (days)</label>"
         "<input name='wday' type='number' min='0' max='7' placeholder='6'>"
         "<button type='submit'>Calibrate</button></form></div>");

  // Device
  p += F("<div class='card'><h2>Device</h2>"
         "<form action='/device' method='post'>"
         "<label>Name (also the setup network name)</label>"
         "<input name='name' value='");
  p += esc(settings::deviceName());
  p += F("'><label>UTC offset, minutes (330 = IST, 0 = UTC, -300 = EST)</label>"
         "<input name='tz' type='number' step='15' min='-720' max='840' "
         "value='");
  p += String(settings::utcOffsetMinutes());
  p += F("'><button type='submit'>Save</button></form></div>");

  sendPage(p);
}

void handleWifiAdd() {
  const String ssid = server.arg("ssid");
  const String pass = server.arg("pass");
  if (ssid.length() == 0) {
    redirect("/?msg=Network%20name%20required");
    return;
  }
  settings::addNetwork(ssid.c_str(), pass.c_str());
  redirect("/?msg=Saved.%20Restart%20to%20join%20it.");
}

void handleWifiForget() {
  settings::removeNetwork(server.arg("i").toInt());
  redirect("/?msg=Forgotten");
}

void handleTaskAdd() {
  if (!tasks::add(server.arg("text").c_str())) {
    redirect("/?msg=Could%20not%20add%20task");
    return;
  }
  redirect("/");
}

void handleTaskToggle() {
  tasks::toggleDone(server.arg("i").toInt());
  redirect("/");
}

void handleTaskDelete() {
  tasks::removeAt(server.arg("i").toInt());
  redirect("/");
}

void handleReminderAdd() {
  // <input type=time> gives "HH:MM".
  const String at = server.arg("at");
  const int colon = at.indexOf(':');
  if (colon < 0) {
    redirect("/?msg=Enter%20a%20time");
    return;
  }
  const int hour = at.substring(0, colon).toInt();
  const int minute = at.substring(colon + 1).toInt();

  if (!reminders::add(server.arg("text").c_str(), hour, minute,
                      (uint8_t)server.arg("days").toInt())) {
    redirect("/?msg=Could%20not%20add%20reminder");
    return;
  }
  redirect("/?msg=Reminder%20set");
}

void handleReminderToggle() {
  const int i = server.arg("i").toInt();
  const reminders::Reminder* r = reminders::at(i);
  if (r) reminders::setEnabled(i, !r->enabled);
  redirect("/");
}

void handleReminderDelete() {
  reminders::removeAt(server.arg("i").toInt());
  redirect("/");
}

void handleLinkSet() {
  settings::setLink(server.arg("i").toInt(), server.arg("label").c_str(),
                    server.arg("url").c_str());
  redirect("/?msg=Link%20saved");
}

void handleLinkAdd() {
  if (!settings::addLink(server.arg("label").c_str(),
                         server.arg("url").c_str())) {
    redirect("/?msg=No%20room%20for%20another%20link");
    return;
  }
  redirect("/?msg=Link%20added");
}

void handleLinkDelete() {
  settings::removeLink(server.arg("i").toInt());
  redirect("/?msg=Link%20removed");
}

// Streams straight from the card rather than reading the file into RAM — a
// minute of audio is about 2 MB and would not fit.
void sendNoteFile(const String& path, const char* mime, const String& filename) {
  File f = SD_MMC.open(path.c_str(), FILE_READ);
  if (!f) {
    server.send(404, "text/plain", "not found");
    return;
  }
  server.sendHeader("Content-Disposition",
                    String("attachment; filename=\"") + filename + "\"");
  server.streamFile(f, mime);
  f.close();
}

void handleNoteWav() {
  const int number = server.arg("n").toInt();
  sendNoteFile(notes::wavPath(number), "audio/wav",
               String("note_") + String(number) + ".wav");
}

void handleNoteTxt() {
  const int number = server.arg("n").toInt();
  sendNoteFile(notes::textPath(number), "text/plain; charset=utf-8",
               String("note_") + String(number) + ".txt");
}

void handleNoteDelete() {
  notes::removeAt(server.arg("i").toInt());
  redirect("/?msg=Recording%20deleted");
}

// Everything in one file, newest first — the usual reason to open this page.
void handleAllTranscripts() {
  String out;
  for (int v = 0; v < notes::count(); v++) {
    const int i = notes::count() - 1 - v;
    const notes::Note* n = notes::at(i);
    if (!n->hasText) continue;
    char head[48];
    snprintf(head, sizeof(head), "#%03d  [%s]  %lus\n", n->number, n->tag,
             (unsigned long)(n->durationMs / 1000));
    out += head;
    out += notes::transcript(i);
    out += "\n\n----------------------------------------\n\n";
  }
  if (out.length() == 0) out = "No transcripts yet.\n";
  server.sendHeader("Content-Disposition",
                    "attachment; filename=\"atomic-notes.txt\"");
  server.send(200, "text/plain; charset=utf-8", out);
}

// Forwards the figures the user read off Claude Code to the companion, which
// is where the session history lives.
void handleCalibrate() {
  String base = settings::companionUrl();
  base.trim();
  while (base.endsWith("/")) base.remove(base.length() - 1);
  if (base.length() == 0) {
    redirect("/?msg=Set%20the%20service%20address%20first");
    return;
  }

  String body = "{";
  body += "\"session_percent\":" + server.arg("spct") + ",";
  body += "\"session_reset_sec\":" + String(server.arg("smin").toInt() * 60) + ",";
  body += "\"week_percent\":" + server.arg("wpct") + ",";
  body += "\"week_reset_sec\":" + String(server.arg("wday").toInt() * 86400L);
  body += "}";

  HTTPClient http;
  http.setTimeout(60000);
  if (!http.begin(base + "/calibrate")) {
    redirect("/?msg=Cannot%20reach%20service");
    return;
  }
  http.addHeader("Content-Type", "application/json");
  const int code = http.POST(body);
  http.end();

  redirect(code == 200 ? "/?msg=Calibrated" : "/?msg=Calibration%20failed");
}

void handleCompanion() {
  settings::setCompanionUrl(server.arg("url").c_str());
  // An unchecked box is simply absent from the POST, which is how HTML forms
  // report false.
  settings::setAutoSync(server.hasArg("auto"));
  redirect("/?msg=Saved");
}

void handleDevice() {
  if (server.hasArg("name")) settings::setDeviceName(server.arg("name").c_str());
  if (server.hasArg("tz")) {
    settings::setUtcOffsetMinutes(server.arg("tz").toInt());
  }
  redirect("/?msg=Saved");
}

void handleNotFound() {
  // Every stray request becomes the config page. This is what turns a captive
  // portal from "find the IP address" into "the page just appears".
  redirect("/");
}

}  // namespace

void begin() {
  if (active) return;

  server.on("/", HTTP_GET, handleRoot);
  server.on("/wifi/add", HTTP_POST, handleWifiAdd);
  server.on("/wifi/forget", HTTP_GET, handleWifiForget);
  server.on("/task/add", HTTP_POST, handleTaskAdd);
  server.on("/task/toggle", HTTP_GET, handleTaskToggle);
  server.on("/task/delete", HTTP_GET, handleTaskDelete);
  server.on("/rem/add", HTTP_POST, handleReminderAdd);
  server.on("/rem/toggle", HTTP_GET, handleReminderToggle);
  server.on("/rem/delete", HTTP_GET, handleReminderDelete);
  server.on("/link/set", HTTP_POST, handleLinkSet);
  server.on("/link/add", HTTP_POST, handleLinkAdd);
  server.on("/link/delete", HTTP_GET, handleLinkDelete);
  server.on("/note/wav", HTTP_GET, handleNoteWav);
  server.on("/note/txt", HTTP_GET, handleNoteTxt);
  server.on("/note/delete", HTTP_GET, handleNoteDelete);
  server.on("/notes/all.txt", HTTP_GET, handleAllTranscripts);
  server.on("/calibrate", HTTP_POST, handleCalibrate);
  server.on("/companion", HTTP_POST, handleCompanion);
  server.on("/device", HTTP_POST, handleDevice);
  server.onNotFound(handleNotFound);

  server.begin();
  active = true;
  Serial.println("[web] config app listening on :80");
}

void stop() {
  if (!active) return;
  server.stop();
  active = false;
}

void update() {
  if (active) server.handleClient();
}

bool running() { return active; }

}  // namespace webapp
}  // namespace services
