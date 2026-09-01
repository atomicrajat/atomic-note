# atomic note — roadmap

Guiding constraint: **two buttons and a 200×200 e-paper panel.** Everything is
shaped by that. Text entry happens in the web app, because two buttons cannot
do it and never will.

Numbers in brackets map to the original feature list.

---

## Done

### Phase 0 — Foundation ✅
arduino-cli toolchain; battery latch and power rails; SSD1681 driver with
automatic full-refresh cadence; `Canvas` (1bpp framebuffer, shapes, text, word
wrap, scaling, invert ink); non-blocking two-button input; `Screen`/`Router`
with a back-stack and dirty-flag repaint; deep sleep with wake-reason routing.

### Phase 1 — Dashboard ✅ `[5] [15]`
PCF85063 RTC; SHTC3 temperature and humidity *(a chip the reference firmware
never used)*; battery gauge; HUD-framed dashboard with drawn numerals.

### Phase 2 — Menu and offline apps ✅ `[3] [6] [12]`
Generic `Menu`; QR links; focus timer with pause/reset and a draining ring;
status banner; on-device Buttons help screen.

### Phase 3 — Storage, tasks, calendar ✅ `[7] [10]`
SD card with atomic writes; task list with on-device triage; month calendar.

### Phase 5 — Network and web app ✅ `[11] [13]` + reminders `[7]`
WiFi with multiple saved networks; captive-portal provisioning; config web app
(tasks, reminders, links, WiFi, device name, timezone); NTP; reminders with
scheduled wake, verified to 3 s.

### Phase 4 — Audio ✅ `[1] [14]`
ES8311 driver written from the datasheet; UI sounds; push-to-talk recording
(hold A on the dashboard); tag selection; tag-filtered note browser with
playback.

---

## Remaining

### Phase 6 — Companion service ✅ `[2] [4]`
Transcription (faster-whisper), Claude usage with calibration, calendar feed,
and a voice chat (`Ask`) backed by a locally-run Ollama model started on
demand. Answers can be read aloud — macOS `say`, re-emitted as a canonical
44-byte-header WAV because the device's player does not walk RIFF chunks.

Controls are written down in [CONTROLS.md](CONTROLS.md).

<details><summary>Original plan and the Admin API caveat</summary>

### Phase 6 — Companion service `[2] [4]`
Both features need a secret held somewhere safer than an ESP32, so one small
service on a laptop or Pi holds every credential and the device holds none.

- **Transcription** `[2]` — device POSTs the WAV; behind that endpoint, either
  `whisper.cpp` locally or a cloud API. The device does not care which.
- **Claude usage monitor** `[4]` — see the caveat below.

**Caveat on `[4]`:** the endpoints are real —
`/v1/organizations/usage_report/messages` and `/v1/organizations/cost_report` —
but they need an **Admin API key**, and the docs state that the Admin API is
unavailable for individual accounts. Works with a Claude Console organisation
and admin rights; with only a personal Pro/Max subscription there is no
supported API, and the fallback is to show API usage or local Claude Code
stats. The admin key must never reach the device: it can manage users and
workspaces.

</details>

### Phase 7 — Knowledge base ✅
Voice notes stop being recordings and become a searchable body of notes.

- **Better ears.** `distil-large-v3` with VAD instead of `base.en`. Everything
  downstream now reads this text rather than a person, so a dropped proper noun
  costs a wrong title and a note that cannot be found again, not a moment's
  squinting.
- **The tag does real work.** It is chosen seconds after speaking, which makes
  it the best signal in the system about what a note is *for* — so it selects
  the extraction prompt, not just the folder. Buy becomes a list; Work becomes
  people and commitments.
- **Obsidian is the database.** `Notes/`, `Topics/`, `Daily/` under one folder
  in the vault. Plain markdown: greppable, diffable, already syncs to a phone,
  and outlives this project. Topic pages accumulate backlinks, which is what
  makes it a knowledge base rather than a pile.
- **Notion mirror.** One-way, queued, retried on disk. Never on the request
  path — the recording in front of you is not contingent on someone else's
  service being up.
- **Ask answers from your own notes.** Hybrid retrieval: BM25 for the proper
  nouns and part numbers, embeddings for the paraphrase, merged with
  reciprocal rank fusion. Embeddings optional; without them it degrades to
  keyword search rather than failing.
- **A voice worth listening to.** Kokoro-82M instead of `say`, and — separately
  — a spoken register for the answer itself, because text written for a 200px
  panel sounds curt when a voice reads it.

Written up in [KNOWLEDGE.md](KNOWLEDGE.md).

### Phase 7.5 — Expenses ✅
A sixth tag, **Expense**, distinct from Buy: Buy is intent, Expense is a
record. Amount, merchant, payment medium and what was received are pulled out
into `Expenses/ledger.jsonl` in the vault, with a generated monthly page and a
Notion mirror with a real currency column.

Spending questions do **not** go through retrieval. Every aggregate is computed
from the ledger and handed to the model as fact, because a local model asked to
add up retrieved amounts gets it wrong — measured at ₹3,347 against a true
₹2,448 — and states it confidently. See [KNOWLEDGE.md](KNOWLEDGE.md).

### Phase 7.6 — Books, Settings, menu order ✅
- **Books tag** — quotes and learnings, filed per book. Quote attribution is
  gated in code on an explicit spoken marker, because the model reliably
  returned paraphrase as quotation.
- **Settings screen** — storage, detected sensors, toggles, power. Built on a
  row model rather than a fixed layout precisely so the sensor work below can
  add rows without redrawing the screen.
- **Sensor detection** — the I2C bus is probed and whatever answers is
  reported, with what it is FOR. Detection only; enabling comes next.
- **Menu reordered** — Notes, Sync, Agenda, Ask first. Every row costs a press
  to pass, so the order is the interface.

### Phase 7.7 — Haptics, discard ✅
- **Vibration on GPIO 3**, mirroring the sound cues. A separate preference
  from sound rather than a fallback, because sound-off-buzz-on is the
  combination people actually want. Driven low before sleep so a motor is
  never left energised.
- **Discard while tagging** — the last position in the tag carousel, drawn
  outlined rather than filled. This also fixed a leak: holding B during
  tagging abandoned the note but left its WAV on the card, unindexed and
  invisible.

### Phase 7.8 — Two-way Notion sync ✅
Expenses now sync in both directions: edit an amount, merchant, medium or
category in Notion and it lands in the ledger; edit the ledger and it lands in
Notion. Deletions propagate both ways. Notes remain a one-way mirror, because
merging prose two sides have edited is the case with no correct answer.

Deliberate choices: conflicts prefer Notion and log what they overwrote;
deletions need prior state, so a lost state file costs a duplicate rather than
a deleted ledger. Written up in [KNOWLEDGE.md](KNOWLEDGE.md).

### Phase 7.9 — Threaded notes ✅
Notes that follow on from each other are linked at capture: a later note that
resolves, corrects or continues an earlier one marks it and adds a forward
link, without editing the original. Retrieval follows the thread forward and is
told which note is current, so a question about a fixed problem is answered
with the fix and the history.

Linking is gated on both a similarity floor and an explicit model judgement,
because a wrong link silently changes every future answer.

### Phase 8 — Sensors ✅ (first app)
`services/motion.h` drives an MPU6050 on the shared I2C bus, and an **Apps**
menu offers what the attached hardware makes possible — struck through when it
does not. First app: **Dice**, rolled by shaking.

Shake thresholds are relative to a rest measurement taken when the screen
opens, because an uncalibrated part reads several percent off 1 g and a fixed
threshold would mean a different gesture on every board.

**Measure** followed: a VL53L0X tape measure, live with a freeze and unit
switching. Its driver is the one place a library is used rather than a
datasheet implementation — a VL53L0X needs SPAD calibration and about eighty
undocumented register writes, and a hand-rolled subset returns numbers that are
merely wrong.

Remaining: gesture sensing, and a per-sensor toggle in Settings if one ever
needs disabling.

### Phase 9 — Extras `[8] [9]`
- **Mini game** `[8]` — partial refresh is ~500 ms, so real-time play is out.
  Turn-based works: 2048 maps cleanly onto two buttons.
- **Notion task sync** `[9]` — the mirror exists; syncing *tasks* back the
  other way does not, and would need the conflict resolution the one-way
  mirror was designed to avoid.
- **Sensor apps** — detection already ships (Settings → Sensors). What remains
  is the other half: a toggle per detected sensor, and an Apps section whose
  contents depend on what is attached — orientation and motion from an IMU,
  proximity from a range finder, gestures from an APDS9960.

### Not yet scheduled
- **Battery life measurement.** Blocked on the dev flags below.
- **32 kHz audio**, if 16 kHz proves too coarse. Needs one clock-coefficient
  row and doubles file size.
- **BLE**, if the web app ever proves insufficient. Deferred deliberately:
  WiFi + BLE together is a real RAM cost and the captive portal removes the
  reason to need it.

---

## ⚠ Development flags still enabled

Both in `src/config.h`. **`kDevKeepAwake` in particular will make idle power
look terrible** — the device never sleeps, so the reminder hop schedule only
runs when sleep is triggered manually.

| Flag | Effect |
|---|---|
| `kDevKeepAwake` | Device never idle-sleeps, so USB never drops |
| `kDevVerbose` | Heartbeat and panel-timing logs |

Turn both off before measuring battery life or building anything for real use.

---

## What the reference taught us, and where we diverged

`027 Pala Note/` is a third-party firmware for the same board, used for
reference only — hardware facts and approach, never code. Checking it caught
three real mistakes during the audio phase alone:

| Their approach | Why it matters |
|---|---|
| Mic gain in `REG16` (analogue), `REG17` left at 0 dB | Digital gain amplifies the noise floor and clips — audible as crackle |
| Capture loop does *nothing* but read/convert/write | A repaint blocks ~500 ms and overruns the DMA, dropping audio |
| Placeholder WAV header, patched on stop | Length is unknown until recording ends |
| Minimum recording duration | A stray tap otherwise leaves a fragment on the card |
| Sounds disabled during capture | Mic and speaker are millimetres apart |

Deliberate departures:

| Reference | Ours |
|---|---|
| Busy-waits up to 600 ms in `loop()` to classify a long press | Polled state machine, returns immediately |
| Partial refresh forever; ghosting accumulates until reboot | Full refresh every 40 partials |
| One `if/else` chain over a 13-state enum | `Screen` objects with a router and back-stack |
| Credentials compiled into the binary | Captive-portal provisioning, secrets in NVS |
| Comma-separated records with free text in the middle | Tab-separated with text last — a comma in a tag corrupted theirs |
| SHTC3 present but unused | Drives the dashboard |
