#!/usr/bin/env python3
"""From a spoken fragment to a page in a knowledge base.

A raw transcript is a poor note. It is one unpunctuated paragraph with false
starts in it, it has no title, and in six months it is unfindable. What makes
it a note is structure: a name, a claim, the things to do about it, and links
to whatever else it touches. That work is mechanical enough for a local model
and tedious enough that nobody does it by hand — which is exactly the shape of
task worth automating.

**The tag does real work.** It is chosen on the device immediately after
speaking, when the intent is still fresh, and it is the strongest signal
available about what the note is FOR. "Buy milk" tagged Buy wants a checklist
item; the same words tagged Idea want a paragraph about grocery delivery. So
the tag selects the extraction prompt rather than merely being recorded as
metadata.

**Obsidian is the database.** Plain markdown in a folder: greppable, diffable,
survives this project, and already syncs to a phone. The alternative — a real
database with markdown exported from it — puts a process between the user and
their own notes for no benefit at this scale.

The knowledge base is three kinds of page, and only the first is written by
hand-equivalent capture:

    Notes/YYYY/<date>-<slug>.md   one voice note, structured
    Topics/<Topic>.md             a map of content, appended to as notes arrive
    Daily/YYYY-MM-DD.md           what was captured that day

Topic pages are what make this a knowledge base rather than a pile. A note
belongs to several, they accumulate backlinks, and Obsidian's graph does the
rest for free.
"""

import datetime
import json
import os
import re

import llm

# The vault to write into. Everything the device produces lands under one
# folder inside it, so an existing vault is never scribbled on.
VAULT = os.path.expanduser(os.environ.get("ATOMIC_VAULT", "~/AtomicNote"))
ROOT = os.environ.get("ATOMIC_VAULT_ROOT", "Atomic Note")

# Extra vaults to READ during retrieval. The device writes to exactly one
# place, but a question is worth answering from everything the user has.
READ_VAULTS = [
    os.path.expanduser(p.strip())
    for p in os.environ.get("ATOMIC_VAULT_READ", "").split(",")
    if p.strip()
]

# A topic page per proper noun would be a vault full of one-line stubs, so the
# model is capped and near-duplicates are folded into whatever page already
# exists. Growth is meant to be slow and deliberate.
#
# Three, down from four. Asked for four, the model reliably filled the fourth
# slot with something it had to reach for — a shopping list came back tagged
# "Personal Care" because coffee was on it. The last topic a model offers is
# the one it is least sure of, and in a knowledge base that is not a harmless
# extra label: it is a page.
MAX_TOPICS = 3
MIN_TOPIC_CHARS = 3

# How many existing topics to show the model. Enough to cover a real vault,
# few enough to stay a hint rather than most of the prompt.
TOPIC_HINT_LIMIT = 40


def notes_dir(when):
    return os.path.join(VAULT, ROOT, "Notes", when.strftime("%Y"))


def topics_dir():
    return os.path.join(VAULT, ROOT, "Topics")


def daily_dir():
    return os.path.join(VAULT, ROOT, "Daily")


# ── Extraction ────────────────────────────────────────────────────────────

STRUCTURE_SYSTEM = (
    "You turn a spoken voice note into a structured note for a personal "
    "knowledge base. You extract; you do not invent. Every field must be "
    "supported by the transcript. Speech is messy: ignore false starts, "
    "filler words and self-corrections, and keep what the speaker settled on. "
    "If the transcript is too short or too garbled to be worth structuring, "
    "return the title and summary and leave the lists empty. "
    "Respond with a single JSON object and nothing else."
)

# One instruction per tag, because the tag is a statement of intent and the
# same words mean different things under different ones. Anything not listed
# falls back to the general treatment.
TAG_GUIDANCE = {
    "Note": (
        "This is a general observation. Capture what was observed and why it "
        "was worth saying out loud."
    ),
    "Idea": (
        "This is an idea. State the idea itself in one clear sentence, then "
        "what it would take or what it depends on. Put the single most "
        "obvious next step in actions, and anything genuinely unresolved in "
        "questions."
    ),
    "Task": (
        "This is work to be done. Every distinct piece of work belongs in "
        "actions, phrased as an imperative starting with a verb. The summary "
        "says what the work is in service of."
    ),
    "Buy": (
        "This is a shopping list. Each item belongs in actions on its own, "
        "with quantity or specifics if they were said. Do not add items that "
        "were not mentioned."
    ),
    "Work": (
        "This is about work. Name the project, the people and the decisions. "
        "Commitments made, by anyone, belong in actions with the owner named. "
        "Put people and projects in entities."
    ),
    "Books": (
        "This is a note taken while reading. Name the book and the author in "
        "entities. Keep the author's words and the reader's own reading of "
        "them distinct: a quotation must not be paraphrased."
    ),
}

def existing_topics(limit=TOPIC_HINT_LIMIT):
    """Topic pages already in the vault, most recently touched first.

    These are shown to the model so it reuses them. Without this the vault
    diverges: the same subject comes back as "Drone Project" today and
    "Drone Hardware" next week, and a knowledge base whose index pages never
    converge is just a pile with extra steps.
    """
    directory = topics_dir()
    if not os.path.isdir(directory):
        return []
    entries = []
    for name in os.listdir(directory):
        if not name.endswith(".md"):
            continue
        path = os.path.join(directory, name)
        try:
            entries.append((os.path.getmtime(path), name[:-3]))
        except OSError:
            continue
    entries.sort(reverse=True)
    return [topic for _, topic in entries[:limit]]


STRUCTURE_PROMPT = """\
Transcript of a voice note, tagged "{tag}", recorded {when}:

\"\"\"
{transcript}
\"\"\"

{guidance}

Return JSON with exactly these keys:

  "title"      A specific title, 3 to 8 words, no trailing punctuation.
               Name the subject, not the genre: "Battery drain on the
               Jetson build", never "Voice note about a project".
  "summary"    One or two sentences, under 40 words, stating what this note
               says. Written as a claim, not as a description of a recording.
  "key_points" Up to 5 short strings. Substance only: facts, decisions,
               numbers, constraints. Omit anything already in the summary.
               Empty list if the note is a single thought.
  "actions"    Up to 6 short imperative strings, each starting with a verb.
               Only things actually to be done. Empty list if there are none.
  "questions"  Up to 3 open questions the note raises and does not answer.
               Empty list is normal and expected.
  "entities"   Up to 6 proper nouns actually named: people, projects, tools,
               companies, places.
  "topics"     Up to {max_topics} broad subject areas this note belongs
               under, each 1 to 3 words. These become shared index pages
               across many notes.
{topic_hint}\
               Fewer is better. Only include a topic the note is genuinely
               about — a shopping list that happens to mention coffee is not
               a note about food. An empty list is a valid answer.
"""


def _topic_hint(existing):
    """The existing-topics nudge, worded to converge without over-fitting.

    An earlier version said "REUSE one of these whenever one fits", and the
    model read that as a menu it had to choose from: a shopping list came back
    filed under "Work" because Work was on the list. Reuse and stretch are
    different instructions, and the difference has to be stated outright —
    saying so once, plus making "none of them" an explicitly correct answer,
    is what separates convergence from mis-filing.
    """
    if not existing:
        return (
            "               Prefer broad subjects that many future notes could\n"
            "               also sit under.\n"
        )
    return (
        "               These topics already exist. If one is genuinely what\n"
        "               this note is about, use it spelled exactly as shown:\n"
        f"                 {', '.join(existing)}\n"
        "               This is a list to match against, NOT a list to choose\n"
        "               from. If none of them is what the note is about, say so\n"
        "               by leaving them out — either name one broad new topic\n"
        "               or return an empty list. Never stretch to fit one.\n"
    )


def _clean_list(value, limit, max_chars=160):
    if not isinstance(value, list):
        return []
    out = []
    for item in value:
        if not isinstance(item, str):
            continue
        text = re.sub(r"\s+", " ", item).strip().strip("-•*").strip()
        if not text:
            continue
        if text.lower() in (existing.lower() for existing in out):
            continue
        out.append(text[:max_chars])
        if len(out) >= limit:
            break
    return out


def _fallback(transcript, tag):
    """A structured note without a model.

    Reached when the model is unreachable or returns something unparseable.
    The note still lands in the vault with a usable title and its transcript
    intact — a capture device that drops what you said because a background
    service is down has failed at the only job that matters.
    """
    flat = re.sub(r"\s+", " ", transcript).strip()
    first = re.split(r"(?<=[.!?])\s", flat)[0] if flat else ""
    title = " ".join(first.split()[:8]) or f"{tag} note"
    return {
        "title": title.rstrip(".,;:"),
        "summary": flat[:240],
        "key_points": [],
        "actions": [],
        "questions": [],
        "entities": [],
        "topics": [],
        "structured": False,
    }


def structure(transcript, tag="Note", when=None):
    """Extract a structured note from a transcript. Never raises."""
    transcript = (transcript or "").strip()
    when = when or datetime.datetime.now().astimezone()

    # Below roughly a sentence there is nothing to extract, and asking a model
    # to structure four words produces confident nonsense.
    if len(transcript) < 25:
        return _fallback(transcript, tag)

    prompt = STRUCTURE_PROMPT.format(
        tag=tag,
        when=when.strftime("%A %d %B %Y at %H:%M"),
        transcript=transcript[:6000],
        guidance=TAG_GUIDANCE.get(tag, TAG_GUIDANCE["Note"]),
        max_topics=MAX_TOPICS,
        topic_hint=_topic_hint(existing_topics()),
    )

    try:
        data = llm.generate_json(prompt, system=STRUCTURE_SYSTEM, max_tokens=800)
    except Exception as exc:  # noqa: BLE001
        print(f"  structuring failed: {exc}", flush=True)
        data = None

    if not isinstance(data, dict) or not data.get("title"):
        return _fallback(transcript, tag)

    title = re.sub(r"\s+", " ", str(data.get("title", ""))).strip().rstrip(".,;:")
    summary = re.sub(r"\s+", " ", str(data.get("summary", ""))).strip()

    return {
        "title": title[:90] or _fallback(transcript, tag)["title"],
        "summary": summary[:400],
        "key_points": _clean_list(data.get("key_points"), 5),
        "actions": _clean_list(data.get("actions"), 6),
        "questions": _clean_list(data.get("questions"), 3),
        "entities": _clean_list(data.get("entities"), 6, max_chars=60),
        "topics": _clean_list(data.get("topics"), MAX_TOPICS, max_chars=40),
        "structured": True,
    }


# ── Naming and paths ──────────────────────────────────────────────────────

def slugify(text, limit=60):
    slug = re.sub(r"[^\w\s-]", "", text.lower(), flags=re.UNICODE)
    slug = re.sub(r"[\s_]+", "-", slug).strip("-")
    return (slug[:limit].rstrip("-")) or "note"


def _canonical_topic(name):
    """Fold a topic onto an existing page when one is close enough.

    A model given the same subject twice says "Drone Project" once and "Drones"
    the next time. Without this the vault fills with near-duplicate index
    pages, which is precisely the failure that makes people abandon a
    knowledge base.
    """
    # Title-case only the words that are entirely lower case. str.title()
    # flattens every acronym it is given — "IoT" came back as "Iot", "CUDA" as
    # "Cuda" — and a topic page named wrong is one nobody will link to by hand.
    name = re.sub(r"\s+", " ", name).strip().strip("#/\\")
    name = " ".join(
        word.capitalize() if word.islower() else word for word in name.split()
    )
    if len(name) < MIN_TOPIC_CHARS:
        return None

    directory = topics_dir()
    if not os.path.isdir(directory):
        return name

    lowered = name.lower()
    for entry in os.listdir(directory):
        if not entry.endswith(".md"):
            continue
        existing = entry[:-3]
        low = existing.lower()
        if low == lowered:
            return existing
        # Singular/plural and containment: "Drone" vs "Drones", "Drone
        # Project" vs "Drone". Deliberately conservative — only when one name
        # is a whole-word extension of the other.
        if low.rstrip("s") == lowered.rstrip("s"):
            return existing
    return name


def _atomic_write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as handle:
        handle.write(text)
    os.replace(tmp, path)


def _unique_path(directory, base):
    """A free filename, without ever overwriting an existing note."""
    candidate = os.path.join(directory, base + ".md")
    if not os.path.exists(candidate):
        return candidate
    for n in range(2, 100):
        candidate = os.path.join(directory, f"{base}-{n}.md")
        if not os.path.exists(candidate):
            return candidate
    return os.path.join(directory, f"{base}-{int(datetime.datetime.now().timestamp())}.md")


# ── Rendering ─────────────────────────────────────────────────────────────

def _quote(value):
    """JSON-quote a string, keeping non-ASCII as itself.

    json.dumps escapes by default, so a title with a rupee sign in it was
    written to frontmatter as "\\u20b9450 at third wave coffee" — and that is
    what then showed up as the note's name in search results and citations.
    The file is UTF-8; there is nothing to escape it for.
    """
    return json.dumps(value, ensure_ascii=False)


def _yaml_list(values):
    return "[" + ", ".join(_quote(v) for v in values) + "]"


def render(note, transcript, tag, when, meta):
    """The markdown for one note page.

    Sections are omitted when empty rather than left as empty headings: a page
    with four blank sections on it reads as a form nobody filled in, and makes
    the notes that DO have actions harder to spot.
    """
    topics = note.get("topics", [])
    lines = [
        "---",
        f"title: {_quote(note['title'])}",
        f"tag: {tag}",
        f"created: {when.isoformat(timespec='seconds')}",
        f"topics: {_yaml_list(topics)}",
        f"entities: {_yaml_list(note.get('entities', []))}",
        "source: voice",
    ]
    if meta.get("device"):
        lines.append(f"device: {meta['device']}")
    if meta.get("note_number"):
        lines.append(f"recording: {meta['note_number']}")
    if meta.get("duration_ms"):
        lines.append(f"duration_seconds: {round(meta['duration_ms'] / 1000)}")
    if not note.get("structured", True):
        # Visible in the vault, so a page that never got a model pass can be
        # found and re-run rather than silently being the weakest note there.
        lines.append("unstructured: true")

    # Domain-specific frontmatter, supplied by whoever built the note. An
    # expense carries a total and a currency that mean nothing to a general
    # note, and that Obsidian queries should still be able to filter on.
    for key, value in (note.get("frontmatter") or {}).items():
        lines.append(f"{key}: {_quote(value)}"
                     if isinstance(value, str) else f"{key}: {value}")
    lines += ["---", "", f"# {note['title']}", ""]

    if note.get("summary"):
        lines += [f"> {note['summary']}", ""]

    # Quotes, where a caller supplied them. Rendered as real blockquotes so
    # the author's words stay visibly theirs — a quotation that looks like a
    # summary is a misattribution waiting to be made months later.
    if note.get("quotes"):
        lines.append("## Quotes")
        for quote in note["quotes"]:
            lines += [f"> {quote}", ""]

    if note.get("key_points"):
        lines.append("## Key points")
        lines += [f"- {point}" for point in note["key_points"]]
        lines.append("")

    if note.get("actions"):
        # Real checkboxes: Obsidian's task queries pick these up across the
        # whole vault, so actions spoken into the device show up wherever the
        # user already tracks tasks.
        lines.append("## Actions")
        lines += [f"- [ ] {action}" for action in note["actions"]]
        lines.append("")

    if note.get("questions"):
        lines.append("## Open questions")
        lines += [f"- {question}" for question in note["questions"]]
        lines.append("")

    # The transcript stays, always, verbatim. The structured page above it is
    # an interpretation by a small model, and the ability to check it against
    # what was actually said is what makes the interpretation trustworthy.
    lines += ["## Transcript", "", transcript.strip(), ""]

    trail = [f"[[{topic}]]" for topic in topics]
    trail.append(f"[[{when.strftime('%Y-%m-%d')}]]")
    lines += ["---", "", "Filed under " + " · ".join(trail), ""]
    return "\n".join(lines)


def digest(note, transcript):
    """What the device shows on a 200x200 panel.

    The device used to display the raw transcript, which on that screen is a
    wall of unpunctuated text. The title and summary are what a person wants
    to see when they glance at a note they made last week, so that is what
    goes back — the transcript is one section down in the vault for when it
    matters.
    """
    parts = [note["title"]]
    if note.get("summary"):
        parts.append(note["summary"])
    for action in note.get("actions", [])[:4]:
        parts.append(f"- {action}")
    if not note.get("structured", True):
        parts.append(transcript.strip()[:300])
    return "\n".join(parts).strip()


# ── Index pages ───────────────────────────────────────────────────────────

def _append_under(path, heading, line, header):
    """Append one line under `heading`, creating the page if it is absent.

    Read-modify-write on a whole file, which is fine for pages of this size and
    means the result is always a valid document. Appending blindly to the end
    would put entries below whatever the user has added underneath.
    """
    if os.path.exists(path):
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
    else:
        text = header

    if line.strip() in text:
        return  # already filed; a re-run must not duplicate entries

    if heading in text:
        index = text.index(heading) + len(heading)
        # Immediately after the heading, so newest is first and the page does
        # not have to be scrolled to see what just arrived.
        text = text[:index] + "\n" + line + text[index:]
    else:
        text = text.rstrip() + f"\n\n{heading}\n{line}\n"

    _atomic_write(path, text)


def _file_topic(topic, title, summary, when):
    path = os.path.join(topics_dir(), f"{topic}.md")
    header = (
        "---\n"
        f"title: {_quote(topic)}\n"
        "kind: topic\n"
        "---\n\n"
        f"# {topic}\n\n"
        "An index of everything captured under this subject.\n\n"
        "## Notes\n"
    )
    entry = f"- [[{title}]] — {summary}" if summary else f"- [[{title}]]"
    _append_under(path, "## Notes", entry, header)


def _file_daily(title, tag, summary, when):
    label = when.strftime("%Y-%m-%d")
    path = os.path.join(daily_dir(), f"{label}.md")
    header = (
        "---\n"
        f"title: {label}\n"
        "kind: daily\n"
        "---\n\n"
        f"# {when.strftime('%A %d %B %Y')}\n\n"
        "## Captured\n"
    )
    entry = f"- {when.strftime('%H:%M')} · **{tag}** · [[{title}]]"
    if summary:
        entry += f" — {summary}"
    _append_under(path, "## Captured", entry, header)


def _write_root_index():
    """A landing page listing the topics, rebuilt on every write.

    Regenerated rather than appended to, because it is derived entirely from
    what is on disk and a stale index is worse than none.
    """
    directory = topics_dir()
    topics = sorted(
        entry[:-3] for entry in os.listdir(directory) if entry.endswith(".md")
    ) if os.path.isdir(directory) else []

    lines = [
        "---",
        'title: "Atomic Note"',
        "kind: index",
        "---",
        "",
        "# Atomic Note",
        "",
        "Voice notes captured on the device, transcribed and structured on the",
        "laptop. Everything here is generated — edit freely, it is never",
        "overwritten except this page.",
        "",
        "## Topics",
        "",
    ]
    lines += [f"- [[{topic}]]" for topic in topics] or ["_Nothing yet._"]
    lines += ["", "## Folders", "", "- `Notes/` one page per recording",
              "- `Topics/` subject indexes", "- `Daily/` what was captured when", ""]
    _atomic_write(os.path.join(VAULT, ROOT, "_index.md"), "\n".join(lines))


# ── The whole pipeline ────────────────────────────────────────────────────

def file_note(transcript, tag="Note", when=None, meta=None, structured=None):
    """Structure a transcript and write it into the vault.

    `structured` lets a caller supply the note itself rather than having the
    general extractor produce it. Expenses use that: an amount, a merchant and
    a payment medium are a different extraction from "what is this note about",
    and the module that understands money should own it. Everything after the
    extraction — filing, topics, the daily list, the index — is the same for
    both, so it stays here rather than being written twice.

    Returns (structured_note, path). Raises only if the vault itself cannot be
    written, which is a real failure worth surfacing to the device.
    """
    when = when or datetime.datetime.now().astimezone()
    meta = meta or {}

    note = structured if structured else structure(transcript, tag, when)

    # The tag is always a topic. It is the one label chosen by a human, so it
    # is the most reliable index in the vault.
    topics = []
    for raw in [tag] + note.get("topics", []):
        canonical = _canonical_topic(raw)
        if canonical and canonical not in topics:
            topics.append(canonical)
    # MAX_TOPICS + 1: the tag is always the first, and it does not count
    # against the model's allowance because it was chosen by a person.
    note["topics"] = topics[: MAX_TOPICS + 1]

    directory = notes_dir(when)
    os.makedirs(directory, exist_ok=True)
    os.makedirs(topics_dir(), exist_ok=True)
    os.makedirs(daily_dir(), exist_ok=True)

    base = f"{when.strftime('%Y-%m-%d')}-{slugify(note['title'])}"
    path = _unique_path(directory, base)

    # The page's own filename is what wikilinks resolve against, so links are
    # built from it rather than from the title — which may have been
    # deduplicated with a numeric suffix.
    link_title = os.path.basename(path)[:-3]
    note["link"] = link_title

    _atomic_write(path, render(note, transcript, tag, when, meta))

    for topic in note["topics"]:
        _file_topic(topic, link_title, note.get("summary", ""), when)
    _file_daily(link_title, tag, note.get("summary", ""), when)
    _write_root_index()

    return note, path


def vault_ready():
    return os.path.isdir(VAULT)


def read_roots():
    """Every directory retrieval should walk."""
    roots = [os.path.join(VAULT, ROOT)]
    roots += [r for r in READ_VAULTS if os.path.isdir(r)]
    return [r for r in roots if os.path.isdir(r)]


def describe():
    state = "ready" if vault_ready() else "MISSING"
    extra = f" +{len(READ_VAULTS)} read-only" if READ_VAULTS else ""
    return f"{os.path.join(VAULT, ROOT)} ({state}){extra}"
