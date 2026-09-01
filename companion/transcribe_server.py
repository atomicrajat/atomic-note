#!/usr/bin/env python3
"""Transcription service for Atomic Note.

The device records to its SD card and POSTs each WAV here; this returns the
text, which the device stores alongside the recording and displays.

Why this lives on a laptop rather than the device: the smallest open-vocabulary
speech model (Whisper tiny, 39M parameters) is about 39 MB quantised, and the
ESP32-S3 has 8 MB of PSRAM. Memory is a hard wall, not a tuning problem. Doing
it here also means the backend can change without reflashing anything.

Protocol, deliberately minimal so the firmware needs no JSON parser:

    POST /transcribe
    Content-Type: audio/wav
    X-Atomic-Tag: Work            the tag chosen on the device
    X-Atomic-Note: 12             the device's recording number
    X-Atomic-Duration: 34000      milliseconds
    <raw WAV bytes>

    200 -> a short digest of the structured note, as plain text
    4xx/5xx -> a short error message, as plain text

    GET  /health        -> "ok" plus the active backends
    GET  /kb-status     -> knowledge base counts and mirror state
    GET  /claude-usage  -> daily Claude Code token totals, one per line
    GET  /claude-limits -> current 5-hour and weekly window usage
    GET  /calendar      -> upcoming events from the configured iCal feeds

    POST /ask        text question -> plain text answer
    POST /ask-voice  WAV question  -> "question\\n---\\nanswer"
    POST /recall     text question -> answer, then sources, from the vault only
    POST /reindex    -> rebuild the retrieval index
    POST /speak      text -> 16 kHz mono WAV, spoken

    /ask and /ask-voice are answered FROM THE USER'S OWN NOTES first. Both
    accept `X-Atomic-Speak: 1`, which switches the model to a spoken register
    — the same answer written to be heard rather than read.

Backends, chosen with ATOMIC_BACKEND:

    faster-whisper (default)  local, private, no API key
    openai                    needs OPENAI_API_KEY

Run:
    pip install faster-whisper
    python3 companion/transcribe_server.py

    # or, using the API instead
    ATOMIC_BACKEND=openai OPENAI_API_KEY=sk-... python3 companion/transcribe_server.py
"""

import datetime
import glob
import http.server
import json
import os
import re
import socket
import socketserver
import sys
import tempfile
import threading
import time
import uuid

# The assistant, the knowledge base, retrieval and speech each live in their
# own module now. This file is the HTTP surface and nothing else: it decides
# what an endpoint means, and the modules decide how it is done.
#
# `llm` owns the Ollama process, so nothing else may start or stop it.
import books
import expenses
import knowledge
import llm
import notion_sync
import recall
import speech
import sync
import threads

PORT = int(os.environ.get("ATOMIC_PORT", "8710"))

BACKEND = os.environ.get("ATOMIC_BACKEND", "faster-whisper")
# distil-large-v3 rather than base.en. The difference is not subtle on the
# input this actually gets: a voice note is spoken quickly, off-mic, into a
# device held at arm's length, and base.en drops proper nouns and numbers —
# exactly the words a note is about. The distilled model is within a point of
# large-v3's accuracy at roughly six times its speed, which on an M-series Mac
# is a couple of seconds for a thirty-second note.
#
# Everything downstream is now reading this text rather than a human being, so
# an error no longer costs a moment's confusion; it costs a wrong title, a
# wrong topic page, and a note that cannot be found again.
MODEL_NAME = os.environ.get("ATOMIC_MODEL", "distil-large-v3")

# A voice note is short. This is a guard against a runaway upload, not a
# meaningful limit — a minute of 16 kHz mono is about 2 MB.
MAX_UPLOAD_BYTES = 32 * 1024 * 1024

# How often to poll Notion for edits made there. A capture syncs immediately;
# this is only for changes this side cannot be told about.
SYNC_INTERVAL_SECONDS = int(os.environ.get("ATOMIC_SYNC_INTERVAL", "300"))

_model = None
_model_lock = threading.Lock()

# Where Claude Code keeps its session transcripts. Each assistant turn carries a
# `usage` object with the token counts, so the device's usage monitor reads
# these rather than the Admin API — that API needs an organisation admin key
# and is unavailable to individual accounts.
CLAUDE_PROJECTS = os.path.expanduser("~/.claude/projects")

# Per-million-token prices, used only for the rough cost line. Cache reads bill
# at a tenth of input and cache writes at 1.25x, which is where the multipliers
# below come from.
PRICES = {
    "opus": (5.0, 25.0),
    "sonnet": (3.0, 15.0),
    "haiku": (1.0, 5.0),
}


def price_for(model):
    name = (model or "").lower()
    for key, value in PRICES.items():
        if key in name:
            return value
    return PRICES["opus"]


def collect_claude_usage(days=7):
    """Aggregate Claude Code token usage per local day.

    Reads every session file rather than tracking state, because sessions are
    appended to over time and there is no reliable "since" marker. It is a few
    hundred files of JSONL at worst, which is fast enough to do per request.
    """
    today = datetime.date.today()
    oldest = today - datetime.timedelta(days=days - 1)

    per_day = {oldest + datetime.timedelta(days=i): 0 for i in range(days)}
    cost_by_day = {day: 0.0 for day in per_day}
    models = {}
    turns = 0

    for path in glob.glob(os.path.join(CLAUDE_PROJECTS, "*", "*.jsonl")):
        try:
            with open(path, "r", errors="replace") as handle:
                for line in handle:
                    try:
                        record = json.loads(line)
                    except ValueError:
                        continue

                    message = record.get("message")
                    if not isinstance(message, dict):
                        continue
                    usage = message.get("usage")
                    if not isinstance(usage, dict):
                        continue

                    stamp = record.get("timestamp")
                    if not stamp:
                        continue
                    try:
                        when = datetime.datetime.fromisoformat(
                            stamp.replace("Z", "+00:00")
                        ).astimezone().date()
                    except ValueError:
                        continue
                    if when not in per_day:
                        continue

                    inp = usage.get("input_tokens", 0)
                    out = usage.get("output_tokens", 0)
                    cache_read = usage.get("cache_read_input_tokens", 0)
                    cache_write = usage.get("cache_creation_input_tokens", 0)

                    total = inp + out + cache_read + cache_write
                    per_day[when] += total
                    turns += 1

                    model = message.get("model", "unknown")
                    models[model] = models.get(model, 0) + total

                    in_price, out_price = price_for(model)
                    cost_by_day[when] += (
                        (inp + cache_write * 1.25) * in_price / 1_000_000.0
                        + cache_read * in_price * 0.1 / 1_000_000.0
                        + out * out_price / 1_000_000.0
                    )
        except OSError:
            continue

    return per_day, cost_by_day, models, turns


# Claude's subscription limits reset on a rolling 5-hour block, and there is a
# weekly cap on top. Neither threshold is published anywhere readable, and
# Claude Code does not persist the live figures it displays — so the ceiling is
# INFERRED from your own history: the busiest block you have ever had is taken
# as approximately the limit.
#
# This is how the community usage monitors do it, and it is honest as long as
# it is labelled: it is only a true limit once you have actually hit one. Until
# then it is just your busiest session, and the percentage will read low.
BLOCK_HOURS = 5

# Where the calibration lives. See calibrate() for why one is needed.
CALIBRATION_PATH = os.path.expanduser("~/.atomic-note-calibration.json")

# Calendar feeds, one secret iCal URL per calendar.
#
# Deliberately NOT configured from the device's web app: that page is served
# unauthenticated on the LAN, and a Google secret-iCal URL grants read access
# to the whole calendar to anyone holding it. It stays on this machine.
CALENDAR_PATH = os.path.expanduser("~/.atomic-note-calendars.json")

# Feeds are re-fetched at most this often. A calendar that changes twice a day
# does not need pulling on every glance at the device.
CALENDAR_CACHE_SECONDS = 600
_calendar_cache = {"fetched_at": 0.0, "events": []}

# Cache reads bill at a tenth of input, and the limits appear to follow the
# same shape: counting them at full weight implies a weekly allowance of over
# seven billion tokens, which is not plausible. Cache writes bill at 1.25x.
CACHE_READ_WEIGHT = 0.1
CACHE_WRITE_WEIGHT = 1.25


def weighted(inp, out, cache_read, cache_write):
    return (
        inp
        + out
        + cache_read * CACHE_READ_WEIGHT
        + cache_write * CACHE_WRITE_WEIGHT
    )


def load_calibration():
    try:
        with open(CALIBRATION_PATH) as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return None


def save_calibration(data):
    with open(CALIBRATION_PATH, "w") as handle:
        json.dump(data, handle, indent=2)


def calibrate(session_percent, session_reset_sec, week_percent, week_reset_sec):
    """Turn the figures Claude Code shows into usable limits.

    Two things cannot be recovered from the session logs alone: where the
    5-hour window boundary falls, and what the actual token ceiling is. Both
    are visible in Claude Code's own /usage display, so the user reads them off
    once and this derives the rest:

      - the window boundary comes from the reset countdown, exactly
      - the ceiling comes from (tokens in that window) / (percentage shown)

    After this the numbers are real rather than inferred, until the limits
    themselves change.
    """
    now = datetime.datetime.now(datetime.timezone.utc)
    events = load_usage_events()

    session_end = now + datetime.timedelta(seconds=session_reset_sec)
    session_start = session_end - datetime.timedelta(hours=BLOCK_HOURS)
    week_end = now + datetime.timedelta(seconds=week_reset_sec)
    week_start = week_end - datetime.timedelta(days=7)

    def total_between(start, end):
        return sum(
            weighted(e[1], e[2], e[3], e[4])
            for e in events
            if start <= e[0] < end
        )

    session_tokens = total_between(session_start, session_end)
    week_tokens = total_between(week_start, week_end)

    data = {
        "calibrated_at": now.isoformat(),
        # Anchors are stored as an offset into the cycle, so the window can be
        # rolled forward indefinitely without recalibrating.
        "session_end": session_end.isoformat(),
        "week_end": week_end.isoformat(),
        "session_limit": int(session_tokens / (session_percent / 100.0))
        if session_percent > 0
        else 0,
        "week_limit": int(week_tokens / (week_percent / 100.0))
        if week_percent > 0
        else 0,
    }
    save_calibration(data)
    return data


def ask(question, spoken=False):
    """Answer a question, from the user's own notes wherever they cover it.

    This used to be a bare call to a local model. It is now retrieval-first:
    the vault is searched, and the passages that come back are what the model
    answers from. That is the whole point of writing the notes down — a
    question about something you said last week should be answered from what
    you actually said, not from what a 7B model considers plausible.

    General questions still work. When nothing in the vault matches, the model
    is told that plainly and answers from general knowledge, so the device does
    not become useless for anything outside its own memory.
    """
    if not question.strip():
        return "I did not catch that."
    return recall.answer(question, spoken=spoken)[0]


def load_calendar_urls():
    try:
        with open(CALENDAR_PATH) as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return []
    urls = data.get("urls", [])
    return [u for u in urls if isinstance(u, str) and u.startswith("http")]


def fetch_calendar_events(days=2):
    """Upcoming events across every configured feed, soonest first.

    Recurrence is expanded properly rather than by hand: an RRULE with
    exceptions, timezone shifts and all-day handling is a great deal more
    intricate than it looks, and getting it subtly wrong shows up as a meeting
    on the wrong day.
    """
    import urllib.request

    import icalendar
    import recurring_ical_events

    now = time.time()
    if now - _calendar_cache["fetched_at"] < CALENDAR_CACHE_SECONDS:
        return _calendar_cache["events"]

    # Today and tomorrow only. A 200px screen cannot usefully show a week, and
    # the question being asked of a desk device is "what is next", not "what is
    # my month". The window runs to the END of tomorrow so late events are not
    # cut off by the current time of day.
    start = datetime.datetime.now().astimezone()
    end = (start + datetime.timedelta(days=days)).replace(
        hour=0, minute=0, second=0, microsecond=0
    )
    collected = []

    for url in load_calendar_urls():
        try:
            with urllib.request.urlopen(url, timeout=20) as response:
                raw = response.read()
            calendar = icalendar.Calendar.from_ical(raw)
            for event in recurring_ical_events.of(calendar).between(start, end):
                summary = str(event.get("SUMMARY", "(no title)"))
                location = str(event.get("LOCATION", ""))
                description = str(event.get("DESCRIPTION", ""))
                begins = event.get("DTSTART").dt
                ends_raw = event.get("DTEND")
                ends = ends_raw.dt if ends_raw is not None else None

                # All-day events come back as a plain date; give them a time so
                # everything downstream can be compared and sorted together.
                all_day = not isinstance(begins, datetime.datetime)
                if all_day:
                    begins = datetime.datetime.combine(
                        begins, datetime.time.min
                    ).astimezone()
                elif begins.tzinfo is None:
                    begins = begins.astimezone()

                if ends is not None and not isinstance(ends, datetime.datetime):
                    ends = datetime.datetime.combine(
                        ends, datetime.time.min
                    ).astimezone()
                elif isinstance(ends, datetime.datetime) and ends.tzinfo is None:
                    ends = ends.astimezone()

                def flatten(text, limit):
                    return (
                        text.replace("\t", " ")
                        .replace("\r", " ")
                        .replace("\n", " ")
                        .strip()[:limit]
                    )

                collected.append(
                    {
                        "start": begins,
                        "end": ends,
                        "summary": flatten(summary, 80) or "(no title)",
                        "location": flatten(location, 60),
                        "description": flatten(description, 160),
                        "all_day": all_day,
                    }
                )
        except Exception as exc:  # noqa: BLE001 - one bad feed must not kill the rest
            print(f"  calendar fetch failed for {url[:48]}...: {exc}", flush=True)

    collected.sort(key=lambda e: e["start"])
    _calendar_cache["fetched_at"] = now
    _calendar_cache["events"] = collected
    return collected


def format_calendar(limit=12):
    """Line-oriented, like everything else the device consumes.

        event <start> <end> <allday 0|1> \t summary \t location \t description

    Tab-separated after the numbers, because summaries and locations contain
    spaces and the device splits on the tabs rather than guessing where a field
    ends.
    """
    urls = load_calendar_urls()
    if not urls:
        return "none\n"

    lines = []
    for event in fetch_calendar_events()[:limit]:
        end = event.get("end")
        lines.append(
            "event %d %d %d\t%s\t%s\t%s"
            % (
                int(event["start"].timestamp()),
                int(end.timestamp()) if end else 0,
                1 if event["all_day"] else 0,
                event["summary"],
                event["location"],
                event["description"],
            )
        )
    if not lines:
        lines.append("empty")
    return "\n".join(lines) + "\n"


def load_usage_events():
    """Every assistant turn as (timestamp, input, output, cache_read, cache_write).

    Kept unaggregated because the limit accounting weights the four kinds of
    token differently — collapsing them to a single total here would throw away
    the distinction that matters.
    """
    events = []
    for path in glob.glob(os.path.join(CLAUDE_PROJECTS, "*", "*.jsonl")):
        try:
            with open(path, "r", errors="replace") as handle:
                for line in handle:
                    if '"usage"' not in line:
                        continue
                    try:
                        record = json.loads(line)
                    except ValueError:
                        continue
                    message = record.get("message")
                    if not isinstance(message, dict):
                        continue
                    usage = message.get("usage")
                    if not isinstance(usage, dict):
                        continue
                    stamp = record.get("timestamp")
                    if not stamp:
                        continue
                    try:
                        when = datetime.datetime.fromisoformat(
                            stamp.replace("Z", "+00:00")
                        )
                    except ValueError:
                        continue
                    events.append(
                        (
                            when,
                            usage.get("input_tokens", 0),
                            usage.get("output_tokens", 0),
                            usage.get("cache_read_input_tokens", 0),
                            usage.get("cache_creation_input_tokens", 0),
                        )
                    )
        except OSError:
            continue
    events.sort()
    return events


def block_anchor(when):
    """Start of the 5-hour block containing `when`, aligned to the hour."""
    anchor = when.replace(minute=0, second=0, microsecond=0)
    return anchor - datetime.timedelta(hours=anchor.hour % BLOCK_HOURS)


def format_claude_limits():
    """Current window usage.

        block used limit percent seconds_to_reset
        week  used limit percent seconds_to_reset
        basis <blocks observed>
        source calibrated|estimated
    """
    events = load_usage_events()
    now = datetime.datetime.now(datetime.timezone.utc)

    if not events:
        return "block 0 0 0 0\nweek 0 0 0 0\nbasis 0\nsource none\n"

    calibration = load_calibration()

    if calibration:
        # Roll the stored boundary forward to whichever window contains now.
        def current_window(end_iso, span):
            end = datetime.datetime.fromisoformat(end_iso)
            while end <= now:
                end += span
            return end - span, end

        session_start, session_end = current_window(
            calibration["session_end"], datetime.timedelta(hours=BLOCK_HOURS)
        )
        week_start, week_end = current_window(
            calibration["week_end"], datetime.timedelta(days=7)
        )

        def total_between(start, end):
            return sum(
                weighted(e[1], e[2], e[3], e[4])
                for e in events
                if start <= e[0] < end
            )

        block_used = int(total_between(session_start, session_end))
        week_used = int(total_between(week_start, week_end))
        block_limit = calibration.get("session_limit", 0)
        week_limit = calibration.get("week_limit", 0)
        block_reset = int((session_end - now).total_seconds())
        week_reset = int((week_end - now).total_seconds())
        source = "calibrated"
    else:
        # Uncalibrated fallback: fixed 5-hour grid, ceiling taken as the
        # busiest window ever seen. Both assumptions are known to be wrong —
        # measured against Claude Code's own display this reads roughly 10%
        # when the truth was 93% — so the device labels it as an estimate and
        # asks to be calibrated.
        blocks = {}
        for event in events:
            anchor = event[0].replace(minute=0, second=0, microsecond=0)
            anchor -= datetime.timedelta(hours=anchor.hour % BLOCK_HOURS)
            blocks[anchor] = blocks.get(anchor, 0) + weighted(*event[1:])

        anchor = now.replace(minute=0, second=0, microsecond=0)
        anchor -= datetime.timedelta(hours=anchor.hour % BLOCK_HOURS)
        block_used = int(blocks.get(anchor, 0))
        block_limit = int(max(blocks.values())) if blocks else 0
        block_reset = int(
            (anchor + datetime.timedelta(hours=BLOCK_HOURS) - now).total_seconds()
        )
        week_used = int(
            sum(weighted(*e[1:]) for e in events if (now - e[0]).days < 7)
        )
        week_limit = week_used
        week_reset = 0
        source = "estimated"

    def percent(used, limit):
        return min(999, int(round(100.0 * used / limit))) if limit else 0

    return (
        "block %d %d %d %d\n"
        "week %d %d %d %d\n"
        "basis %d\n"
        "source %s\n"
        % (
            block_used,
            block_limit,
            percent(block_used, block_limit),
            max(0, block_reset),
            week_used,
            week_limit,
            percent(week_used, week_limit),
            max(0, week_reset),
            len(events),
            source,
        )
    )


def format_claude_usage():
    """Plain, line-oriented, so the firmware needs no JSON parser.

        days <n>
        day <label> <tokens> <cents>
        total <tokens> <cents>
        top <model> <tokens>
    """
    per_day, cost_by_day, models, turns = collect_claude_usage()
    ordered = sorted(per_day)

    lines = ["days %d" % len(ordered)]
    for day in ordered:
        lines.append(
            "day %s %d %d"
            % (day.strftime("%a"), per_day[day], round(cost_by_day[day] * 100))
        )

    total_tokens = sum(per_day.values())
    total_cents = round(sum(cost_by_day.values()) * 100)
    lines.append("total %d %d" % (total_tokens, total_cents))
    lines.append("turns %d" % turns)

    if models:
        top = max(models, key=models.get)
        lines.append("top %s %d" % (top, models[top]))
    return "\n".join(lines) + "\n"


def load_local_model():
    """Load faster-whisper once, on first use.

    Deferred rather than loaded at startup so the server answers /health
    immediately and the several-second model load happens on the first real
    request, where the device is already waiting anyway.
    """
    global _model
    with _model_lock:
        if _model is not None:
            return _model
        from faster_whisper import WhisperModel

        print(f"loading {MODEL_NAME} (first run downloads it)...", flush=True)
        # int8 on CPU: roughly 4x faster than float32 with no accuracy loss
        # worth hearing on speech this short.
        _model = WhisperModel(MODEL_NAME, device="cpu", compute_type="int8")
        print("model ready", flush=True)
        return _model


def transcribe_local(wav_path):
    model = load_local_model()
    segments, _info = model.transcribe(
        wav_path,
        beam_size=5,
        language="en",
        # The proper nouns this user actually says — shops from the ledger,
        # plus anything in ~/.atomic-note-vocabulary.txt. Whisper conditions
        # its decoding on this, which is the difference between "Croma" and
        # "Chrome". Applied to every note, not just expenses: a mis-heard
        # project or person's name is just as bad in a work note.
        initial_prompt=expenses.vocabulary_prompt() or None,
        # Voice activity detection, on. A push-to-talk recording begins and
        # ends with the sound of a thumb on a button and whatever silence the
        # user left while thinking. Whisper hallucinates fluently into silence
        # — "Thank you for watching", subtitle credits — and those inventions
        # then get structured into the knowledge base as though they were said.
        vad_filter=True,
        vad_parameters={"min_silence_duration_ms": 400},
        # Drop segments the model itself is unsure of, rather than writing them
        # down as fact.
        no_speech_threshold=0.6,
        # Whisper conditions each window on the previous one, which on a short
        # note makes one bad transcription poison the rest of it.
        condition_on_previous_text=False,
    )
    return " ".join(segment.text.strip() for segment in segments).strip()


def transcribe_openai(wav_path):
    from openai import OpenAI

    client = OpenAI()
    with open(wav_path, "rb") as handle:
        result = client.audio.transcriptions.create(
            model="whisper-1", file=handle, response_format="text"
        )
    return str(result).strip()


def transcribe(wav_path):
    if BACKEND == "openai":
        return transcribe_openai(wav_path)
    return transcribe_local(wav_path)


# ── Speaking the answer ───────────────────────────────────────────────────
# Moved wholesale into speech.py, which now picks between Kokoro (a small
# neural model that sounds like a person) and macOS `say` (which does not), and
# rewrites the text for the ear before speaking it. The device still receives
# 16 kHz mono with a canonical 44-byte header whichever engine ran.
def synthesize(text):
    return speech.synthesize(text)


def format_kb_status():
    """What the knowledge base currently holds.

        vault <path>
        indexed <files> <chunks> <embedded>
        notion <state>
        search <mode>
        spend <count> <total> this month
    """
    counts = recall.stats()
    month = expenses.totals(
        expenses.load(datetime.date.today().strftime("%Y-%m"))
    )
    return (
        "vault %s\n"
        "indexed %d %d %d\n"
        "notion %s\n"
        "search %s\n"
        "spend %d %s\n"
        "sync %s\n"
        % (
            knowledge.describe(),
            counts["files"], counts["chunks"], counts["embedded"],
            notion_sync.describe(),
            "hybrid" if counts["embedded"] else "keyword only",
            month["count"], expenses.money(month["total"]),
            "two-way every %ds" % SYNC_INTERVAL_SECONDS
            if notion_sync.expenses_enabled() else "one-way (no expenses db)",
        )
    )


def capture(transcript, tag, note_number, duration_ms):
    """Everything that happens to a transcript after it is heard.

    Structure it, write it into the vault, mirror it to Notion, and make it
    searchable. Ordered by what the user loses if a step fails: the note on
    disk is the thing that must survive, so it is written first and everything
    after it is best-effort. Notion is queued rather than called, and indexing
    a note that is already on disk can always be redone later.

    Returns the short digest the device displays.
    """
    when = datetime.datetime.now().astimezone()
    meta = {
        "device": "atomic-note",
        "note_number": note_number,
        "duration_ms": duration_ms,
    }

    if not knowledge.vault_ready():
        # No vault is a configuration problem, not a reason to lose a
        # recording. The device still gets its transcript back and keeps it on
        # the card, which is where it was going to live anyway.
        print(f"  no vault at {knowledge.VAULT}; returning transcript only",
              flush=True)
        return transcript

    # An Expense note gets a different extraction: amount, merchant, payment
    # medium and what was received are a different question from "what is this
    # note about", and asking the general extractor for them produces a
    # summary of a receipt rather than a row in a ledger.
    #
    # If the tag says Expense but nothing actually spent was described, this
    # falls through to the normal path rather than filing an empty record.
    spend = expenses.extract(transcript, when) if tag == "Expense" else []
    structured = expenses.as_structured(spend, transcript) if spend else None

    # A reading note has a source and two kinds of content — the author's
    # words and the reader's — that must not be blurred into one another.
    reading = books.extract(transcript, when) if tag == "Books" else None
    if reading:
        structured = books.as_structured(reading, transcript)

    # Does this follow up something already written down?
    #
    # Asked BEFORE the note is filed, so the link can go into its frontmatter
    # rather than being bolted on afterwards — and searched against the vault
    # as it was a moment ago, which cannot match the note being written now.
    link = None
    try:
        preview = structured or knowledge.structure(transcript, tag, when)
        link = threads.find_related(transcript, preview, when)
        if link:
            structured = preview
            thread_id = threads.thread_of(link["path"]) or uuid.uuid4().hex[:12]
            structured.setdefault("frontmatter", {}).update({
                "thread": thread_id,
                "follows": f'"[[{link["link"]}]]"',
                "relation": link["relation"],
            })
    except Exception as exc:  # noqa: BLE001
        print(f"  threading failed: {exc}", flush=True)
        link = None

    note, path = knowledge.file_note(transcript, tag, when, meta, structured)
    print(f"  filed: {path}", flush=True)

    if link:
        # And the other half: the earlier note is marked as having been
        # followed up, with a forward link. Without this, retrieving the old
        # note alone would still report a problem that has since been fixed.
        threads.mark_followed_up(link["path"], link["relation"], note["link"],
                                 note.get("summary", ""), when, thread_id)
        print(f"  linked: {link['relation']} -> {link['title']}"
              f" ({link['why']})", flush=True)

    if reading:
        # The book's own page, which accumulates across every session rather
        # than scattering one book over ten recordings.
        page = books.file_reading(reading, note["link"], when)
        if page:
            print(f"  book: {page}", flush=True)

    if spend:
        # Ledger first, then the month page it feeds. Both live inside the
        # vault, so an expense survives exactly as well as any other note.
        for page in expenses.record(spend, when, note_path=path,
                                    note_link=note.get("link", "")):
            print(f"  ledger: {page}", flush=True)
        # Notion is NOT pushed from here. sync.py owns expense pages in both
        # directions, and having two writers would race to create the same row
        # twice. The sync below picks these up, records the page ids, and is
        # idempotent — so a failed push is simply retried next time rather than
        # needing its own queue.
        if notion_sync.expenses_enabled():
            threading.Thread(target=_sync_quietly, daemon=True).start()

    if notion_sync.enabled():
        notion_sync.mirror(note, transcript, tag, when, meta)

    # In a thread: embedding a new note takes a second or two, and the device
    # is holding an HTTP connection open for the whole of this call.
    threading.Thread(target=_index_quietly, daemon=True).start()

    # For an expense the digest is a confirmation, not a summary. A wrong
    # amount recorded silently is worse than no record at all, so the parsed
    # figure goes back to the panel to be checked while the shop is still in
    # sight.
    if spend:
        return expenses.digest(spend)
    if reading:
        return books.digest(reading)
    return knowledge.digest(note, transcript)


def _sync_quietly():
    try:
        report = sync.sync_expenses()
        if any(v for k, v in report.items() if k != "skipped"):
            print(f"  notion sync: {sync.describe(report)}", flush=True)
    except Exception as exc:  # noqa: BLE001
        print(f"  notion sync failed: {exc}", flush=True)


def _index_quietly():
    try:
        recall.reindex()
    except Exception as exc:  # noqa: BLE001
        print(f"  indexing failed: {exc}", flush=True)


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        print(f"  {self.address_string()} {fmt % args}", flush=True)

    def _reply(self, code, body):
        payload = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def _wants_speech(self):
        """Is this answer going to be read aloud?"""
        return self.headers.get("X-Atomic-Speak", "0").strip() == "1"

    def _reply_bytes(self, code, payload, content_type):
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        if self.path == "/health":
            self._reply(200, f"ok {BACKEND} {MODEL_NAME}")
        elif self.path.startswith("/expenses"):
            # /expenses  or  /expenses?month=YYYY-MM
            month = None
            if "?" in self.path:
                for pair in self.path.split("?", 1)[1].split("&"):
                    key, _, value = pair.partition("=")
                    if key == "month" and re.fullmatch(r"\d{4}-\d{2}", value):
                        month = value
            try:
                self._reply(200, expenses.format_summary(month))
            except Exception as exc:  # noqa: BLE001
                self._reply(500, f"expenses unavailable: {exc}")
        elif self.path == "/kb-status":
            try:
                self._reply(200, format_kb_status())
            except Exception as exc:  # noqa: BLE001
                self._reply(500, f"status unavailable: {exc}")
        elif self.path == "/calendar":
            try:
                self._reply(200, format_calendar())
            except Exception as exc:  # noqa: BLE001
                self._reply(500, f"calendar unavailable: {exc}")
        elif self.path == "/claude-limits":
            try:
                self._reply(200, format_claude_limits())
            except Exception as exc:  # noqa: BLE001
                self._reply(500, f"limits unavailable: {exc}")
        elif self.path == "/claude-usage":
            try:
                self._reply(200, format_claude_usage())
            except Exception as exc:  # noqa: BLE001
                self._reply(500, f"usage unavailable: {exc}")
        else:
            self._reply(404, "not found")

    def do_POST(self):
        if self.path == "/calibrate":
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(length).decode("utf-8", "replace")
            try:
                fields = json.loads(raw)
                data = calibrate(
                    float(fields["session_percent"]),
                    int(fields["session_reset_sec"]),
                    float(fields["week_percent"]),
                    int(fields["week_reset_sec"]),
                )
            except Exception as exc:  # noqa: BLE001
                self._reply(400, f"bad calibration: {exc}")
                return
            print(f"calibrated: {data}", flush=True)
            self._reply(
                200,
                "session limit %d, week limit %d"
                % (data["session_limit"], data["week_limit"]),
            )
            return

        if self.path == "/speak":
            # Text in, audio out. Kept separate from /ask so the device only
            # pays for synthesis when the user has speech switched on, and so a
            # failure here cannot cost them the answer they already have.
            length = int(self.headers.get("Content-Length", "0"))
            text = self.rfile.read(length).decode("utf-8", "replace")
            try:
                wav = synthesize(text)
            except Exception as exc:  # noqa: BLE001
                self._reply(500, f"cannot speak: {exc}")
                return
            print(f"spoke {len(wav)} bytes", flush=True)
            self._reply_bytes(200, wav, "audio/wav")
            return

        if self.path == "/ask":
            # Text in, answer out. Used when the question is typed rather than
            # spoken.
            length = int(self.headers.get("Content-Length", "0"))
            question = self.rfile.read(length).decode("utf-8", "replace").strip()
            try:
                self._reply(200, ask(question, spoken=self._wants_speech()))
            except Exception as exc:  # noqa: BLE001
                self._reply(500, str(exc))
            return

        if self.path == "/recall":
            # Memory only, with its sources. Separate from /ask because this
            # one is diagnostic: when an answer looks wrong, this is how you
            # see which notes it came from.
            length = int(self.headers.get("Content-Length", "0"))
            question = self.rfile.read(length).decode("utf-8", "replace").strip()
            try:
                text, sources = recall.answer(question)
            except Exception as exc:  # noqa: BLE001
                self._reply(500, str(exc))
                return
            lines = [text, "---"]
            for source in sources:
                lines.append(f"{source['title']}\t{source['path']}")
            if not sources:
                lines.append("(nothing in the vault matched)")
            self._reply(200, "\n".join(lines) + "\n")
            return

        if self.path == "/sync":
            # Two-way, on demand. Also runs after each capture and on a timer,
            # so this is for when you have just edited something in Notion and
            # do not want to wait.
            try:
                report = sync.sync_expenses(verbose=True)
            except Exception as exc:  # noqa: BLE001
                self._reply(500, str(exc))
                return
            self._reply(200, sync.describe(report) + "\n")
            return

        if self.path == "/reindex":
            try:
                files, chunks = recall.reindex(force=True)
            except Exception as exc:  # noqa: BLE001
                self._reply(500, str(exc))
                return
            self._reply(200, f"indexed {files} files, {chunks} chunks\n")
            return

        if self.path == "/ask-voice":
            # A spoken question: transcribe it and answer in ONE round trip.
            # Two separate requests would mean the device holding a 2 MB upload
            # open while a model loads, and twice the chance of a timeout.
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 44:
                self._reply(400, "no audio")
                return
            if length > MAX_UPLOAD_BYTES:
                self._reply(413, "too large")
                return
            data = self.rfile.read(length)

            path = None
            try:
                with tempfile.NamedTemporaryFile(
                    suffix=".wav", delete=False
                ) as tmp:
                    tmp.write(data)
                    path = tmp.name
                question = transcribe(path)
            except Exception as exc:  # noqa: BLE001
                self._reply(500, f"could not hear: {exc}")
                return
            finally:
                if path and os.path.exists(path):
                    os.unlink(path)

            print(f"asked: {question}", flush=True)
            try:
                # The device tells us whether it is going to read this out. An
                # answer written for a 200px panel and one written to be spoken
                # are different texts, and using the terse one for both is what
                # made the voice sound like a station announcement.
                answer = ask(question, spoken=self._wants_speech())
            except Exception as exc:  # noqa: BLE001
                self._reply(500, str(exc))
                return
            print(f"  -> {answer[:80]}", flush=True)

            # Question first so the device can show what it heard - which is
            # what makes a wrong answer diagnosable rather than baffling.
            self._reply(200, f"{question}\n---\n{answer}")
            return

        if self.path != "/transcribe":
            self._reply(404, "not found")
            return

        length = int(self.headers.get("Content-Length", "0"))
        if length <= 44:
            self._reply(400, "no audio")
            return
        if length > MAX_UPLOAD_BYTES:
            self._reply(413, "too large")
            return

        # Read the whole body before doing anything slow, so the device is not
        # left holding a half-sent request while a model loads.
        data = self.rfile.read(length)

        seconds = (len(data) - 44) / 2 / 16000
        print(f"received {len(data)} bytes (~{seconds:.1f}s)", flush=True)

        path = None
        try:
            with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
                tmp.write(data)
                path = tmp.name
            text = transcribe(path)
        except Exception as exc:  # noqa: BLE001 - report anything to the device
            print(f"  failed: {exc}", flush=True)
            self._reply(500, f"transcription failed: {exc}")
            return
        finally:
            if path and os.path.exists(path):
                os.unlink(path)

        if not text:
            # Distinguish "heard nothing" from "went wrong": the device shows
            # this verbatim, and a blank transcript is confusing.
            self._reply(200, "(silence)")
            return
        print(f"  -> {text[:80]}", flush=True)

        # The tag was chosen on the device seconds after speaking, which makes
        # it the best signal available about what the note is for. Headers
        # rather than a multipart body: the firmware has no encoder, and three
        # header lines cost it nothing.
        tag = (self.headers.get("X-Atomic-Tag") or "Note").strip()[:20] or "Note"
        try:
            note_number = int(self.headers.get("X-Atomic-Note", "0"))
            duration_ms = int(self.headers.get("X-Atomic-Duration", "0"))
        except ValueError:
            note_number, duration_ms = 0, 0

        try:
            digest = capture(text, tag, note_number, duration_ms)
        except Exception as exc:  # noqa: BLE001
            # The transcript is what the device asked for and it is in hand.
            # Failing the request because the vault write went wrong would
            # throw away the one part that already succeeded.
            print(f"  filing failed: {exc}", flush=True)
            digest = text

        self._reply(200, digest)


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def local_ip():
    """Best guess at the address the device should be pointed at."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("8.8.8.8", 80))
        return sock.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        sock.close()


def main():
    if BACKEND not in ("faster-whisper", "openai"):
        print(f"unknown ATOMIC_BACKEND: {BACKEND}", file=sys.stderr)
        return 1

    address = f"http://{local_ip()}:{PORT}"
    print("Atomic Note companion")
    print(f"  speech-to-text : {BACKEND} ({MODEL_NAME})")
    print(f"  assistant      : {llm.describe()}")
    print(f"  voice          : {speech.describe()}")
    print(f"  knowledge base : {knowledge.describe()}")
    print(f"  notion mirror  : {notion_sync.describe()}")
    print(f"  listening on {address}")
    print(f"  put this in the device's web app: {address}")

    if not knowledge.vault_ready():
        print()
        print(f"  ! no vault at {knowledge.VAULT}")
        print("    notes will be transcribed but not filed. Set ATOMIC_VAULT.")
    print()

    # Reclaims the model's memory when the assistant goes unused.
    llm.start_watchdog()

    # Picks up anything that failed to reach Notion while this was not running.
    notion_sync.start()

    def sync_loop():
        """Two-way sync on a timer.

        A capture triggers one immediately, but an edit made in Notion has
        nothing on this side to notice it — so it is polled. Five minutes is
        chosen against Notion's rate limits rather than against impatience:
        the sync is one query plus one write per changed row, and nothing here
        is urgent enough to poll harder than that.
        """
        while True:
            time.sleep(SYNC_INTERVAL_SECONDS)
            try:
                report = sync.sync_expenses()
                if any(v for k, v in report.items() if k != "skipped"):
                    print(f"  notion sync: {sync.describe(report)}", flush=True)
            except Exception as exc:  # noqa: BLE001
                print(f"  notion sync failed: {exc}", flush=True)

    if notion_sync.expenses_enabled():
        threading.Thread(target=sync_loop, daemon=True).start()

    # Index in the background at startup rather than on the first question.
    # A cold index over an existing vault is the one genuinely slow operation
    # here, and it should not land on someone standing there holding a button.
    def warm_index():
        try:
            files, chunks = recall.reindex()
            if files:
                print(f"  indexed {files} files, {chunks} chunks", flush=True)
        except Exception as exc:  # noqa: BLE001
            print(f"  initial index failed: {exc}", flush=True)

    def warm_models():
        """Take the cold-start cost now, while nobody is waiting.

        The first note after a reboot used to pay for a speech model load, a
        language model load and possibly a voice download, all inside the
        window where someone is standing there holding a button. None of that
        needs a request to trigger it, so it does not get one.
        """
        try:
            load_local_model()
        except Exception as exc:  # noqa: BLE001
            print(f"  speech model not ready: {exc}", flush=True)
        if speech.ENGINE == "kokoro":
            try:
                speech.ensure_kokoro_files()
            except Exception as exc:  # noqa: BLE001
                print(f"  voice not ready: {exc}", flush=True)

    # Separate threads: a 350 MB voice download must not hold up indexing, and
    # neither should delay the server accepting requests.
    threading.Thread(target=warm_index, daemon=True).start()
    threading.Thread(target=warm_models, daemon=True).start()

    with Server(("", PORT), Handler) as server:
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            print("\nstopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
