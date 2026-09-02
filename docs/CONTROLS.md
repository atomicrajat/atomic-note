# atomic note — controls

Two buttons: **A** on top, **B** below.

## The whole grammar

| Gesture | Meaning |
|---|---|
| Tap A | Previous |
| Tap B | Next |
| Hold A | Select / confirm |
| Hold B | Back |

That is all of it. Every screen is built from these four and nothing else.

**Reverse navigation is on a tap.** Previously stepping backwards meant holding
B, so overshooting an entry in a long menu cost either a slow gesture or a wrap
all the way round. Moving in both directions has to be equally cheap, because
you are equally likely to want either.

## The two exceptions, both on the clock screen

The dashboard is the root: there is nothing to select and nothing to go back
to, so both holds are free.

| Gesture | Meaning |
|---|---|
| Hold A | Record a voice note, for as long as you hold |
| Hold B | Sleep |
| Tap either | Open the menu |

## What select means on each screen

Confirm is always hold A; what it confirms depends on where you are.

| Screen | Hold A |
|---|---|
| Menu / notes / tags | Open |
| Tasks | Mark done |
| Calendar | Jump back to today |
| Focus timer | Start / pause / resume |
| Ask | Hold and speak a question, answered from your own notes |
| Notes → note | Play / stop |
| Recording → tag | Save |
| WiFi | Connect / disconnect |
| Usage | Refetch |

Taps step through whatever the screen holds — menu entries, months, pages of a
transcript, presets. On a screen with only two views, either tap flips between
them.

## Voice answers

Off by default. On the Ask screen a tap toggles it, and the badge shows the
state. Speech is synthesised by the companion service on the laptop.

The toggle does more than mute. The device tells the companion whether an
answer is going to be read out, and the model writes differently when it is —
conversational rather than clipped, because the terse phrasing that makes a
200px screen readable sounds like a station announcement out loud.

## Tags

Six, cycled with a tap after recording. The tag is not just a label — it
decides how the companion reads what you said.

| Tag | Becomes |
|---|---|
| Note | A general observation |
| Idea | The idea, what it depends on, the next step |
| Task | Checkboxes |
| Buy | A shopping list — things you have **not** bought yet |
| Work | People, projects, decisions, commitments |
| Expense | A ledger row: amount, shop, how you paid, what you got |
| Books | Quotes and learnings, filed on the book's own page |

Buy and Expense are the pair worth getting right. Buy is something you want;
Expense is money already gone. Nothing downstream can tell them apart from the
words, so the choice is yours at the moment of tagging.

## Apps

Features that need something plugged in. They live in their own list because
the main menu is what the device always does, and this is what it can do
*today*, given what is attached.

| App | Needs | Gesture |
|---|---|---|
| Dice | MPU6050 | Shake hard to roll; hold A also rolls |
| Measure | VL53L0X | Live distance; hold A freezes, tap changes units |

**Measure** is live by default — point it and read the number. Holding A
freezes the reading so it can be written down, and holding A again resumes with
a fresh measurement rather than the stale one. A tap cycles mm / cm / inch /
feet in either state, converting the stored millimetres rather than
re-measuring, so switching units after freezing works.

A frozen reading says **HELD** in an inverted chip. Mistaking a frozen number
for a live one is the only way to misread this screen badly, so it is stated
rather than implied.

A row whose hardware is missing is **struck through**, not hidden. A menu that
silently changes length is confusing; one where a row comes alive when you plug
a sensor in explains itself.

Dice is the first thing on this device that takes an *input* rather than
showing a readout. Two buttons can confirm and step; they cannot express "roll
it", and a shake can — which is most of the argument for the sensor.

**Shake thresholds are measured, not fixed.** Opening the screen samples what
the sensor reads lying still and sets the trigger relative to that. The part in
this device reads 1.11 g at rest rather than 1.00 — ordinary uncalibrated
scale error, and it varies between parts, so a hard-coded threshold would mean
a slightly different gesture on every board.

One shake is one roll: after rolling, the device has to come back to rest for
400 ms before another can trigger, or a single vigorous shake rolls five times.

## Feedback

A panel refresh takes half a second, so nothing visual can acknowledge a press
in time to feel responsive. Two channels cover that instead:

| | Tap | Hold A | Hold B | Reminder |
|---|---|---|---|---|
| Sound | click | select | back | alert |
| Vibration | 90 ms | 120 ms | 100 ms | 3 × 200 ms |

They are **separate settings**, not one falling back to the other. The useful
combination is often sound off and vibration on — in a meeting, or with the
device in a pocket — and tying them together would make that unreachable.

Reminders ignore both preferences, exactly as before: an alert is the one
thing you asked to be interrupted by.

Nothing detects whether a motor is attached. Driving a pin with nothing on it
costs a few microamps and is otherwise silent, so there is nothing to
configure.

Those durations were set by sweeping this motor, not guessed. The first
attempt used 12 ms and 25 ms — chosen by analogy with the audio cues, which
are 10–60 ms — and could not be felt at all. A motor is not a speaker: the
eccentric mass has to spin up, which takes tens of milliseconds before there
is anything to feel. Intensity is not separately adjustable, because the pin
is driven on or off, so a longer pulse is the only way to make a stronger one.

`buzz [ms]` on the serial console pulses the motor directly, ignoring the
preference — which is how these were found, and how to tell "too short" from
"not wired up". It needs `config::kDevTools = true`; the bring-up commands are
compiled out of a release build.

## The menu

Ordered by how often it is reached, not by when it was built. Every row costs
a press to pass and a tap on the dashboard lands on the first one, so on a
two-button device the order *is* the interface.

```
Notes · Sync · Agenda · Ask          what you use daily
Focus Timer · Status Sign · My Links · Tasks · Calendar
Claude Usage · WiFi · Settings · Buttons
```

## Settings

Read-only where it has to be, editable where two buttons can manage it.

| Section | Shows |
|---|---|
| Storage | Card free %, space, and what the recordings themselves take |
| Sensors | What answered on the I2C bus, and what each is *for* |
| Options | Sound, Volume, Vibrate, Read aloud, Auto sync |

Hold A flips a toggle. **Volume** is not a toggle — it steps through 50 / 65 /
75 / 85 / 100% and plays a cue at the new level, because choosing a volume you
cannot hear is guesswork. It applies to everything the speaker does: spoken
answers, note playback and the UI cues all come out of the same DAC, and a
device with three separate loudness settings is one nobody adjusts.

The default is **85%**, up from the codec's 70 — a spoken answer at 70 is
audible on a desk and not much more, and the thing you most want to hear from
across a room is the one thing the screen cannot show you.
| Power | Battery, voltage, free heap and PSRAM |

Everything needing text — WiFi, tags, links, the companion URL — stays in the
web app, because two buttons cannot type and a worse version of a page that
already works is not worth building.

Sensors are **detected, not configured**: the bus is probed and whatever
answers is listed. An address shared by several parts is reported as
candidates rather than guessed at.

## What the device shows after recording

Not the transcript. The companion structures each note and returns its title
and summary, which is what you actually want when glancing at something you
said last week; a raw transcript on a 200×200 panel is a wall of unpunctuated
speech. The full text is kept in the vault, at the bottom of the note.

For an **Expense** it shows the parsed figure instead — `₹450 · third wave
coffee · UPI · two cold brews` — because that is a confirmation, not a summary.
A wrong amount recorded silently is worse than no record, and the shop is still
in sight while you are looking at it.
