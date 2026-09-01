#!/usr/bin/env python3
"""Reading notes: what a book said, and what you made of it.

A book note is not a general note. It has a source — a title, usually an
author, sometimes a page — and it carries two different kinds of content that
should not be blurred together:

    a quote      the author's words, which must survive verbatim
    a learning   your reading of them, which is yours

Keeping those apart is the whole point. A paraphrase silently stored as a quote
is a misattribution you will not notice for months, and a quote stored without
its source is unusable the moment you want to cite it. So the extraction is
told which is which, and the model is told never to tidy a quote.

**Notes accumulate per book, not per session.** One page per recording would
scatter ten weeks of reading across ten files. `Books/<Title>.md` grows as you
read, newest first, so the page is the book — which is how anyone actually
wants to revisit it.
"""

import datetime
import os
import re

import knowledge
import llm


def books_dir():
    return os.path.join(knowledge.VAULT, knowledge.ROOT, "Books")


BOOK_SYSTEM = (
    "You extract reading notes from a spoken note about a book. "
    "You never invent a quotation. A quote is the author's exact words and "
    "must be recorded exactly as spoken, with nothing smoothed, shortened or "
    "corrected; if the speaker paraphrased, that is a learning, not a quote. "
    "Respond with a single JSON object and nothing else."
)

BOOK_PROMPT = """\
A voice note taken while reading, recorded {when}:

\"\"\"
{transcript}
\"\"\"

{known}\
Return JSON with exactly these keys:

  "book"      The book's title as said, in Title Case. Empty string if no
              book was named — do NOT guess one from the subject matter.
  "author"    The author, if named. Empty string otherwise.
  "location"  Page, chapter or section if mentioned, e.g. "p. 42",
              "chapter 3". Empty string otherwise.
  "quotes"    The author's exact words, and ONLY where the speaker explicitly
              marked them — by saying "quote", "unquote", "the book says",
              "he writes", or by reading a passage out with clear
              attribution. Copy them verbatim: do not fix grammar, shorten or
              add words, and never include the speaker's own framing
              ("another one from X, ...") inside the quotation.
              If the speaker was summarising in their own words, that is a
              LEARNING, not a quote. When in doubt it is a learning.
              An empty list is the normal case.
  "learnings" Up to 4 short strings: what the speaker took from it, in their
              own words. This is where a paraphrase belongs.
  "thoughts"  Up to 3 of the speaker's own reactions, disagreements or
              connections to something else. Empty list is normal.
  "topics"    Up to 2 broad subjects the ideas belong to, 1 to 3 words.
"""


def _known_books_hint():
    """Existing book pages, so a title said twice files to one page."""
    directory = books_dir()
    if not os.path.isdir(directory):
        return ""
    titles = sorted(e[:-3] for e in os.listdir(directory) if e.endswith(".md"))
    if not titles:
        return ""
    return (
        "Books already being read — if this note is about one of them, spell "
        "the title exactly as shown:\n  " + ", ".join(titles[:30]) + "\n\n"
    )


# Words that mark a quotation out loud. Speech has no quotation marks, so a
# speaker signals one by saying so — and if they did not, there is no
# quotation to record.
_QUOTE_MARKERS = re.compile(
    "\\bquote[sd]?\\b|\\bunquote\\b|\\bquoting\\b|\\bverbatim\\b|"
    "\\bwrites\\b|\\bwrote\\b|\\bputs it\\b|[\"\u201c\u201d]",
    re.I,
)


def _demote_unmarked_quotes(reading, transcript):
    """Move "quotes" into learnings unless the speaker actually marked one.

    The model does not hold this line on its own. Asked for reading notes on
    "from Deep Work, the idea that attention residue is why switching costs so
    much", it returned that sentence — the speaker's own paraphrase, with the
    speaker's own framing still in it — as a QUOTE by Cal Newport. On another
    it quoted "another one from Thinking Fast and Slow, the anchoring
    effect...", a sentence that appears in no book.

    A misattribution is not a cosmetic error. It survives in the vault, gets
    retrieved months later, and carries nothing to reveal itself. So the check
    is made in code rather than asked for in a prompt: if nothing in the
    transcript marks a quotation, nothing is stored as one. The text is kept
    as a learning, which is what it always was.
    """
    if not reading["quotes"] or _QUOTE_MARKERS.search(transcript):
        return reading

    for quote in reading["quotes"]:
        if quote.lower() not in (l.lower() for l in reading["learnings"]):
            reading["learnings"].append(quote[:200])
    reading["learnings"] = reading["learnings"][:5]
    reading["quotes"] = []
    return reading


def extract(transcript, when=None):
    """Reading notes from a transcript, or None if there is nothing to file."""
    transcript = (transcript or "").strip()
    when = when or datetime.datetime.now().astimezone()
    if len(transcript) < 15:
        return None

    prompt = BOOK_PROMPT.format(
        when=when.strftime("%A %d %B %Y at %H:%M"),
        transcript=transcript[:5000],
        known=_known_books_hint(),
    )
    try:
        data = llm.generate_json(prompt, system=BOOK_SYSTEM, max_tokens=800)
    except Exception as exc:  # noqa: BLE001
        print(f"  book extraction failed: {exc}", flush=True)
        return None
    if not isinstance(data, dict):
        return None

    quotes = knowledge._clean_list(data.get("quotes"), 5, max_chars=600)
    learnings = knowledge._clean_list(data.get("learnings"), 4, max_chars=200)
    thoughts = knowledge._clean_list(data.get("thoughts"), 3, max_chars=200)
    if not (quotes or learnings or thoughts):
        return None

    reading = {
        "book": knowledge._canonical_topic(str(data.get("book", "")).strip())
                or "",
        "author": re.sub(r"\s+", " ", str(data.get("author", ""))).strip()[:80],
        "location": re.sub(r"\s+", " ",
                           str(data.get("location", ""))).strip()[:40],
        "quotes": quotes,
        "learnings": learnings,
        "thoughts": thoughts,
        "topics": knowledge._clean_list(data.get("topics"), 2, max_chars=40),
    }
    return _demote_unmarked_quotes(reading, transcript)


def as_structured(reading, transcript):
    """Shape reading notes like a structured note, for the usual filing."""
    book = reading["book"]
    label = book or "Reading note"

    if reading["learnings"]:
        summary = reading["learnings"][0]
    elif reading["quotes"]:
        summary = f'"{reading["quotes"][0][:160]}"'
    else:
        summary = reading["thoughts"][0]

    title = label
    if reading["learnings"]:
        # A page named only after the book collides with every other note from
        # the same book, and `_unique_path` would then number them -2, -3.
        # Naming it after what was learnt keeps them findable apart.
        title = f"{label} — {reading['learnings'][0][:48]}"

    topics = ["Books"] + reading["topics"]
    if book:
        topics.insert(1, book)

    frontmatter = {}
    if book:
        frontmatter["book"] = book
    if reading["author"]:
        frontmatter["author"] = reading["author"]
    if reading["location"]:
        frontmatter["location"] = reading["location"]

    return {
        "title": title[:90],
        "summary": summary[:400],
        "quotes": reading["quotes"],
        "key_points": reading["learnings"],
        "actions": [],
        "questions": reading["thoughts"],
        "entities": [x for x in (book, reading["author"]) if x],
        "topics": topics,
        "structured": True,
        "frontmatter": frontmatter,
    }


def file_reading(reading, link_title, when):
    """Append this session's notes to the book's own page.

    Newest first, under one heading, so the page reads as the book rather than
    as a pile of recordings. Created on the first note about it.
    """
    book = reading["book"]
    if not book:
        return None  # nothing to file it under; the note page still exists

    os.makedirs(books_dir(), exist_ok=True)
    path = os.path.join(books_dir(), f"{knowledge.slugify(book, 70)}.md")

    header_lines = ["---", f"title: {knowledge._quote(book)}", "kind: book"]
    if reading["author"]:
        header_lines.append(f"author: {knowledge._quote(reading['author'])}")
    header_lines += ["---", "", f"# {book}", ""]
    if reading["author"]:
        header_lines += [f"_by {reading['author']}_", ""]
    header_lines += ["## Notes", ""]
    header = "\n".join(header_lines)

    where = f" ({reading['location']})" if reading["location"] else ""
    block = [f"### {when.strftime('%d %B %Y')}{where} — [[{link_title}]]"]
    for quote in reading["quotes"]:
        # A real blockquote, so the author's words are visually theirs and
        # never mistaken later for your own summary.
        block.append(f"> {quote}")
        block.append("")
    for learning in reading["learnings"]:
        block.append(f"- {learning}")
    for thought in reading["thoughts"]:
        block.append(f"- _{thought}_")

    knowledge._append_under(path, "## Notes", "\n".join(block) + "\n", header)
    return path


def digest(reading):
    """What the device shows: the book, and the first thing taken from it."""
    lines = [reading["book"] or "Reading note"]
    if reading["quotes"]:
        lines.append(f'"{reading["quotes"][0][:120]}"')
    for learning in reading["learnings"][:3]:
        lines.append(f"- {learning}")
    return "\n".join(lines)
