#!/usr/bin/env python3
"""Answering questions from the vault.

The knowledge base is only worth building if you can ask it things. This is the
retrieval half: an index over the markdown, a search that finds the right
passages, and a prompt that answers from them without inventing the parts it
cannot find.

**Hybrid search, not pure embeddings.** Semantic search is what finds the note
about "the drone battery problem" when you asked about "flight time", and it is
also what confidently returns three unrelated notes when you asked for a
specific proper noun. Keyword search is the opposite: exact on names and
numbers, useless on paraphrase. Personal notes need both — they are full of
project names and part numbers AND half-remembered ideas. The two rankings are
merged with reciprocal rank fusion, which needs no score calibration between
two scales that are not comparable.

**Embeddings are optional.** If no encoder is installed, search degrades to
FTS5 alone. That is worse, and it still works, and it means a fresh machine is
useful before a 300 MB download finishes.

**The index is incremental.** Files are re-read only when mtime or size
changed, so a reindex over a vault that has not moved costs a stat per file.
Retrieval can therefore be refreshed on every question without anyone noticing.
"""

import os
import re
import sqlite3
import struct
import threading
import time

import expenses
import knowledge
import llm

DB_PATH = os.path.expanduser(
    os.environ.get("ATOMIC_INDEX", "~/.atomic-note/index.db")
)

# Bump whenever chunking, filtering or the embedding contract changes. An
# existing index is then thrown away and rebuilt rather than left holding rows
# the current code would never have written.
INDEX_VERSION = "3"

# Long enough to hold a whole section of a note, short enough that a hit is a
# passage rather than a page. Overlap keeps a fact that straddles a boundary
# from being findable in neither half.
CHUNK_CHARS = 900
CHUNK_OVERLAP = 150

# How many passages reach the model. More context is not better here: a small
# local model given twelve passages answers from the wrong one, and every
# passage is latency the device is waiting through.
TOP_K = 6

# Below this the retrieval is treated as having found nothing relevant, and
# the model is told so rather than being handed noise to answer from.
MIN_HITS = 1

# Absolute cosine floor for "this passage is about the question at all".
#
# The relative gate below cannot express this. Ranking always produces a best
# match, and on a question the vault has nothing to say about, the best match
# is still returned and still looks like the winner — asked for the capital of
# France, a vault of engineering notes confidently offers its engineering
# notes. Only an absolute score separates "the closest thing here" from
# "something relevant".
#
# Measured with embeddinggemma: genuine matches score 0.46-0.67 and unrelated
# passages 0.13-0.26, so the gap is wide and this sits in the middle of it.
# It IS model-dependent, hence the override — a different encoder will put its
# own scale around a different number.
RELEVANCE_FLOOR = float(os.environ.get("ATOMIC_RELEVANCE_FLOOR", "0.32"))

# Passages from any single note. See the diversity pass in search().
MAX_PER_NOTE = 2

_lock = threading.Lock()
_indexing = threading.Lock()


SCHEMA = """
    CREATE TABLE IF NOT EXISTS files (
        path  TEXT PRIMARY KEY,
        mtime REAL NOT NULL,
        size  INTEGER NOT NULL
    );
    CREATE TABLE IF NOT EXISTS chunks (
        id      INTEGER PRIMARY KEY,
        path    TEXT NOT NULL,
        ord     INTEGER NOT NULL,
        title   TEXT,
        heading TEXT,
        text    TEXT NOT NULL,
        vec     BLOB,
        -- Threading. A note that has been followed up carries a status and a
        -- thread id, and retrieval has to know both: answering "what was wrong
        -- with the install" from a note that has since been resolved is
        -- correct about the past and wrong about now.
        status  TEXT,
        thread  TEXT
    );
    CREATE INDEX IF NOT EXISTS chunks_path ON chunks(path);
    CREATE INDEX IF NOT EXISTS chunks_thread ON chunks(thread);

    -- A plain FTS5 table, NOT contentless.
    --
    -- `content=''` saves storing the text twice, and in exchange forbids
    -- ordinary DELETE: rows can only be removed by handing the original column
    -- values back through a special insert. Every path that reindexes a changed
    -- or deleted note has to delete rows, so the saving costs correctness at
    -- exactly the point where notes get edited. The duplicated text is a few MB
    -- on a vault of thousands.
    CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts5(text, title);

    CREATE TABLE IF NOT EXISTS meta (
        key   TEXT PRIMARY KEY,
        value TEXT NOT NULL
    );
"""


def _connect():
    os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)
    connection = sqlite3.connect(DB_PATH, check_same_thread=False)
    connection.execute("PRAGMA journal_mode=WAL")

    # Check the version BEFORE creating anything, and drop the tables outright
    # when it has moved.
    #
    # An earlier version of this only deleted the ROWS. That is not a
    # migration: `CREATE TABLE IF NOT EXISTS` leaves an existing table's
    # columns exactly as they were, so adding `status` to the schema produced
    # an empty index and "table chunks has no column named status" on every
    # insert. The index is derived from the vault and costs seconds to rebuild,
    # so throwing it away is always the right move.
    stored = ""
    try:
        row = connection.execute(
            "SELECT value FROM meta WHERE key='index_version'"
        ).fetchone()
        stored = row[0] if row else ""
    except sqlite3.OperationalError:
        stored = ""  # no meta table yet: a fresh database

    if stored and stored != INDEX_VERSION:
        print(f"  index schema {stored} -> {INDEX_VERSION}; rebuilding",
              flush=True)
        connection.executescript(
            "DROP TABLE IF EXISTS chunks_fts;"
            "DROP TABLE IF EXISTS chunks;"
            "DROP TABLE IF EXISTS files;"
        )

    connection.executescript(SCHEMA)
    connection.execute(
        "INSERT OR REPLACE INTO meta(key, value) VALUES ('index_version', ?)",
        (INDEX_VERSION,),
    )
    connection.commit()
    return connection


_db = None


def db():
    global _db
    with _lock:
        if _db is None:
            _db = _connect()
        return _db


# ── Reading the vault ─────────────────────────────────────────────────────

def _walk():
    seen = set()
    for root in knowledge.read_roots():
        for directory, dirnames, filenames in os.walk(root):
            # Obsidian's own state, and anything the user has hidden.
            dirnames[:] = [d for d in dirnames if not d.startswith(".")]
            for name in filenames:
                if not name.endswith(".md"):
                    continue
                path = os.path.join(directory, name)
                if path not in seen:
                    seen.add(path)
                    yield path


def _strip_frontmatter(text):
    """Return (body, title, kind). Frontmatter is metadata, not prose.

    `kind` is what marks a page as generated. Topic pages and daily pages are
    built from the notes rather than written, so every summary in the vault
    appears in three places — and indexing all three means a search for it
    returns the note, its topic page and its daily page as three separate
    results, crowding out the notes that actually differ. Only pages a person
    or the capture pipeline wrote are worth searching.
    """
    title = kind = status = thread = ""
    if text.startswith("---"):
        end = text.find("\n---", 3)
        if end != -1:
            head = text[3:end]
            match = re.search(r'^title:\s*"?(.*?)"?\s*$', head, re.M)
            if match:
                title = match.group(1).strip()
            match = re.search(r"^kind:\s*(\S+)\s*$", head, re.M)
            if match:
                kind = match.group(1).strip()
            match = re.search(r"^status:\s*(\S+)\s*$", head, re.M)
            if match:
                status = match.group(1).strip()
            match = re.search(r"^thread:\s*(\S+)\s*$", head, re.M)
            if match:
                thread = match.group(1).strip()
            text = text[end + 4:]
    return text.lstrip("\n"), title, kind, status, thread


# Pages this pipeline generates, and therefore will not index.
#
# `expense-month` is generated too and is deliberately NOT in here. Those pages
# are the only place the arithmetic exists — spend by category, by medium, by
# shop, already summed. Excluding them would leave "how much did I spend on
# coffee last month" to be answered by a model adding up thirty retrieved
# amounts in its head, which it does confidently and wrongly.
GENERATED_KINDS = {"topic", "daily", "index"}


def _chunk(text, title):
    """Split a note into passages, keeping each one's heading with it.

    Splitting on headings first means a passage is a coherent unit — the
    Actions of one note never merge with the Transcript of the next section —
    and carrying the heading into the indexed text is what lets "what do I
    need to buy" find a list that never uses the word "buy".
    """
    sections = []
    current_heading = ""
    buffer = []

    for line in text.splitlines():
        match = re.match(r"^(#{1,6})\s+(.*)$", line)
        if match:
            if buffer:
                sections.append((current_heading, "\n".join(buffer)))
                buffer = []
            current_heading = match.group(2).strip()
            continue
        buffer.append(line)
    if buffer:
        sections.append((current_heading, "\n".join(buffer)))

    chunks = []
    for heading, body in sections:
        body = re.sub(r"\n{3,}", "\n\n", body).strip()
        if len(body) < 20:
            continue

        start = 0
        while start < len(body):
            piece = body[start:start + CHUNK_CHARS]
            if len(piece) < 20:
                break
            # Prefix with title and heading so the embedding sees what this
            # passage is about, not just what it says.
            label = " › ".join(part for part in (title, heading) if part)
            chunks.append((heading, f"{label}\n{piece}" if label else piece))
            if start + CHUNK_CHARS >= len(body):
                break
            start += CHUNK_CHARS - CHUNK_OVERLAP

    return chunks


def _pack(vector):
    return struct.pack(f"{len(vector)}f", *vector)


def _unpack(blob):
    return struct.unpack(f"{len(blob) // 4}f", blob)


def _normalise(vectors):
    """Unit-length, so cosine similarity is a plain dot product later."""
    import numpy as np

    array = np.asarray(vectors, dtype=np.float32)
    norms = np.linalg.norm(array, axis=1, keepdims=True)
    norms[norms == 0] = 1.0
    return array / norms


def reindex(force=False, verbose=False):
    """Bring the index up to date. Returns (files_indexed, chunks_written)."""
    if not _indexing.acquire(blocking=False):
        return (0, 0)  # a reindex is already running; one at a time is enough

    try:
        # Money first, BEFORE the files are walked.
        #
        # Dropping a deleted note from the search index is not enough on its
        # own: an expense also lives as a row in the ledger, and the ledger is
        # what the totals are computed from. Without this, deleting a note
        # would remove it from what can be *found* while leaving it in what
        # gets *summed* — the worst of both.
        #
        # It runs first because reconciling rewrites the month pages, and those
        # pages are files this walk is about to index. Doing it afterwards
        # would leave the current pass reading totals that were already stale.
        try:
            dropped, _ = expenses.sync_from_vault(verbose=verbose)
            if dropped:
                print(f"  ledger: dropped {dropped} expense(s) whose note was "
                      "deleted", flush=True)
        except Exception as exc:  # noqa: BLE001
            print(f"  ledger reconcile failed: {exc}", flush=True)

        connection = db()
        known = {
            row[0]: (row[1], row[2])
            for row in connection.execute("SELECT path, mtime, size FROM files")
        }

        present = set()
        files_done = 0
        chunks_done = 0

        for path in _walk():
            present.add(path)
            try:
                stat = os.stat(path)
            except OSError:
                continue

            if not force and path in known:
                mtime, size = known[path]
                if abs(mtime - stat.st_mtime) < 1e-6 and size == stat.st_size:
                    continue

            try:
                with open(path, encoding="utf-8", errors="replace") as handle:
                    raw = handle.read()
            except OSError:
                continue

            body, title, kind, status, thread = _strip_frontmatter(raw)
            if not title:
                title = os.path.basename(path)[:-3]

            # Generated: drop anything previously indexed for it and move on.
            # Recorded in `files` regardless, so it is not re-read every pass.
            pieces = [] if kind in GENERATED_KINDS else _chunk(body, title)

            _forget(connection, path)
            for ordinal, (heading, text) in enumerate(pieces):
                cursor = connection.execute(
                    "INSERT INTO chunks(path, ord, title, heading, text, "
                    "status, thread) VALUES (?,?,?,?,?,?,?)",
                    (path, ordinal, title, heading, text, status, thread),
                )
                connection.execute(
                    "INSERT INTO chunks_fts(rowid, text, title) VALUES (?,?,?)",
                    (cursor.lastrowid, text, title),
                )
            connection.execute(
                "INSERT OR REPLACE INTO files(path, mtime, size) VALUES (?,?,?)",
                (path, stat.st_mtime, stat.st_size),
            )
            files_done += 1
            chunks_done += len(pieces)
            if verbose:
                print(f"  indexed {os.path.basename(path)} ({len(pieces)})", flush=True)

        # Notes deleted from the vault must leave the index, or they keep
        # being cited as sources for files that are not there.
        for path in set(known) - present:
            _forget(connection, path)
            connection.execute("DELETE FROM files WHERE path=?", (path,))

        connection.commit()
        _embed_pending(connection, verbose=verbose)
        return files_done, chunks_done
    finally:
        _indexing.release()


def _forget(connection, path):
    rows = connection.execute("SELECT id FROM chunks WHERE path=?", (path,)).fetchall()
    for (rowid,) in rows:
        connection.execute("DELETE FROM chunks_fts WHERE rowid=?", (rowid,))
    connection.execute("DELETE FROM chunks WHERE path=?", (path,))


def _embed_pending(connection, batch=32, verbose=False):
    """Fill in embeddings for chunks that have none.

    Separate from indexing so a vault walk never blocks on the encoder, and so
    a machine with no embedding model still gets a complete keyword index.
    """
    rows = connection.execute(
        "SELECT id, text FROM chunks WHERE vec IS NULL"
    ).fetchall()
    if not rows:
        return 0

    if llm.embed_model() is None:
        return 0

    done = 0
    for start in range(0, len(rows), batch):
        window = rows[start:start + batch]
        try:
            vectors = llm.embed([text for _, text in window])
        except Exception as exc:  # noqa: BLE001
            print(f"  embedding failed: {exc}", flush=True)
            return done
        if not vectors:
            return done

        for (rowid, _), vector in zip(window, _normalise(vectors)):
            connection.execute(
                "UPDATE chunks SET vec=? WHERE id=?", (_pack(vector.tolist()), rowid)
            )
        done += len(window)
        if verbose:
            print(f"  embedded {done}/{len(rows)}", flush=True)
    connection.commit()
    return done


# ── Search ────────────────────────────────────────────────────────────────

_STOPWORDS = {
    "what", "when", "where", "which", "who", "whom", "whose", "why", "how",
    "did", "do", "does", "was", "were", "is", "are", "the", "a", "an", "of",
    "to", "in", "on", "for", "about", "my", "me", "i", "and", "or", "that",
    "this", "it", "any", "all", "have", "has", "had", "said", "say", "tell",
}


def _fts_query(question):
    """A safe FTS5 MATCH expression from arbitrary spoken text.

    User text goes nowhere near the query syntax: an apostrophe or a bare `-`
    from a transcript is a syntax error, and a question mark is a wildcard.
    Only word characters survive, each quoted.
    """
    words = re.findall(r"[A-Za-z0-9]+", question.lower())
    terms = [w for w in words if len(w) > 2 and w not in _STOPWORDS]
    if not terms:
        terms = [w for w in words if len(w) > 2]
    if not terms:
        return ""
    return " OR ".join(f'"{term}"' for term in terms[:12])


def _lexical(connection, question, limit):
    expression = _fts_query(question)
    if not expression:
        return []
    try:
        rows = connection.execute(
            "SELECT rowid FROM chunks_fts WHERE chunks_fts MATCH ? "
            "ORDER BY bm25(chunks_fts, 1.0, 2.0) LIMIT ?",
            (expression, limit),
        ).fetchall()
    except sqlite3.OperationalError:
        return []
    return [row[0] for row in rows]


def _semantic(connection, question, limit):
    if llm.embed_model() is None:
        return []
    try:
        vectors = llm.embed([question])
    except Exception:  # noqa: BLE001
        return []
    if not vectors:
        return []

    import numpy as np

    rows = connection.execute(
        "SELECT id, vec FROM chunks WHERE vec IS NOT NULL"
    ).fetchall()
    if not rows:
        return []

    query = _normalise(vectors)[0]
    matrix = np.array([_unpack(blob) for _, blob in rows], dtype=np.float32)
    if matrix.shape[1] != query.shape[0]:
        # The embedding model changed under an existing index. Rather than
        # returning nonsense, drop the stale vectors and let them be rebuilt.
        connection.execute("UPDATE chunks SET vec=NULL")
        connection.commit()
        return []

    scores = matrix @ query
    order = np.argsort(-scores)[:limit]
    # The cosine goes back with the id. Rank alone is enough to fuse with
    # BM25, but it says nothing about whether the best match is any good — and
    # deciding what to show as a SOURCE needs exactly that.
    return [(rows[i][0], float(scores[i])) for i in order]


def search(question, k=TOP_K):
    """Passages most likely to answer `question`, best first.

    Reciprocal rank fusion over the two rankings: a passage's score is the sum
    of 1/(60+rank) across the lists it appears in. It needs no comparable
    scales between BM25 and cosine, and it rewards passages both methods liked
    — which are reliably the right ones.
    """
    connection = db()
    pool = max(k * 4, 20)

    semantic = _semantic(connection, question, pool)
    similarity = dict(semantic)

    fused = {}
    for ranking in (_lexical(connection, question, pool),
                    [rowid for rowid, _ in semantic]):
        for rank, rowid in enumerate(ranking):
            fused[rowid] = fused.get(rowid, 0.0) + 1.0 / (60 + rank)

    ordered = sorted(fused, key=fused.get, reverse=True)
    if not ordered:
        return []

    # Look up more than we need, so passages dropped for diversity below can
    # be replaced rather than leaving the context short.
    candidates = ordered[: k * 4]
    placeholders = ",".join("?" * len(candidates))
    rows = connection.execute(
        f"SELECT id, path, title, heading, text, status, thread "
        f"FROM chunks WHERE id IN ({placeholders})",
        candidates,
    ).fetchall()
    by_id = {row[0]: row for row in rows}

    # At most two passages from any one note.
    #
    # Without this the strongest match takes every slot: "why did inference get
    # slower" filled five of six from the note that named the problem, leaving
    # one for the follow-up note that actually contained the cause. Retrieval
    # that returns the same document six times has found one thing, not six.
    best, per_path = [], {}
    for rowid in candidates:
        row = by_id.get(rowid)
        if row is None:
            continue
        path = row[1]
        if per_path.get(path, 0) >= MAX_PER_NOTE:
            continue
        per_path[path] = per_path.get(path, 0) + 1
        best.append(rowid)
        if len(best) >= k:
            break

    return [
        {
            "path": by_id[rowid][1],
            "title": by_id[rowid][2],
            "heading": by_id[rowid][3],
            "text": by_id[rowid][4],
            "status": by_id[rowid][5] or "",
            "thread": by_id[rowid][6] or "",
            "score": fused[rowid],
            "similarity": similarity.get(rowid),
        }
        for rowid in best
        if rowid in by_id
    ]


# How far below the best match a note may be and still be called a source.
# Cosine similarity is bounded and comparable, so this is a real threshold
# rather than a tuned constant: a note 20% less similar than the best one is
# plausibly about the same thing, and one at half is not.
SOURCE_SIMILARITY_RATIO = 0.8

# Without embeddings there is no comparable score, only an order. Show a few
# and stop, rather than implying a relevance the ranking cannot support.
SOURCE_FALLBACK_LIMIT = 3


def sources(hits):
    """The notes an answer actually came from, best first.

    Two things are being fixed here. `search` returns passages, and two
    passages from one note are two pieces of context but one source. And RRF
    ranks EVERYTHING it was given — on a small vault that means every note
    comes back for every question, so reporting the ranking verbatim claimed a
    shopping list as a source for a question about a build failure.

    Rank is the wrong tool for that second problem, because it has no notion of
    "not close enough". Cosine similarity does, so where embeddings exist the
    cut is made on similarity to the best match.
    """
    seen, unique = set(), []
    for hit in hits:
        if hit["path"] in seen:
            continue
        seen.add(hit["path"])
        unique.append(hit)

    scored = [hit for hit in unique if hit.get("similarity") is not None]
    if not scored:
        return unique[:SOURCE_FALLBACK_LIMIT]

    scored.sort(key=lambda hit: hit["similarity"], reverse=True)
    # Both gates: relevant in absolute terms, and close to the best match.
    floor = max(RELEVANCE_FLOOR, scored[0]["similarity"] * SOURCE_SIMILARITY_RATIO)
    return [hit for hit in scored if hit["similarity"] >= floor]


def relevant(hits):
    """Do any of these passages actually bear on the question?

    Used to choose between answering from the notes and admitting they have
    nothing. Without an encoder there is no way to tell, so the ranking is
    taken at face value — which is the older, weaker behaviour, and the reason
    an embedding model is worth pulling.
    """
    scored = [hit["similarity"] for hit in hits if hit.get("similarity") is not None]
    if not scored:
        return bool(hits)
    return max(scored) >= RELEVANCE_FLOOR


# ── Answering ─────────────────────────────────────────────────────────────

# Two registers for the same job. The screen prompt is the old one: clipped,
# because every word costs a page turn on a 200x200 panel. The spoken prompt
# is new, and it exists because text written to be read tersely sounds curt
# and strange when a voice says it — contractions and a normal sentence rhythm
# are most of what makes synthesised speech sound like a person.
SCREEN_SYSTEM = (
    "You answer questions from a person's own notes, on a small e-ink screen. "
    "Reply in at most 5 short sentences, under 90 words. "
    "Plain prose only: no preamble, no bullet points, no markdown, no headings. "
    "Answer from the notes when they cover it. Refer to a note by what it is "
    "called or when it was made — never by a number, a file path or a URL. "
    "If the notes do not cover it, say so in one sentence, then answer from "
    "general knowledge if you can."
)

SPOKEN_SYSTEM = (
    "You answer questions from a person's own notes, out loud, in a friendly "
    "and natural speaking voice. "
    "Talk the way a thoughtful friend would: use contractions, vary your "
    "sentence length, and get to the point without sounding clipped. "
    "At most 4 sentences, under 80 words. "
    "No lists, no markdown, no headings, no bullet points — this is spoken, "
    "so it has to work as continuous speech. "
    "Never read out a file path, a URL, or a date stamp; refer to notes by "
    "what they are about, and by when, as a person would say it. "
    "Answer from the notes when they cover it. If they don't, say so briefly "
    "and then help anyway if you can."
)

CONTEXT_PROMPT = """\
Here are passages from the person's own notes that may be relevant.

{context}

Their question: {question}

Answer it. Use the passages when they are relevant and ignore the ones that
are not. Do not invent anything that is not in the passages or in well-known
general knowledge, and never claim a note says something it does not.

Answer the question directly. Do not list which passages you used, and do not
number them — if it helps to say where something came from, name the note.

Some passages are labelled OUTDATED, meaning a later note resolved or corrected
them. Never present an outdated passage as the current state. Say what the
situation was, then what it is now — "that was failing because X; you fixed it
on the 5th by doing Y" — so the history is kept and the answer is current.

Those labels are for you, not for the reader. Never mention them and never
describe a note as outdated or superseded — just tell the story in order, as a
person would.

Passages marked "summary, already totalled" contain figures summed over every
entry they cover. Quote those figures as they stand rather than recomputing
them from the entries beneath.\
"""


SPEND_PROMPT = """\
{facts}

The person asked: {question}

Answer using ONLY the figures above. They cover exactly the period the question
asked about and nothing else, so do not qualify them with a different period or
add anything from memory. They are exact and already summed: read the relevant
line off and say it — do not add, subtract or recompute anything.
If the exact figure asked for is not listed, say what IS listed and leave it
there rather than deriving a number.

Write it as a sentence someone would say. The block above is a data sheet, not
a draft: do not copy its labels, its capitalisation or its layout, and do not
reply with a list. "You spent ₹1,249 on UPI and ₹2,448 by card this month" —
not "UPI: ₹1,249".

When asked how much, lead with the TOTAL for the period and name the period —
"you've spent ₹370 today" — then add a detail or two if it helps. A breakdown
that never states the total makes the reader add it up themselves, which is the
thing this is here to avoid.\
"""

NO_CONTEXT_PROMPT = """\
The person asked: {question}

Nothing in their notes matches this question. Say so in a few words, then
answer from general knowledge if you reasonably can.\
"""


def _note_date(path):
    """The date in a note's filename, for ordering a thread."""
    match = re.search(r"(\d{4}-\d{2}-\d{2})", os.path.basename(path))
    return match.group(1) if match else ""


def expand_threads(hits, per_thread=1):
    """Pull in the latest note of any thread a hit belongs to.

    Retrieval matches on wording, and the wording of a problem is in the note
    that reported it — not in the one that fixed it three days later. So a
    question about a broken install finds Monday's note and stops, and the
    answer describes a problem that no longer exists.

    Following the thread forward is what fixes that. The passages are added
    rather than substituted: what went wrong is still the answer to half the
    question.
    """
    threaded = {h.get("thread") for h in hits if h.get("thread")}
    if not threaded:
        return hits

    have = {h["path"] for h in hits}
    connection = db()
    extra = []

    for thread in threaded:
        rows = connection.execute(
            "SELECT id, path, title, heading, text, status, thread "
            "FROM chunks WHERE thread=?", (thread,)
        ).fetchall()
        if not rows:
            continue
        # The tip of the thread: the most recent note in it. Everything
        # earlier has been followed up by something.
        latest = max((r[1] for r in rows), key=_note_date, default=None)
        if not latest or latest in have:
            continue
        for row in sorted((r for r in rows if r[1] == latest),
                          key=lambda r: r[0])[:per_thread]:
            extra.append({
                "path": row[1], "title": row[2], "heading": row[3],
                "text": row[4], "status": row[5] or "", "thread": row[6] or "",
                "score": 0.0, "similarity": None, "followed_up": True,
            })
            have.add(row[1])

    return hits + extra


def _is_summary(hit):
    """Does this passage hold pre-computed aggregates?"""
    return f"{os.sep}Expenses{os.sep}" in hit.get("path", "")


def _format_context(hits):
    """Label passages by note, not by number.

    They used to be numbered `[1]`, `[2]`, and the model dutifully wrote "this
    is mentioned in note [1], [2] and [6]" — meaningless on the panel and
    worse read aloud. Nothing needs the numbers: giving each passage its note's
    name and date means a citation is already something a person can use.
    """
    blocks = []
    for hit in hits:
        when = ""
        # The note filename carries its date; surfacing it lets the model say
        # "a couple of weeks ago" instead of quoting a path.
        match = re.search(r"(\d{4}-\d{2}-\d{2})", os.path.basename(hit["path"]))
        if match:
            when = f" ({match.group(1)})"

        # Monthly expense pages carry figures that are ALREADY summed over
        # every entry in the month. Retrieval also returns the individual
        # notes those entries came from, and a model handed both will happily
        # add a category total to the very rows it is a total OF: asked to
        # split spending by payment method it answered "Card ₹3,347" for a
        # month whose true card total was ₹2,448. Labelling the summary is
        # what lets the prompt rule below actually bite.
        if _is_summary(hit):
            label = "summary, already totalled"
        elif hit.get("status") in ("resolved", "superseded", "continued"):
            # Say it outright. A model given an old note and a newer one has
            # to work out which is current from dates alone, and it does that
            # badly — it will happily report a problem as ongoing because that
            # note was longer and more specific.
            # Lower case and unremarkable, on purpose. An earlier version
            # shouted "OUTDATED", and the model kept quoting the word back at
            # the reader — "the earlier note was outdated" — which is
            # scaffolding, not an answer.
            label = {
                "resolved": "earlier note, later resolved",
                "superseded": "earlier note, later corrected",
                "continued": "earlier note in the same story",
            }[hit["status"]]
        elif hit.get("followed_up"):
            label = "most recent note in the same story"
        else:
            label = "note"
        blocks.append(
            f"--- {label}: \"{hit['title']}\"{when}\n{hit['text'].strip()}"
        )
    return "\n\n".join(blocks)


# Sentences that describe the plumbing rather than answering the question.
# The prompt asks for these not to appear and mostly they do not; this removes
# the ones that slip through, because "the earlier note was outdated" is a
# remark about how the retrieval works and means nothing to the person who
# asked what went wrong.
_META_TALK = re.compile(
    r"\b(outdated|superseded|the (earlier|previous|older) note\b.*\b(was|is)\b)",
    re.I,
)


def _strip_meta_talk(text):
    """Drop sentences that talk about the notes instead of the subject."""
    if not text:
        return text
    parts = re.split(r"(?<=[.!?])\s+", text.strip())
    kept = [p for p in parts if not _META_TALK.search(p)]
    # Never strip everything: if that is all it said, the original is better
    # than nothing.
    return " ".join(kept).strip() if kept else text


def answer(question, spoken=False, refresh=True):
    """Answer `question` from the vault. Returns (text, sources).

    `refresh` reindexes first, so a note spoken thirty seconds ago is already
    answerable. Incremental, so it costs a stat per file when nothing changed.
    """
    question = (question or "").strip()
    if not question:
        return "I did not catch that.", []

    if refresh:
        try:
            reindex()
        except Exception as exc:  # noqa: BLE001
            print(f"  reindex failed: {exc}", flush=True)

    system = SPOKEN_SYSTEM if spoken else SCREEN_SYSTEM

    # Money questions bypass retrieval entirely.
    #
    # Not because retrieval fails to find the right notes — it finds them — but
    # because answering "how much" from a pile of passages is an arithmetic
    # task, and a local model does arithmetic badly enough to be worse than
    # useless: a wrong total stated confidently is a number someone might act
    # on. The ledger can answer these exactly, so it does, and the model is
    # left with the part it is good at: turning a figure into a sentence.
    if expenses.is_spending_question(question):
        facts = expenses.fact_sheet(question=question)
        if facts:
            text = llm.generate(
                SPEND_PROMPT.format(facts=facts, question=question),
                system=system, max_tokens=260, temperature=0.3,
            )
            return text, []

    hits = expand_threads(search(question))

    # Not just "did anything come back", but "is any of it about this". A
    # question the vault cannot answer used to be handed the vault's six
    # closest passages anyway, which invites the model to force a connection
    # between a shopping list and whatever was asked.
    if len(hits) < MIN_HITS or not relevant(hits):
        text = llm.generate(
            NO_CONTEXT_PROMPT.format(question=question),
            system=system, max_tokens=260, temperature=0.6,
        )
        return text, []

    text = _strip_meta_talk(llm.generate(
        CONTEXT_PROMPT.format(
            context=_format_context(hits), question=question
        ),
        system=system, max_tokens=300, temperature=0.5,
    ))
    # Passages in, notes out. The model needed the passages; the person asking
    # wants to know which notes the answer came from.
    return text, sources(hits)


def stats():
    connection = db()
    files = connection.execute("SELECT COUNT(*) FROM files").fetchone()[0]
    chunks = connection.execute("SELECT COUNT(*) FROM chunks").fetchone()[0]
    embedded = connection.execute(
        "SELECT COUNT(*) FROM chunks WHERE vec IS NOT NULL"
    ).fetchone()[0]
    return {"files": files, "chunks": chunks, "embedded": embedded}


if __name__ == "__main__":
    import sys

    started = time.time()
    files, chunks = reindex(force="--force" in sys.argv, verbose=True)
    print(f"indexed {files} files, {chunks} chunks in {time.time()-started:.1f}s")
    print(stats())

    query = " ".join(a for a in sys.argv[1:] if not a.startswith("--"))
    if query:
        text, sources = answer(query)
        print(f"\nQ: {query}\nA: {text}\n")
        for source in sources:
            print(f"  - {source['title']} ({source['score']:.4f})")
