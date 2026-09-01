#include "transcribe.h"

#include <HTTPClient.h>
#include <SD_MMC.h>
#include <WiFi.h>

#include "network.h"
#include "notes.h"
#include "settings.h"

namespace services {
namespace transcribe {
namespace {

// The ceiling, and it is a hard one: HTTPClient::setTimeout takes a uint16_t,
// so anything above 65535 ms silently wraps. Asking for three minutes here
// gave 48.9 seconds — a shorter timeout than the value it replaced, which is
// the worst kind of wrong, because the number in the source says otherwise.
//
// 65 seconds is therefore the most the device can wait, and the companion is
// built around that rather than the other way round: it loads the speech model
// and the voice at startup instead of on the first request, so the pipeline a
// note actually meets is a warm one. Warm, that is transcribe, structure, file
// and index in about fifteen seconds.
constexpr uint16_t kTimeoutMs = 65000;

String endpoint(const char* path) {
  String base = settings::companionUrl();
  base.trim();
  if (base.length() == 0) return String("");
  while (base.endsWith("/")) base.remove(base.length() - 1);
  return base + path;
}

}  // namespace

bool reachable() {
  if (!network::isJoined()) return false;
  const String url = endpoint("/health");
  if (url.length() == 0) return false;

  HTTPClient http;
  http.setTimeout(4000);
  if (!http.begin(url)) return false;
  const int code = http.GET();
  http.end();
  return code == 200;
}

bool one(int noteIndex, String& error) {
  const notes::Note* note = notes::at(noteIndex);
  if (!note) {
    error = "no such note";
    return false;
  }
  if (!network::isJoined()) {
    error = "no network";
    return false;
  }

  const String url = endpoint("/transcribe");
  if (url.length() == 0) {
    error = "no service URL";
    return false;
  }

  File wav = SD_MMC.open(notes::wavPath(note->number).c_str(), FILE_READ);
  if (!wav) {
    error = "audio missing";
    return false;
  }
  const size_t size = wav.size();

  HTTPClient http;
  http.setTimeout(kTimeoutMs);
  if (!http.begin(url)) {
    wav.close();
    error = "bad URL";
    return false;
  }
  http.addHeader("Content-Type", "audio/wav");

  // The tag, the number and the length go with the audio.
  //
  // The tag is the interesting one. It is chosen on the device within seconds
  // of speaking, while the intent is still fresh, which makes it the best
  // signal anywhere in the system about what the note is FOR. The companion
  // uses it to choose how the transcript is read — a Buy note becomes a list,
  // a Work note becomes people and commitments — so it is no longer merely a
  // label to file under.
  //
  // Headers rather than a multipart body: the firmware has no encoder, and
  // three header lines cost nothing.
  http.addHeader("X-Atomic-Tag", note->tag);
  http.addHeader("X-Atomic-Note", String(note->number));
  http.addHeader("X-Atomic-Duration", String(note->durationMs));

  // Stream from the card rather than reading the file into RAM. A minute of
  // audio is about 2 MB, which would not fit in internal memory and would be a
  // waste of PSRAM when the socket can be fed directly.
  const int code = http.sendRequest("POST", &wav, size);
  wav.close();

  if (code != 200) {
    error = code > 0 ? String("service said ") + String(code)
                     : String("cannot reach service");
    http.end();
    return false;
  }

  const String text = http.getString();
  http.end();

  if (text.length() == 0) {
    error = "empty reply";
    return false;
  }
  if (!notes::setTranscript(noteIndex, text)) {
    error = "cannot write to card";
    return false;
  }
  return true;
}

}  // namespace transcribe
}  // namespace services
