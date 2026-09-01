#!/usr/bin/env python3
"""Two-way sync between the vault and Notion.

The mirror was deliberately one-way: the vault is the truth, Notion is a copy,
and conflict resolution is the part of two-way sync that actually breaks. That
is still the right description of the RISK — it is not the right answer when
the point of having Notion is being able to fix a misheard amount from a phone.

So this exists, and it is scoped to where two-way sync is tractable:

    expenses   fully two-way, on every field
    notes      metadata (title, tag, summary, topics) and deletions
    note body  vault -> Notion only

The body is the exception on purpose. It is generated from the transcript, and
merging prose that both sides have edited is precisely the case with no correct
answer. Everything synced here is a small typed field where "which value is
this" has one.

**How change is detected.** After each sync, what was agreed is written to
`~/.atomic-note/sync-state.json`: Notion's `last_edited_time` for the page, and
a hash of the local record. Next time, either side differing from that is a
change on that side. That is what distinguishes "edited in Notion" from "never
seen before", and without it the first run would look like everything changed
at once.

**Conflicts prefer Notion**, because a Notion edit is someone deliberately
typing into a field, whereas a local change is usually the device appending.
The overwritten local values are written to `~/.atomic-note/sync-conflicts.log`
rather than discarded — a sync that silently loses an edit is worse than one
that refuses to run.

**Deletions require prior state.** A row is only deleted because it is missing
if we have a record of it existing on both sides. Anything else is treated as
new. A first run, a lost state file or a half-configured token can then cost a
duplicate, which is recoverable; the alternative failure mode is deleting a
ledger.
"""

import datetime
import hashlib
import json
import os
import uuid

import expenses
import notion_sync

STATE_PATH = os.path.expanduser("~/.atomic-note/sync-state.json")
CONFLICT_LOG = os.path.expanduser("~/.atomic-note/sync-conflicts.log")

# The fields a person can meaningfully edit on an expense row. `currency`,
# `source_path` and the ids are local bookkeeping and are never taken from
# Notion.
EXPENSE_FIELDS = ["amount", "merchant", "medium", "category", "item", "note",
                  "needs_review", "when"]


def load_state():
    try:
        with open(STATE_PATH) as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        data = {}
    data.setdefault("expenses", {})
    data.setdefault("notes", {})
    return data


def save_state(state):
    os.makedirs(os.path.dirname(STATE_PATH), exist_ok=True)
    tmp = STATE_PATH + ".tmp"
    with open(tmp, "w") as handle:
        json.dump(state, handle, indent=2)
    os.replace(tmp, STATE_PATH)


def row_hash(record):
    """A fingerprint of just the syncable fields."""
    payload = json.dumps(
        {k: record.get(k) for k in EXPENSE_FIELDS},
        sort_keys=True, ensure_ascii=False, default=str,
    )
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()[:16]


def log_conflict(what, kept, discarded):
    os.makedirs(os.path.dirname(CONFLICT_LOG), exist_ok=True)
    with open(CONFLICT_LOG, "a", encoding="utf-8") as handle:
        handle.write(json.dumps({
            "at": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
            "what": what,
            "kept_from_notion": kept,
            "overwritten_local": discarded,
        }, ensure_ascii=False, default=str) + "\n")


def _minute(value):
    """A timestamp truncated to the minute, for comparison only.

    Notion's date property has minute resolution and hands back
    "…T12:01:00.000+05:30" for a local "…T12:01:49+05:30". Compared literally
    that reads as an edit on every single sync — flagging a conflict, and
    quietly throwing away the seconds each time. The two values describe the
    same moment, so they are compared as one.
    """
    text = str(value or "")
    return text[:16] if len(text) >= 16 else text


def _normalise_when(value, fallback):
    """Notion dates come back as a date or a datetime; keep them comparable."""
    if not value:
        return fallback
    text = str(value)
    if len(text) == 10:  # a bare date — keep the local time of day
        return text + fallback[10:] if len(fallback) > 10 else text
    # Same minute means unchanged: keep the local value, seconds and all.
    if _minute(text) == _minute(fallback):
        return fallback
    return text


def _apply_remote(local, remote):
    """Copy the editable fields of a Notion row onto a local record."""
    changed = []
    for field in EXPENSE_FIELDS:
        new = remote.get(field)
        if field == "when":
            new = _normalise_when(new, local.get("when", ""))
        if field == "amount" and new is not None:
            new = round(float(new), 2)
        if field == "needs_review":
            new = bool(new)
        if new in (None, "") and field not in ("note", "item", "merchant"):
            continue  # a cleared select is not a deliberate blanking of data
        if local.get(field) != new:
            changed.append((field, local.get(field), new))
            local[field] = new
    return changed


def _pair_key(amount, merchant, when):
    """What makes two rows 'the same purchase' across the two systems."""
    try:
        value = round(float(amount), 2)
    except (TypeError, ValueError):
        return None
    return (value,
            (merchant or "").strip().lower(),
            str(when or "")[:10])


def adopt_existing(local, remote, known, verbose=False):
    """Pair up rows that already exist on both sides but were never linked.

    Every ledger row written before two-way sync existed has no page id, and
    every Notion row created by the old one-way mirror was never recorded
    against one. Left alone, the first sync would see a local row with nothing
    to update and push a second copy of something already there — the first
    run would duplicate the entire ledger.

    Matching on amount, merchant and date is enough here: two genuinely
    distinct purchases identical in all three on the same day are rare, and the
    cost of being wrong is a mislinked row rather than a lost one.
    """
    claimed = {r.get("notion_page_id") for r in local if r.get("notion_page_id")}
    by_key = {}
    for page_id, rem in remote.items():
        if page_id in claimed:
            continue
        key = _pair_key(rem.get("amount"), rem.get("merchant"), rem.get("when"))
        if key:
            by_key.setdefault(key, []).append(page_id)

    adopted = 0
    for row in local:
        if row.get("notion_page_id"):
            continue
        key = _pair_key(row.get("amount"), row.get("merchant"), row.get("when"))
        candidates = by_key.get(key) if key else None
        if not candidates:
            continue
        page_id = candidates.pop(0)
        row["notion_page_id"] = page_id
        adopted += 1
        if verbose:
            print(f"  paired existing: {expenses.money(row.get('amount'))} "
                  f"{row.get('merchant') or ''}", flush=True)
    return adopted


def sync_expenses(verbose=False, dry_run=False):
    """Reconcile the ledger and the Notion expenses database, both ways."""
    if not notion_sync.expenses_enabled():
        return {"skipped": "expenses database not configured"}

    settings = notion_sync.config()
    database_id = settings["expenses_database_id"]

    state = load_state()
    known = state["expenses"]

    local = expenses.load()
    for row in local:
        row.setdefault("id", uuid.uuid4().hex)
        row.setdefault("notion_page_id", "")

    remote = {}
    for page in notion_sync.query_all(database_id):
        row = notion_sync.read_expense_page(page)
        remote[row["notion_page_id"]] = row

    # Before anything is created, link up what already exists on both sides.
    adopted = adopt_existing(local, remote, known, verbose=verbose)

    report = {"pushed": 0, "pulled": 0, "created_here": 0, "paired": adopted,
              "deleted_here": 0, "archived_there": 0, "conflicts": 0}
    kept = []

    for row in local:
        page_id = row.get("notion_page_id")

        # Never mirrored — create it.
        if not page_id:
            if not dry_run:
                page_id, _url, edited = notion_sync.push_expense_row(
                    row, database_id)
                row["notion_page_id"] = page_id
                known[page_id] = {"edited": edited, "hash": row_hash(row)}
            report["pushed"] += 1
            kept.append(row)
            continue

        if page_id not in remote:
            # Gone from Notion. Only believe that if we have seen it there.
            if page_id in known:
                report["deleted_here"] += 1
                if verbose:
                    print(f"  deleted here: {expenses.money(row.get('amount'))} "
                          f"{row.get('merchant') or ''} (removed in Notion)",
                          flush=True)
                continue
            kept.append(row)
            continue

        rem = remote[page_id]
        prev = known.get(page_id, {})
        local_changed = row_hash(row) != prev.get("hash")
        remote_changed = rem["notion_edited"] != prev.get("edited")

        if remote_changed:
            before = {f: row.get(f) for f in EXPENSE_FIELDS}
            changes = _apply_remote(row, rem)
            if changes:
                report["pulled"] += 1
                # A conflict needs BOTH sides to have moved since a known
                # agreement. With no prior state there is no agreement to have
                # diverged from — that is a first pairing, not a disagreement,
                # and reporting it as one would cry wolf on every new install.
                if local_changed and prev:
                    report["conflicts"] += 1
                    log_conflict(f"expense {row.get('id')}", rem, before)
                if verbose:
                    for field, old, new in changes:
                        print(f"  pulled {field}: {old!r} -> {new!r}", flush=True)
        elif local_changed:
            if not dry_run:
                updated = notion_sync.update_page(
                    page_id, notion_sync.expense_properties(row))
                # Take the NEW timestamp. Keeping the one read before this
                # write would make the next sync mistake our own push for a
                # Notion edit and pull it straight back — which is exactly how
                # a local change came back reverted in testing.
                rem["notion_edited"] = updated.get("last_edited_time",
                                                   rem["notion_edited"])
            report["pushed"] += 1
            if verbose:
                print(f"  pushed {expenses.money(row.get('amount'))} "
                      f"{row.get('merchant') or ''}", flush=True)

        if not dry_run:
            known[page_id] = {"edited": rem["notion_edited"],
                              "hash": row_hash(row)}
        kept.append(row)

    # Pages in Notion with nothing local behind them.
    seen = {r.get("notion_page_id") for r in kept}
    for page_id, rem in remote.items():
        if page_id in seen:
            continue
        if page_id in known:
            # We created it and the local row is gone: mirror the deletion.
            report["archived_there"] += 1
            if not dry_run:
                notion_sync.archive_page(page_id)
                known.pop(page_id, None)
            if verbose:
                print(f"  archived in Notion: {rem.get('item') or 'expense'}",
                      flush=True)
            continue

        # Typed straight into Notion — adopt it as a ledger row. It has no
        # source note, which reconcile() already knows to leave alone.
        row = {
            "id": uuid.uuid4().hex,
            "currency": expenses.CURRENCY,
            "source": "", "source_path": "",
            "notion_page_id": page_id,
        }
        _apply_remote(row, rem)
        row.setdefault("when",
                       datetime.datetime.now().astimezone().isoformat(timespec="seconds"))
        row.setdefault("needs_review", False)
        kept.append(row)
        report["created_here"] += 1
        if not dry_run:
            known[page_id] = {"edited": rem["notion_edited"], "hash": row_hash(row)}
        if verbose:
            print(f"  new from Notion: {expenses.money(row.get('amount'))} "
                  f"{row.get('merchant') or ''}", flush=True)

    if not dry_run:
        expenses._rewrite(kept)
        expenses.rebuild_all()
        save_state(state)

    return report


def describe(report):
    if "skipped" in report:
        return report["skipped"]
    parts = [f"{v} {k.replace('_', ' ')}" for k, v in report.items() if v]
    return ", ".join(parts) if parts else "already in sync"


if __name__ == "__main__":
    import sys

    dry = "--dry-run" in sys.argv
    result = sync_expenses(verbose=True, dry_run=dry)
    print(("[dry run] " if dry else "") + describe(result))
