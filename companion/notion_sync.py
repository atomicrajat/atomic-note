#!/usr/bin/env python3
"""Mirroring the knowledge base into Notion.

Obsidian is the database; Notion is the window into it from a phone, a work
laptop, or anywhere the vault is not. So this is deliberately a one-way mirror:
the vault is the source of truth, and a page here is a copy. Two-way sync
between a folder of files and a hosted document store is a genuinely hard
problem — conflict resolution, deletes, partial edits — and solving it badly is
worse than not solving it.

**Nothing here is allowed to make the device wait.** Notion is a network call
to someone else's service with its own rate limits and outages; the recording
in front of the user is not contingent on it. Mirroring runs on a background
worker, and anything that fails goes to a queue on disk that is retried later.
A note reaches Notion late or after a restart, never not at all, and never at
the cost of the capture.

Credentials live in ~/.atomic-note-notion.json, alongside the calendar feeds
and for the same reason: an integration token can read and write every page it
is shared with, so it stays on this machine and never goes near the device or
the unauthenticated LAN web app.
"""

import datetime
import json
import os
import queue
import threading
import time
import urllib.error
import urllib.request

CONFIG_PATH = os.path.expanduser("~/.atomic-note-notion.json")
QUEUE_PATH = os.path.expanduser("~/.atomic-note/notion-queue.jsonl")

API = "https://api.notion.com/v1"

# Pinned deliberately. Notion versions its API by date and older versions keep
# working; tracking the newest would mean this file breaking on a day nobody
# touched it.
API_VERSION = "2022-06-28"

# Notion rejects rich_text runs over 2000 characters outright, so a long
# transcript is split rather than truncated.
RICH_TEXT_LIMIT = 1900

_config = None
_config_mtime = 0.0
_queue = queue.Queue()
_worker_started = False
_lock = threading.Lock()
_last_error = ""


def config():
    """Reloaded when the file changes, so adding a token needs no restart."""
    global _config, _config_mtime

    try:
        mtime = os.path.getmtime(CONFIG_PATH)
    except OSError:
        _config = None
        return None

    if _config is not None and mtime == _config_mtime:
        return _config

    try:
        with open(CONFIG_PATH) as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return None

    if not data.get("token") or not data.get("database_id"):
        return None

    _config, _config_mtime = data, mtime
    return _config


def enabled():
    return config() is not None


def expenses_enabled():
    """Expenses go to their OWN database, and only if one is configured.

    Not a view over the notes database: an expense needs a real number column
    for Notion to sum and chart, and a notes table with a mostly-empty Amount
    on it is a worse version of both. Separate `expenses_database_id` in the
    same config file, so the mirror works with or without it.
    """
    settings = config()
    return bool(settings and settings.get("expenses_database_id"))


def check_access(token, database_id):
    """Can this token actually see this database? Raises with the reason.

    Deliberately independent of the config file. Setup has to answer this
    BEFORE saving anything — writing the file first and testing after leaves a
    half-configured mirror on disk when the test fails, which `enabled()` then
    reports as ready and every note afterwards queues against.

    Note that Notion answers 404, not 403, for an object the integration has
    not been granted — it will not confirm that something exists to a caller
    who cannot see it. So "not found" here almost always means "not shared".
    """
    request = urllib.request.Request(
        f"{API}/databases/{database_id}",
        headers={
            "Authorization": f"Bearer {token}",
            "Notion-Version": API_VERSION,
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            return json.loads(response.read())
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", "replace")
        if exc.code == 404:
            raise RuntimeError(
                "the integration cannot see that database. In Notion, open the "
                "database (or a page above it), then ••• -> Connections -> add "
                "your integration."
            ) from exc
        raise RuntimeError(f"Notion {exc.code}: {detail[:200]}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"Notion unreachable: {exc}") from exc


def _request(method, path, payload=None, timeout=30):
    settings = config()
    if not settings:
        raise RuntimeError("Notion is not configured")

    request = urllib.request.Request(
        f"{API}{path}",
        method=method,
        data=json.dumps(payload).encode("utf-8") if payload is not None else None,
        headers={
            "Authorization": f"Bearer {settings['token']}",
            "Notion-Version": API_VERSION,
            "Content-Type": "application/json",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.loads(response.read())
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", "replace")[:300]
        raise RuntimeError(f"Notion {exc.code}: {detail}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"Notion unreachable: {exc}") from exc


# ── Building the page ─────────────────────────────────────────────────────

def _rich(text):
    """Split text into runs Notion will accept."""
    text = text or ""
    return [
        {"type": "text", "text": {"content": text[i:i + RICH_TEXT_LIMIT]}}
        for i in range(0, max(len(text), 1), RICH_TEXT_LIMIT)
    ] or [{"type": "text", "text": {"content": ""}}]


def _paragraph(text):
    return {"object": "block", "type": "paragraph",
            "paragraph": {"rich_text": _rich(text)}}


def _heading(text):
    return {"object": "block", "type": "heading_2",
            "heading_2": {"rich_text": _rich(text)}}


def _bullet(text):
    return {"object": "block", "type": "bulleted_list_item",
            "bulleted_list_item": {"rich_text": _rich(text)}}


def _todo(text):
    return {"object": "block", "type": "to_do",
            "to_do": {"rich_text": _rich(text), "checked": False}}


def _blocks(note, transcript):
    blocks = []
    if note.get("summary"):
        blocks.append({
            "object": "block", "type": "callout",
            "callout": {
                "rich_text": _rich(note["summary"]),
                "icon": {"type": "emoji", "emoji": "🎙️"},
            },
        })

    if note.get("key_points"):
        blocks.append(_heading("Key points"))
        blocks += [_bullet(point) for point in note["key_points"]]

    if note.get("actions"):
        blocks.append(_heading("Actions"))
        blocks += [_todo(action) for action in note["actions"]]

    if note.get("questions"):
        blocks.append(_heading("Open questions"))
        blocks += [_bullet(question) for question in note["questions"]]

    # Collapsed, because the structured note above is what you read and the
    # transcript is what you check it against.
    blocks.append({
        "object": "block", "type": "toggle",
        "toggle": {
            "rich_text": _rich("Transcript"),
            "children": [
                _paragraph(transcript[i:i + RICH_TEXT_LIMIT])
                for i in range(0, max(len(transcript), 1), RICH_TEXT_LIMIT)
            ][:40],
        },
    })
    # Notion caps children at 100 blocks per create call.
    return blocks[:100]


def _properties(note, tag, when, meta):
    props = {
        "Name": {"title": _rich(note.get("title", "Voice note"))},
        "Tag": {"select": {"name": tag}},
        "Captured": {"date": {"start": when.isoformat(timespec="seconds")}},
    }
    if note.get("summary"):
        props["Summary"] = {"rich_text": _rich(note["summary"][:1900])}
    if note.get("topics"):
        props["Topics"] = {
            "multi_select": [
                # Commas are a separator in Notion's multi-select and are
                # rejected in an option name.
                {"name": topic.replace(",", " ")[:100]}
                for topic in note["topics"][:8]
            ]
        }
    if note.get("actions"):
        props["Actions"] = {"number": len(note["actions"])}
    if meta.get("note_number"):
        props["Recording"] = {"number": int(meta["note_number"])}
    return props


# ── The worker ────────────────────────────────────────────────────────────

EXPENSE_SCHEMA = {
    "Item": {"title": {}},
    # A real number with a currency format, so Notion sums it, charts it and
    # shows it as money in every rollup.
    "Amount": {"number": {"format": "rupee"}},
    "Merchant": {"rich_text": {}},
    "Medium": {"select": {"options": [
        {"name": name} for name in
        ("UPI", "Card", "Cash", "Netbanking", "Wallet", "Other")
    ]}},
    "Category": {"select": {"options": [
        {"name": name} for name in (
            "Food & Drink", "Groceries", "Transport", "Shopping",
            "Bills & Utilities", "Health", "Entertainment", "Travel",
            "Home", "Education", "Other",
        )
    ]}},
    "Spent": {"date": {}},
    "Needs review": {"checkbox": {}},
    "Recording": {"number": {"format": "number"}},
    "Note": {"rich_text": {}},
}


def _expense_properties(record, when, meta):
    props = {
        # The title is what the row reads as in a list, so it is what was
        # bought — not the amount, which has its own sortable column.
        "Item": {"title": _rich(record.get("item") or "Expense")},
        "Medium": {"select": {"name": record.get("medium") or "Other"}},
        "Category": {"select": {"name": record.get("category") or "Other"}},
        "Spent": {"date": {"start": record.get("when") or when.isoformat()}},
        "Needs review": {"checkbox": bool(record.get("needs_review"))},
    }
    if record.get("amount") is not None:
        props["Amount"] = {"number": float(record["amount"])}
    if record.get("merchant"):
        props["Merchant"] = {"rich_text": _rich(record["merchant"])}
    if record.get("note"):
        props["Note"] = {"rich_text": _rich(record["note"])}
    if meta.get("note_number"):
        props["Recording"] = {"number": int(meta["note_number"])}
    return props


def _push_expense(job):
    settings = config()
    result = _request("POST", "/pages", {
        "parent": {"database_id": settings["expenses_database_id"]},
        "properties": _expense_properties(
            job["record"], datetime.datetime.fromisoformat(job["when"]),
            job["meta"],
        ),
    })
    return result.get("url", "")


def _push(job):
    """Send one page. Raises on failure so the caller decides about retrying."""
    settings = config()
    payload = {
        "parent": {"database_id": settings["database_id"]},
        "properties": _properties(
            job["note"], job["tag"],
            datetime.datetime.fromisoformat(job["when"]), job["meta"],
        ),
        "children": _blocks(job["note"], job["transcript"]),
    }
    result = _request("POST", "/pages", payload)
    return result.get("url", "")


def _enqueue_to_disk(job):
    os.makedirs(os.path.dirname(QUEUE_PATH), exist_ok=True)
    with open(QUEUE_PATH, "a", encoding="utf-8") as handle:
        handle.write(json.dumps(job) + "\n")


def _drain_disk_queue():
    """Retry everything that failed earlier, oldest first.

    Rewritten wholesale rather than edited in place: the file is small, and a
    partially-consumed queue after a crash would duplicate pages.
    """
    if not os.path.exists(QUEUE_PATH) or not enabled():
        return

    try:
        with open(QUEUE_PATH, encoding="utf-8") as handle:
            jobs = [json.loads(line) for line in handle if line.strip()]
    except (OSError, ValueError):
        return

    remaining = []
    for job in jobs:
        try:
            url = _push_expense(job) if job.get("kind") == "expense" else _push(job)
            print(f"  notion (retry): {url}", flush=True)
        except Exception as exc:  # noqa: BLE001
            job["attempts"] = job.get("attempts", 0) + 1
            # Give up eventually. A job that has failed ten times is not a
            # transient outage, it is a malformed page or a revoked token, and
            # retrying it forever hides the ones that would succeed.
            if job["attempts"] < 10:
                remaining.append(job)
            else:
                label = (job.get("record", {}).get("item")
                         if job.get("kind") == "expense"
                         else job.get("note", {}).get("title"))
                print(f"  notion: giving up on '{label}': {exc}", flush=True)

    tmp = QUEUE_PATH + ".tmp"
    with open(tmp, "w", encoding="utf-8") as handle:
        for job in remaining:
            handle.write(json.dumps(job) + "\n")
    os.replace(tmp, QUEUE_PATH)


def _worker():
    global _last_error

    while True:
        job = _queue.get()
        try:
            if not enabled():
                _enqueue_to_disk(job)  # configured later, sent then
                continue
            if job.get("kind") == "expense":
                url = _push_expense(job)
            else:
                url = _push(job)
            _last_error = ""
            print(f"  notion: {url}", flush=True)
        except Exception as exc:  # noqa: BLE001
            _last_error = str(exc)
            print(f"  notion failed, queued: {exc}", flush=True)
            _enqueue_to_disk(job)
        finally:
            _queue.task_done()


def _retry_loop():
    while True:
        time.sleep(300)
        try:
            _drain_disk_queue()
        except Exception:  # noqa: BLE001 - a retry loop must never die
            pass


def start():
    global _worker_started
    with _lock:
        if _worker_started:
            return
        _worker_started = True
    threading.Thread(target=_worker, daemon=True).start()
    threading.Thread(target=_retry_loop, daemon=True).start()
    threading.Thread(target=_drain_disk_queue, daemon=True).start()


def mirror(note, transcript, tag, when, meta):
    """Queue one note for Notion. Returns immediately, always."""
    start()
    _queue.put({
        "kind": "note",
        "note": note,
        "transcript": transcript[:20000],
        "tag": tag,
        "when": when.isoformat(),
        "meta": meta,
        "attempts": 0,
    })


def mirror_expenses(records, when, meta):
    """Queue one row per purchase. One note can hold several."""
    start()
    for record in records:
        _queue.put({
            "kind": "expense",
            "record": record,
            "when": when.isoformat(),
            "meta": meta,
            "attempts": 0,
        })


# ── Reading back ──────────────────────────────────────────────────────────
# Everything above writes. Two-way sync needs to read as well: what the rows
# currently say, when each was last edited, and which have gone.

def query_all(database_id, page_size=100):
    """Every non-archived page in a database, following pagination.

    Archived pages are simply absent from the result, which is exactly what
    makes a Notion-side deletion detectable: a page we have a record of, that
    the database no longer returns, has been deleted there.
    """
    pages, cursor = [], None
    while True:
        payload = {"page_size": page_size}
        if cursor:
            payload["start_cursor"] = cursor
        body = _request("POST", f"/databases/{database_id}/query", payload)
        pages.extend(body.get("results", []))
        if not body.get("has_more"):
            return pages
        cursor = body.get("next_cursor")
        if not cursor:
            return pages


def _plain(prop):
    """Flatten a rich_text or title property to a string."""
    if not prop:
        return ""
    runs = prop.get("rich_text") or prop.get("title") or []
    return "".join(r.get("plain_text", "") for r in runs).strip()


def _select(prop):
    value = (prop or {}).get("select")
    return (value or {}).get("name", "") or ""


def _multi(prop):
    return [v.get("name", "") for v in (prop or {}).get("multi_select", [])]


def read_expense_page(page):
    """One Notion row, in the shape expenses.py stores.

    `Recording` is read but never written back from here — it identifies the
    device recording a row came from and is not the user's to edit.
    """
    props = page.get("properties", {})
    spent = ((props.get("Spent") or {}).get("date") or {}).get("start") or ""
    return {
        "notion_page_id": page.get("id", ""),
        "notion_edited": page.get("last_edited_time", ""),
        "item": _plain(props.get("Item")),
        "amount": (props.get("Amount") or {}).get("number"),
        "merchant": _plain(props.get("Merchant")),
        "medium": _select(props.get("Medium")),
        "category": _select(props.get("Category")),
        "when": spent,
        "needs_review": bool((props.get("Needs review") or {}).get("checkbox")),
        "note": _plain(props.get("Note")),
        "recording": (props.get("Recording") or {}).get("number"),
    }


def read_note_page(page):
    """One Notion note row's editable metadata.

    The page BODY is deliberately not read. Merging prose that two sides have
    both edited is the part of two-way sync that genuinely does not work, and
    the body is generated from the transcript anyway.
    """
    props = page.get("properties", {})
    return {
        "notion_page_id": page.get("id", ""),
        "notion_edited": page.get("last_edited_time", ""),
        "title": _plain(props.get("Name")),
        "tag": _select(props.get("Tag")),
        "summary": _plain(props.get("Summary")),
        "topics": _multi(props.get("Topics")),
        "recording": (props.get("Recording") or {}).get("number"),
    }


def update_page(page_id, properties):
    """Write properties back to an existing page."""
    return _request("PATCH", f"/pages/{page_id}", {"properties": properties})


def archive_page(page_id):
    """Move a page to Notion's trash. Recoverable there for 30 days."""
    return _request("PATCH", f"/pages/{page_id}", {"archived": True})


def push_expense_row(record, database_id=None):
    """Create one expense page, returning (page_id, url, last_edited_time).

    The timestamp matters as much as the id. Writing to Notion changes the
    page's last_edited_time, so a sync that records the value it read BEFORE
    writing will see its own write as a remote edit next time round — and pull
    it back over whatever was changed locally since.
    """
    settings = config()
    when = datetime.datetime.fromisoformat(
        record.get("when") or datetime.datetime.now().astimezone().isoformat()
    )
    result = _request("POST", "/pages", {
        "parent": {"database_id":
                   database_id or settings["expenses_database_id"]},
        "properties": _expense_properties(record, when, {}),
    })
    return (result.get("id", ""), result.get("url", ""),
            result.get("last_edited_time", ""))


def expense_properties(record):
    """Public wrapper, for the sync engine writing an edit back."""
    when = datetime.datetime.fromisoformat(
        record.get("when") or datetime.datetime.now().astimezone().isoformat()
    )
    return _expense_properties(record, when, {})


# ── Setup ─────────────────────────────────────────────────────────────────

SCHEMA = {
    "Name": {"title": {}},
    "Tag": {"select": {"options": [
        {"name": name} for name in ("Note", "Idea", "Task", "Buy", "Work")
    ]}},
    "Captured": {"date": {}},
    "Summary": {"rich_text": {}},
    "Topics": {"multi_select": {}},
    "Actions": {"number": {"format": "number"}},
    "Recording": {"number": {"format": "number"}},
}


def create_expense_database(token, parent_page_id, title="Expenses"):
    """Create the expense ledger's Notion mirror. Used once, by setup."""
    return _create(token, parent_page_id, title, EXPENSE_SCHEMA)


def create_database(token, parent_page_id, title="Atomic Note"):
    """Create the mirror database under a page the integration can see.

    Used once, by setup. Returns the new database id, which goes into the
    config file next to the token.
    """
    return _create(token, parent_page_id, title, SCHEMA)


def _create(token, parent_page_id, title, schema):
    request = urllib.request.Request(
        f"{API}/databases",
        method="POST",
        data=json.dumps({
            "parent": {"type": "page_id", "page_id": parent_page_id},
            "title": [{"type": "text", "text": {"content": title}}],
            "properties": schema,
        }).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {token}",
            "Notion-Version": API_VERSION,
            "Content-Type": "application/json",
        },
    )
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.loads(response.read())["id"]


def pending():
    try:
        with open(QUEUE_PATH, encoding="utf-8") as handle:
            return sum(1 for line in handle if line.strip())
    except OSError:
        return 0


def describe():
    if not enabled():
        return "not configured (~/.atomic-note-notion.json)"
    waiting = pending()
    state = f"ready, {waiting} queued" if waiting else "ready"
    state += ", expenses on" if expenses_enabled() else ", expenses off"
    if _last_error:
        state += f", last error: {_last_error[:60]}"
    return state
