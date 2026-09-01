# atomic note — the knowledge base

What happens to a voice note after you stop holding the button.

The device records and tags. Everything else runs on the laptop, because the
work needs a speech model, a language model and a filesystem, and the ESP32-S3
has 8 MB of PSRAM. The device holds no credentials and no models; it POSTs a
WAV and gets back two lines to show you.

---

## The pipeline

```
  device                      companion (laptop)                    stores
  ──────                      ──────────────────                    ──────

  hold A ─▶ record
     │      tag it
     │
     └─ POST /transcribe ──▶  distil-large-v3          the words
        X-Atomic-Tag: Work      + VAD
                                    │
                                    ▼
                                structure by tag  ───▶  Obsidian vault
                                (local LLM, JSON)         Notes/ Topics/ Daily/
                                    │                          │
                                    ├──────────────────▶  Notion (queued)
                                    │
                                    └──────────────────▶  search index
                                                            FTS5 + embeddings
     ◀── title + summary ─────────────┘
```

Asking a question runs the same machinery backwards:

```
  hold A on Ask ─▶ POST /ask-voice ─▶ transcribe ─▶ search the vault
                   X-Atomic-Speak: 1                     │
                                                         ▼
                                              answer from the passages found
                                                         │
                   ◀── question + answer ─────────────────┘
                   POST /speak ─▶ Kokoro ─▶ 16 kHz WAV
```

---

## Why the tag matters

The tag is chosen on the device within seconds of speaking, while the intent is
still fresh. That makes it the single most reliable signal in the system about
what a note is *for* — better than anything a model can infer from the words
alone, because it is a statement of purpose rather than a guess at one.

So it selects the extraction prompt rather than merely labelling the page:

| Tag | What the model is asked to find |
|---|---|
| **Note** | What was observed, and why it was worth saying |
| **Idea** | The idea in one sentence, what it depends on, the next step |
| **Task** | Every distinct piece of work, as an imperative |
| **Buy** | Each item on its own, with quantities if they were said |
| **Work** | Project, people, decisions, and who committed to what |
| **Expense** | Amount, merchant, payment medium, what was received |
| **Books** | Quotes, learnings, and the book they came from |

"Get more cells" tagged **Buy** becomes a checklist item. The same words tagged
**Idea** become a paragraph about battery capacity. Same transcript, different
note, because you told it which one you meant.

**Buy and Expense are opposites, not neighbours.** Buy is intent: something
wanted, in the future, with no amount attached. Expense is a record: money
already gone, with a number. Nothing downstream can recover the difference from
the words alone — "coffee at Third Wave" is either a plan or a receipt
depending entirely on which you meant. So it is decided at the moment of
tagging, by you, in the second after you stop speaking.

---

## Expenses

An Expense note takes a different extraction — amount, merchant, payment
medium, what was received — and lands in a ledger rather than a page of prose.

```
Atomic Note/Expenses/
  ledger.jsonl    the records
  2026-08.md      a generated view of one month
```

**The numbers are JSONL, not markdown.** Everywhere else in this project the
markdown *is* the database, because prose is what markdown is for. Money is not
prose: it has to be summed, grouped and compared, and re-deriving that from a
rendered table means parsing a table — which breaks the first time someone
edits a cell. So the ledger is the record of truth and the monthly page is a
generated view of it. Still one folder, still greppable, still syncs with
everything else, just in a format that survives arithmetic.

What it handles, verified:

| Said | Recorded |
|---|---|
| "four fifty at third wave on UPI" | ₹450 · third wave coffee · UPI · Food & Drink |
| "twelve hundred at more supermarket, card" | ₹1,200 · Groceries |
| "scanned the QR at the chai tapri, 40 bucks" | ₹40 · UPI |
| "899 on my credit card and also 349, same card" | **two** rows, both Card |
| "I should buy a soldering iron sometime" | nothing — that is intent, not spend |

**The transcript decides the payment method, not the model.** Told plainly to
return nothing when no method was mentioned, the model invents one anyway:
*"spent 250 at blue tokai for coffee"* came back as **Cash**, from a sentence
naming no method at all. That is the same failure as quoting a paraphrase — a
required-looking field getting filled. So the transcript is scanned for a
payment word, and if there is none the default (`UPI`) applies. Anything else
you say out loud, which is easier than correcting a row afterwards.

Categories and payment mediums come from fixed lists. A free-text category
cannot be grouped: "food", "Food", "eating out" and "restaurant" become four
rows in a summary that should have one.

### Reading notes

A **Books** note keeps two things apart that must not blur: the author's words
and your reading of them. Notes accumulate on `Books/<Title>.md` rather than
scattering one book across ten recordings.

**A quote is only recorded when you marked one out loud** — by saying "quote",
"unquote", "the book says", "he writes". That is enforced in code, not asked
for in the prompt, because the model would not hold the line: given *"from Deep
Work, the idea that attention residue is why switching costs so much"* it
returned the speaker's own paraphrase, framing and all, as a quotation by Cal
Newport. Another came back quoting *"another one from Thinking Fast and Slow,
the anchoring effect..."* — a sentence in no book.

A misattribution is not cosmetic. It survives in the vault, gets retrieved
months later, and carries nothing to reveal itself. So if the transcript
contains no quotation marker, nothing is stored as a quote; the text is kept as
a learning, which is what it was.

## Notes that follow on from each other

You say on Monday that an install is broken. On Thursday you say you fixed it,
and how. Those are one story told twice, and filed as two unrelated pages the
question *"what was wrong with that install?"* gets answered with Monday's note
— correct about the past, wrong about now.

**The old note is not edited.** The record of what was broken is worth keeping,
and rewriting history to keep an answer current is how a notebook stops being
trustworthy. Instead the two are linked, the earlier one is marked resolved and
gains a forward link, and retrieval is told which is current. The data remains;
the answer moves on.

```
what was the issue with the ROS installation?

  -> The ROS2 installation on the Jetson was failing because the colcon
     build tool was missing and the apt repository key had expired. The
     issue was resolved on August 31st by re-adding the apt key using curl
     from the ROS archive and installing python3-colcon-common-extensions.
```

Three relations, deliberately few: **resolves**, **corrects**, **continues**.

**A link needs real evidence.** Two gates, both must pass: the candidate clears
a similarity floor higher than retrieval's own, and the model names the
relationship explicitly. A knowledge base that invents connections is worse
than one that misses them — a wrong link quietly changes every future answer.
Tested with a deliberate distractor ("the office coffee machine is broken")
between the two ROS notes; it was correctly left unlinked.

**Retrieval follows the thread forward.** Matching happens on wording, and the
wording of a problem lives in the note that reported it, not the one that fixed
it three days later. So a hit on any note in a thread also pulls in the most
recent note of that thread.

**The labels never reach the reader.** Passages are marked as earlier or
current so the model knows which is which — and it kept quoting that back
("the earlier note was outdated"), which is a remark about how retrieval works,
not an answer. The labels are now unremarkable lower case, and a stripper
removes any sentence that still talks about the notes instead of the subject.
Prompting alone did not hold, exactly as with the arithmetic.

### Asking about money

**Spending questions bypass retrieval entirely.** Not because retrieval fails
to find the right notes — it finds them — but because answering "how much" from
a pile of passages is arithmetic, and a local model does arithmetic badly
enough to be worse than useless.

That is measured, not assumed. Asked to split a month by payment method with
the notes retrieved normally, qwen3:8b answered **Card ₹3,347** against a true
₹2,448 — it had added a category total to the very rows the total was *of*.
Told plainly in the prompt not to do that, it produced *"₹2,448 (UPI) and
₹1,279 (cash) minus ₹799"* — the same wrong answer, with working shown.

So the model no longer does sums at all. Every aggregate anyone is likely to
ask for — overall, per category, per medium, per merchant — is computed from
the ledger and handed over as a fact sheet. The model picks the right line and
writes a sentence around it. Nothing to add up means nothing to add up wrongly.

```
how much of my spending was on UPI versus card?
  -> You spent ₹1,249 on UPI and ₹2,448 by card this month.
```

### Two things that are deliberately defensive

**Currency comes from words, never symbols.** Asked to transcribe "three
hundred and twenty rupees", Whisper writes `$320`. Reading that `$` as USD
would turn every rupee amount into a dollar amount, so symbols are ignored
outright and the configured currency stands unless someone actually said
"dollars" or "euros".

**Currencies never add together.** A foreign amount is summed separately and
reported alongside, because ₹3,877 + $40 is 3,917 of nothing — and a total that
is silently meaningless is the exact failure this module is arranged to avoid.

An amount that could not be heard clearly is kept and flagged ⚠️ rather than
dropped, and the device shows the parsed figure back as confirmation — a wrong
amount recorded silently is worse than no record at all.

---

## What lands in the vault

Three kinds of page, under `Atomic Note/` inside your vault. Nothing outside
that folder is ever written to.

```
Atomic Note/
  Notes/2026/2026-08-28-jetson-build-blocked-on-cuda.md
  Topics/Work.md
  Topics/Hardware.md
  Daily/2026-08-28.md
  _index.md
```

**`Notes/`** — one page per recording. Frontmatter, a title, a summary, then
whichever of key points / actions / open questions the note actually has.
Empty sections are omitted rather than left blank, because a page with four
empty headings reads as a form nobody filled in.

Actions are real markdown checkboxes, so Obsidian's task queries pick them up
across the whole vault. Something you say into the device shows up wherever you
already track tasks.

**The transcript is always kept, verbatim, at the bottom.** The structured note
above it is one small model's interpretation, and being able to check it
against what was actually said is what makes the interpretation trustworthy.

**`Topics/`** — the part that makes this a knowledge base rather than a pile.
Each note is filed under its tag plus up to four subjects, and topic pages
accumulate links as notes arrive. Near-duplicate topics are folded together
("Drone" and "Drones" share a page), because topic sprawl is exactly what kills
a vault.

**`Daily/`** — what you captured that day, in order, with times.

---

## Retrieval

Questions are answered from your own notes first. The index is SQLite, rebuilt
incrementally — a file is re-read only when its mtime or size changed, so
refreshing before every question costs a `stat` per file.

Search is **hybrid**, and deliberately so:

- **Keyword** (FTS5, BM25) is exact on proper nouns, part numbers and dates,
  and useless on paraphrase.
- **Semantic** (embeddings, cosine) finds the note about "the drone battery
  problem" when you asked about "flight time", and also confidently returns
  three unrelated notes when you asked for a specific name.

Personal notes need both — they are full of project names *and* half-remembered
ideas. The two rankings are merged with reciprocal rank fusion, which needs no
calibration between two incomparable score scales and rewards passages that
both methods liked.

**Embeddings are optional.** With no encoder installed, search falls back to
keyword only. Worse, still useful, and it means a fresh machine works before a
300 MB download finishes.

If nothing in the vault matches, the model is told that plainly and answers
from general knowledge — the device does not become useless for anything
outside its own memory.

---

## The voice

Two separate problems, and conflating them is why the first version sounded
like a train announcement.

**The engine.** Kokoro-82M, a small neural synthesiser that runs in about a
second on Apple silicon. macOS `say` is the fallback and always works.

**The words.** Text written for a 200×200 panel and text written to be heard
are not the same string. Two things happen:

1. The model is asked for a different register when the device says it is going
   to read the answer out (`X-Atomic-Speak: 1`) — contractions, varied sentence
   length, no lists, and never reading a file path or URL aloud.
2. `speech.naturalize()` rewrites what the model cannot fix about itself,
   because it is a property of the text rather than the writing: markdown
   stripped, `e.g.` spoken as "for example", `~3` as "about 3", bullets given
   full stops so the synthesiser has somewhere to breathe.

---

## Notion

**Expenses sync both ways. Notes are still a one-way mirror.**

That split is deliberate. Two-way sync is tractable exactly where a field has
one correct value — an amount, a merchant, a payment method — and breaks where
two sides have both edited prose. So the structured rows sync in both
directions and the note bodies do not.

| | Vault → Notion | Notion → Vault |
|---|---|---|
| Expense fields | ✅ | ✅ |
| Expense rows added | ✅ | ✅ adopted into the ledger |
| Expense deletions | ✅ archived there | ✅ dropped locally |
| Note pages | ✅ | ✗ |
| Note bodies | ✅ | ✗ never |

### How it decides what changed

After each sync, the agreement is written to `~/.atomic-note/sync-state.json`:
Notion's `last_edited_time` for the page, and a hash of the local row. Next
time, either side differing from that is a change *on that side*. Without it
the first run would look like everything changed at once.

**Conflicts prefer Notion**, because an edit there is someone deliberately
typing into a field while a local change is usually the device appending. The
overwritten local values go to `~/.atomic-note/sync-conflicts.log` rather than
being discarded — a sync that silently loses an edit is worse than one that
refuses to run.

**Deletions require prior state.** A row is only deleted for being missing if
there is a record of it existing on both sides. Anything else is treated as
new, so a lost state file costs a duplicate — recoverable — rather than a
deleted ledger.

### Two bugs this had, both found by testing against the real database

**The first run would have duplicated the entire ledger.** Every row written
before sync existed had no page id, so each would have been pushed as a second
copy of something already there. Rows are now paired first, on amount plus
merchant plus date.

**A local edit came back reverted.** Writing to Notion changes the page's
`last_edited_time`, and the sync was storing the value it read *before*
writing — so the next run mistook its own push for a Notion edit and pulled it
straight back. The timestamp is now taken from the write's response.

### Running it

Automatic after every capture and every 5 minutes (`ATOMIC_SYNC_INTERVAL`).
On demand:

```sh
curl -X POST localhost:8710/sync
python3 companion/sync.py --dry-run   # show what would change, touch nothing
```

Nothing about it is allowed to make the device wait. Mirroring runs on a
background worker, and anything that fails goes to a queue on disk that is
retried every five minutes and on startup. A note reaches Notion late, or after
a restart — never at the cost of the recording in front of you.

The integration token can read and write every page it is shared with, so it
lives in `~/.atomic-note-notion.json` and never goes near the device or the
unauthenticated LAN web app. Same reasoning as the calendar feeds.

There are two databases, both at the top level of the private sidebar:

```
Atomic Note   b0deedf4-a740-4432-8571-cb3c40dbd8a2   notes
Expenses      15d2e4f8-d4e8-4b37-9749-55bbe7d9f702   spending
```

Separate on purpose: an expense needs a real number column for Notion to sum
and chart, and a notes table with a mostly-empty Amount on it is a worse
version of both. Either can be configured without the other.

**Atomic Note** sits at the top level rather than under **AtomicRajat**. It deliberately sits on its own rather than under **AtomicRajat**:
that page is the content source for `atomicrajat.com`, so a voice note landing
inside it would be sitting among live website content, and anything shared
from that page would take the notes with it.

| Property | Type | |
|---|---|---|
| Name | title | The note's title |
| Tag | select | Note · Idea · Task · Buy · Work |
| Captured | date | When it was spoken |
| Summary | text | Extracted by the local model |
| Topics | multi-select | Mirrors the vault topic pages |
| Actions | number | How many to-dos it produced |
| Recording | number | The device's recording number |

What is still needed is a token, because the connector that created the
database is not something a background daemon can use. Create an internal
integration at <https://www.notion.so/my-integrations>, connect it to the
**Atomic Note** database itself (••• → Connections), then:

```sh
./companion/setup-knowledge.sh --notion
```

Paste the token and the database id above. Setup reads the database back
before saving, so a token that cannot see it fails there, with a message —
rather than silently, on the first note, hours later.

---

## Setup

```sh
./companion/setup-knowledge.sh          # packages, models, voice, vault
./companion/setup-knowledge.sh --check  # report only, change nothing
./companion/setup-knowledge.sh --notion # configure the mirror
```

Then point the device at the service in its web app, as before.

### Configuration

Everything has a working default; these are the ones worth knowing.

| Variable | Default | What it does |
|---|---|---|
| `ATOMIC_VAULT` | `~/atomicrajat` | Vault to write into |
| `ATOMIC_VAULT_ROOT` | `Atomic Note` | Folder inside it |
| `ATOMIC_VAULT_READ` | — | Extra vaults to *search*, comma separated |
| `ATOMIC_MODEL` | `distil-large-v3` | Speech-to-text |
| `ATOMIC_LLM_MODEL` | *(best installed)* | Pin the language model |
| `ATOMIC_EMBED_MODEL` | *(best installed)* | Pin the embedding model |
| `ATOMIC_TTS` | `kokoro` | `kokoro`, `say`, or `piper` |
| `ATOMIC_TTS_VOICE` | `af_heart` | Kokoro voice |
| `ATOMIC_CURRENCY` | `INR` | Default currency for expenses |
| `ATOMIC_DEFAULT_MEDIUM` | `UPI` | Payment method when the note names none |
| `ATOMIC_SYNC_INTERVAL` | `300` | Seconds between two-way Notion syncs |

**Model choice is resolved, not pinned.** Left unset, the service asks Ollama
what is installed and takes the best from a ranked list, so pulling a stronger
model is the whole upgrade. Pinning a name means breaking on a machine that
does not have it — which is a failure that stays silent until the first note.

### Endpoints

```
GET  /health        backends and models
GET  /kb-status     vault path, index counts, mirror state
POST /transcribe    WAV + X-Atomic-Tag -> the note's digest
POST /ask           text question -> answer, from your notes
POST /ask-voice     WAV question -> "question\n---\nanswer"
POST /recall        text question -> answer, then its sources
GET  /expenses      this month's totals; ?month=YYYY-MM for another
POST /reindex       rebuild the search index from scratch
POST /speak         text -> 16 kHz mono WAV
```

`/recall` is the diagnostic one: when an answer looks wrong, it shows which
notes it came from.

---

## Failure, and what it costs

Ordered by what you lose. The recording is the thing that must survive; nothing
downstream of it is allowed to threaten it.

| If this fails | You lose | You keep |
|---|---|---|
| Structuring model | Title, summary, actions | The note, filed, transcript intact, marked `unstructured: true` |
| Vault write | The page | The transcript, returned to the device and stored on its card |
| Notion | Nothing yet | Queued on disk, retried every 5 minutes |
| Indexing | Answerable *now* | Rebuilt on the next question or `/reindex` |
| Kokoro | The audio | The answer, already on screen before speech is fetched |

A capture device that drops what you said because a background service is down
has failed at the only job that actually matters.
