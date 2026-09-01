#!/usr/bin/env python3
"""Connecting notes that are about the same thing over time.

You say on Monday that an install is broken. On Thursday you say you fixed it,
and how. Those are one story told twice, and a knowledge base that files them
as two unrelated pages will answer "what was wrong with that install?" with the
Monday note — correct about the past, wrong about now.

The fix is NOT to edit Monday's note. The record of what was broken is worth
keeping, and rewriting history to keep an answer current is how a notebook
stops being trustworthy. Instead the two are linked, Monday's note is marked as
having been resolved, and retrieval is told which of the two is current. The
data remains; the answer moves on.

**A link is only made when there is real evidence for one.** Two gates, and
both must pass: the candidate has to clear the same similarity floor retrieval
uses, and the model has to name the relationship explicitly. A knowledge base
that invents connections is worse than one that misses them — a wrong link
quietly changes what every future answer says.

Relations, deliberately few:

    resolves    the earlier problem is fixed, and this says how
    continues   more about the same thing, nothing settled
    corrects    the earlier note was wrong; this supersedes it
"""

import datetime
import json
import os
import re

import knowledge
import llm
import recall

# How many earlier notes to consider. Retrieval is already ranked, and asking
# the model about a long list makes it more likely to force a connection.
CANDIDATES = 4

# A candidate must be at least this similar before it is even shown to the
# model. Higher than the retrieval floor: being worth reading as context is a
# much lower bar than being the same subject.
LINK_FLOOR = 0.45

RELATIONS = {"resolves", "continues", "corrects"}

LINK_SYSTEM = (
    "You decide whether a new note is a follow-up to one the person already "
    "wrote. You are strict: most notes are unrelated to most other notes, and "
    "'none' is the correct answer far more often than not. A shared topic is "
    "NOT a follow-up — it has to be the same specific thing. "
    "Respond with a single JSON object and nothing else."
)

LINK_PROMPT = """\
A new note, recorded {when}:

  Title:   {title}
  Summary: {summary}
  Said:    "{transcript}"

Earlier notes that might be about the same thing:

{candidates}

Return JSON:

  "relation"  One of:
                "resolves"  — the new note fixes or settles a problem the
                              earlier note described, or reports it is done
                "corrects"  — the earlier note was wrong and this replaces it
                "continues" — more about the same specific thing, unsettled
                "none"      — anything else
  "target"    The number of the earlier note, or null when relation is "none".
  "why"       At most 12 words on what connects them, or "".

Choose "none" unless the two are about THE SAME specific thing — the same
install, the same bug, the same trip. Two notes that merely share a topic, a
project or a tool are not related for this purpose.
"""


def _note_pages_only(hits):
    """Keep hits that are actual notes, not generated index pages."""
    marker = f"{os.sep}Notes{os.sep}"
    return [h for h in hits if marker in h.get("path", "")]


def find_related(transcript, note, when=None, exclude_path=None):
    """The earlier note this one follows up, or None.

    Returns {"relation", "path", "title", "why"}.
    """
    when = when or datetime.datetime.now().astimezone()

    # Search on the summary and title rather than the raw transcript: the
    # transcript carries filler and false starts that drag the embedding
    # toward whatever else was said clumsily.
    query = " ".join(filter(None, [note.get("title", ""),
                                   note.get("summary", "")])) or transcript
    try:
        hits = recall.search(query, k=CANDIDATES * 2)
    except Exception as exc:  # noqa: BLE001
        print(f"  link search failed: {exc}", flush=True)
        return None

    seen, candidates = set(), []
    for hit in _note_pages_only(hits):
        path = hit["path"]
        if path in seen or path == exclude_path:
            continue
        similarity = hit.get("similarity")
        if similarity is not None and similarity < LINK_FLOOR:
            continue
        seen.add(path)
        candidates.append(hit)
        if len(candidates) >= CANDIDATES:
            break

    if not candidates:
        return None

    listing = "\n".join(
        "  [{}] {} ({})\n      {}".format(
            n, hit["title"],
            (re.search(r"(\d{4}-\d{2}-\d{2})", os.path.basename(hit["path"])) or
             [None, "undated"])[1] if re.search(
                r"(\d{4}-\d{2}-\d{2})", os.path.basename(hit["path"])) else "undated",
            hit["text"].strip().replace("\n", " ")[:220],
        )
        for n, hit in enumerate(candidates, 1)
    )

    try:
        data = llm.generate_json(
            LINK_PROMPT.format(
                when=when.strftime("%A %d %B %Y"),
                title=note.get("title", ""),
                summary=note.get("summary", ""),
                transcript=transcript[:1200],
                candidates=listing,
            ),
            system=LINK_SYSTEM, max_tokens=200,
        )
    except Exception as exc:  # noqa: BLE001
        print(f"  link check failed: {exc}", flush=True)
        return None

    if not isinstance(data, dict):
        return None
    relation = str(data.get("relation", "")).strip().lower()
    if relation not in RELATIONS:
        return None

    try:
        index = int(data.get("target"))
    except (TypeError, ValueError):
        return None
    if not 1 <= index <= len(candidates):
        return None

    hit = candidates[index - 1]
    return {
        "relation": relation,
        "path": hit["path"],
        "title": hit["title"],
        "link": os.path.basename(hit["path"])[:-3],
        "why": re.sub(r"\s+", " ", str(data.get("why", ""))).strip()[:90],
    }


# ── Recording the link on the earlier note ────────────────────────────────

# What the earlier note becomes once something has followed it up. `open` is
# the absence of any of these and is never written.
STATUS_FOR = {
    "resolves": "resolved",
    "corrects": "superseded",
    "continues": "continued",
}

_TRAILER = re.compile(r"\n---\n\nFiled under .*$", re.S)


def _set_frontmatter(text, key, value):
    """Set or replace one frontmatter key, leaving everything else alone."""
    if not text.startswith("---"):
        return text
    end = text.find("\n---", 3)
    if end == -1:
        return text
    head, rest = text[3:end], text[end:]

    line = f"{key}: {value}"
    pattern = re.compile(rf"^{re.escape(key)}:.*$", re.M)
    head = pattern.sub(line, head) if pattern.search(head) else head.rstrip("\n") + "\n" + line + "\n"
    return "---" + head + rest


def mark_followed_up(target_path, relation, new_link, summary, when, thread_id):
    """Annotate the earlier note: a status, a thread, and a forward link.

    The note's own prose is not touched. What is added is an Updates section —
    generated content, in a file that was generated — so that anything
    retrieving this note later finds the update attached to it rather than
    having to know to go looking.
    """
    try:
        with open(target_path, encoding="utf-8") as handle:
            text = handle.read()
    except OSError:
        return False

    text = _set_frontmatter(text, "status", STATUS_FOR.get(relation, "continued"))
    text = _set_frontmatter(text, "thread", thread_id)

    verb = {"resolves": "Resolved", "corrects": "Corrected",
            "continues": "Followed up"}.get(relation, "Updated")
    entry = "- **{}** {} — [[{}]]{}".format(
        verb, when.strftime("%d %b %Y"), new_link,
        f" — {summary}" if summary else "")

    if "## Updates" in text:
        index = text.index("## Updates") + len("## Updates")
        text = text[:index] + "\n" + entry + text[index:]
    else:
        section = f"\n## Updates\n{entry}\n"
        # Before the "Filed under" trailer, so the footer stays last.
        match = _TRAILER.search(text)
        text = (text[:match.start()] + "\n" + section + text[match.start():]
                if match else text.rstrip() + "\n" + section)

    knowledge._atomic_write(target_path, text)
    return True


def thread_of(path):
    """The thread id already on a note, if any."""
    try:
        with open(path, encoding="utf-8") as handle:
            head = handle.read(1200)
    except OSError:
        return ""
    match = re.search(r"^thread:\s*(\S+)\s*$", head, re.M)
    return match.group(1) if match else ""
