#!/usr/bin/env python3
"""Interactive setup for the Notion mirrors.

Two databases now, and they are separate on purpose: notes are prose and
expenses are numbers, and a notes table with a mostly-empty Amount column is a
worse version of both. Either can be configured without the other.

Nothing is written until it has been read back successfully. Notion answers 404
rather than 403 for an object an integration has not been granted, so "not
found" here nearly always means "not shared" — and a config saved before that
check leaves a mirror that reports itself ready and fails on every note.

    python3 companion/notion_setup.py
"""

import getpass
import json
import os
import re
import sys

import notion_sync

CONFIG_PATH = os.path.expanduser("~/.atomic-note-notion.json")

INTRO = """
Notion mirror setup.

An integration token can read and write every page it is shared with, so it
stays on this machine: written to ~/.atomic-note-notion.json, never sent to the
device or the LAN web app.

  1. https://www.notion.so/my-integrations -> New integration (internal).
  2. Open each database you want mirrored.
  3. In it: ••• -> Connections -> add your integration.
  4. Copy each database's URL or id below.

Leave any answer blank to skip that part.
"""


def as_id(text):
    """A dashed uuid from a URL, a bare id, or nothing."""
    found = re.findall(r"[0-9a-fA-F]{32}", (text or "").replace("-", ""))
    if not found:
        return None
    raw = found[-1]
    return f"{raw[0:8]}-{raw[8:12]}-{raw[12:16]}-{raw[16:20]}-{raw[20:32]}"


def load():
    try:
        with open(CONFIG_PATH) as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return {}


def save(config):
    with open(CONFIG_PATH, "w") as handle:
        json.dump(config, handle, indent=2)
    os.chmod(CONFIG_PATH, 0o600)  # a token is a credential


def verify(token, database_id, label):
    try:
        found = notion_sync.check_access(token, database_id)
    except Exception as exc:  # noqa: BLE001
        print(f"  ✗ {label}: {exc}")
        return None
    title = "".join(r.get("plain_text", "") for r in found.get("title", []))
    print(f'  ✓ {label}: "{title or "(untitled)"}"')
    return title


def ask_database(token, key, label, config):
    current = config.get(key)
    if current:
        # Re-check what is already configured rather than assuming it still
        # works — a database can be un-shared long after it was set up.
        if verify(token, current, f"{label} (already configured)"):
            return
        print(f"    the stored {label} id no longer works; enter a new one or "
              "leave blank to keep it")

    answer = input(f"{label} database id or URL (blank to skip): ").strip()
    if not answer:
        return
    database_id = as_id(answer)
    if not database_id:
        print("  ✗ no 32-character id found in that")
        return
    if verify(token, database_id, label):
        config[key] = database_id


def main():
    print(INTRO)
    config = load()

    token = config.get("token")
    if token:
        # Never echo it back, not even partially — this runs in a terminal
        # whose scrollback outlives the session.
        keep = input("A token is already saved. Reuse it? [Y/n] ").strip().lower()
        if keep in ("n", "no"):
            token = None
    if not token:
        token = getpass.getpass("Integration token (ntn_...): ").strip()
    if not token:
        print("no token, nothing to do")
        return 1
    config["token"] = token

    print()
    ask_database(token, "database_id", "Notes", config)
    ask_database(token, "expenses_database_id", "Expenses", config)

    if not config.get("database_id") and not config.get("expenses_database_id"):
        print("\nNothing verified, so nothing saved.")
        return 1

    save(config)
    print(f"\nSaved to {CONFIG_PATH}")
    print("  notes    :", config.get("database_id") or "not configured")
    print("  expenses :", config.get("expenses_database_id") or "not configured")
    print("\nRestart the companion to pick it up.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
