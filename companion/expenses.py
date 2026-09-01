#!/usr/bin/env python3
"""Money spent, pulled out of a sentence.

"Paid four fifty at Third Wave on UPI for two cold brews" carries four facts a
ledger needs — amount, merchant, medium, what for — and no expense app will
ever be faster to reach than holding a button. So the device captures it and
this turns it into a row.

**Expense is not Buy.** Buy is a shopping list: something wanted, in the
future, no amount yet. Expense is a record: money already gone, with a number
attached. They want opposite treatment — a Buy note becomes checkboxes to tick
later, an Expense note becomes a line in a ledger that must never be edited by
hand — so they are separate tags rather than one tag and a guess.

**The numbers live in JSONL, not in prose.** Everywhere else in this project
the markdown IS the database, because prose is what markdown is for. Money is
not prose: it has to be summed, grouped and compared, and re-deriving that from
a rendered table means parsing a table, which breaks the first time someone
edits a cell. So `Expenses/ledger.jsonl` inside the vault is the record of
truth and the monthly markdown page is a generated view of it. Still one
folder, still greppable, still syncs with everything else — just in a format
that survives arithmetic.

**Totals are computed here and written into the page.** That is what lets
"how much did I spend on coffee last month" work at all: retrieval finds the
monthly page, and the page already contains the real figures. Asking a 8B model
to add up thirty amounts from thirty retrieved passages produces a confident
wrong number, every time.
"""

import datetime
import json
import os
import re
import uuid

import knowledge
import llm

# Rupees by default: the mediums this is built around — UPI, and the phrasing
# people use with it — are Indian. Everything downstream reads the currency off
# the record rather than assuming, so changing this is enough.
CURRENCY = os.environ.get("ATOMIC_CURRENCY", "INR")
SYMBOLS = {"INR": "₹", "USD": "$", "EUR": "€", "GBP": "£", "JPY": "¥"}

# A guard against a misheard number becoming a five-lakh coffee, not a real
# limit. Anything above it is kept but flagged for review rather than dropped.
IMPLAUSIBLE_ABOVE = float(os.environ.get("ATOMIC_EXPENSE_MAX", "500000"))

# Fixed lists, deliberately. A free-text category column cannot be grouped:
# "food", "Food", "eating out" and "restaurant" become four rows in a summary
# that should have one. The model picks from these or says Other.
MEDIUMS = ["UPI", "Card", "Cash", "Netbanking", "Wallet", "Other"]

# What a purchase was paid with when the note never says.
#
# Not a guess dressed up as data — it is the overwhelmingly common case here,
# and the alternative is a blank column that makes every "how did I pay for
# things" question answer "Unspecified". Anything else gets said out loud,
# because saying "on card" while recording is easier than correcting a row
# afterwards.
DEFAULT_MEDIUM = os.environ.get("ATOMIC_DEFAULT_MEDIUM", "UPI")
CATEGORIES = [
    "Food & Drink", "Groceries", "Transport", "Shopping", "Bills & Utilities",
    "Health", "Entertainment", "Travel", "Home", "Education", "Other",
]

# What people actually say, mapped to what gets stored. The model is asked for
# a canonical medium and mostly obliges; this catches the rest, because "paid
# by GPay" is far more common in a transcript than "paid by UPI".
MEDIUM_ALIASES = {
    "gpay": "UPI", "google pay": "UPI", "phonepe": "UPI", "phone pe": "UPI",
    "paytm": "UPI", "bhim": "UPI", "upi": "UPI", "scan": "UPI",
    "qr": "UPI", "online": "UPI",
    "credit": "Card", "debit": "Card", "card": "Card", "swipe": "Card",
    "visa": "Card", "mastercard": "Card", "rupay": "Card",
    "cash": "Cash", "notes": "Cash",
    "netbanking": "Netbanking", "net banking": "Netbanking",
    "bank transfer": "Netbanking", "neft": "Netbanking", "imps": "Netbanking",
    "wallet": "Wallet", "amazon pay": "Wallet",
}


def expenses_dir():
    return os.path.join(knowledge.VAULT, knowledge.ROOT, "Expenses")


def ledger_path():
    return os.path.join(expenses_dir(), "ledger.jsonl")


# ── Extraction ────────────────────────────────────────────────────────────

EXPENSE_SYSTEM = (
    "You extract spending records from a spoken note. You record only what was "
    "actually said: never invent an amount, a shop or a payment method. "
    "Speech is messy — ignore false starts and keep what the speaker settled "
    "on. One note may contain several separate purchases; return one record "
    "for each. Respond with a single JSON object and nothing else."
)

EXPENSE_PROMPT = """\
A voice note about money already spent, recorded {when}:

\"\"\"
{transcript}
\"\"\"

Return JSON: {{"expenses": [ ... ]}}, one object per distinct purchase, each with:

  "amount"    The number only, as a JSON number. No currency symbol, no commas.
              Spoken forms map to digits: "four fifty" in a shop context is
              450, "twelve hundred" is 1200, "two and a half thousand" is 2500.
              If no amount was said for a purchase, use null.
  "merchant"  The shop, restaurant, service or person paid, exactly as named.
              Empty string if not said. Do not guess from the item.
  "medium"    How it was paid, one of: {mediums}.
              "GPay", "PhonePe", "Paytm", "scanned the QR" all mean UPI.
              Use "Other" only if a method was mentioned but fits none of
              these; use "" if no method was mentioned at all.
  "item"      What was bought or received, in a few words, as said.
  "category"  Exactly one of: {categories}.
  "note"      Anything else said about this purchase that matters, or "".
  "currency"  A 3-letter code, ONLY if a currency was named in WORDS —
              "dollars", "euros", "pounds", "dirhams". Otherwise null.
              Ignore currency SYMBOLS completely: speech-to-text writes "$"
              for a spoken "rupees", so a symbol says nothing about which
              currency was meant. Words are reliable; symbols are not.

Rules:
- Only money ALREADY SPENT. Something the speaker plans to buy is not an
  expense and must be left out entirely.
- Split genuinely separate purchases into separate records. Do not split one
  purchase into its individual items.
- If the note mentions no spending at all, return {{"expenses": []}}.
"""


def _to_amount(value):
    """A positive number, or None. Accepts the several shapes a model returns."""
    if value is None:
        return None
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        amount = float(value)
    else:
        # "₹1,450.50", "Rs 450", "450/-" — strip everything that is not part
        # of a number and see what is left.
        text = re.sub(r"[^\d.]", "", str(value))
        if not text or text.count(".") > 1:
            return None
        try:
            amount = float(text)
        except ValueError:
            return None
    if amount <= 0:
        return None
    return round(amount, 2)


def _canonical_medium(value, transcript=""):
    """The payment method, decided by the TRANSCRIPT rather than the model.

    Told plainly to return "" when no payment method was mentioned, the model
    invents one anyway: "spent 250 at blue tokai for coffee" came back as Cash,
    from a sentence that names no method at all. That is not a wrong guess that
    a better prompt fixes — it is the model filling a required-looking field,
    the same failure as quoting a paraphrase in books.py.
    
    So the transcript decides. If it names a method, that is the method. If it
    names none, the default applies. The model's answer is used only to break a
    tie when the transcript matched more than one.
    """
    text = " " + (transcript or "").lower() + " "

    stated = []
    for alias, medium in MEDIUM_ALIASES.items():
        if re.search(rf"\b{re.escape(alias)}\b", text) and medium not in stated:
            stated.append(medium)
    for medium in MEDIUMS:
        if (re.search(rf"\b{medium.lower()}\b", text)
                and medium not in stated):
            stated.append(medium)

    if not stated:
        # Nothing was said, so nothing is being overridden — this is the
        # documented default, not a guess about this particular purchase.
        return DEFAULT_MEDIUM
    if len(stated) == 1:
        return stated[0]

    # Several matched — "paid the card bill in cash". Let the model choose
    # between what the transcript actually contains, and fall back to the
    # first match if its answer is not one of them.
    claimed = (value or "").strip().lower()
    for medium in stated:
        if claimed == medium.lower():
            return medium
    for alias, medium in MEDIUM_ALIASES.items():
        if claimed and alias in claimed and medium in stated:
            return medium
    return stated[0]


# Only currencies whose names a speaker would actually say. Anything else
# falls back to the configured default rather than being trusted.
_KNOWN_CURRENCIES = {"INR", "USD", "EUR", "GBP", "JPY", "AED", "SGD",
                     "AUD", "CAD", "CHF"}


def _currency_for(value):
    """The currency for a record.

    Defaults to the configured one and only departs from it when the model
    reports a code it was told to give ONLY for a currency named in words.
    The transcript is not trustworthy on symbols: asked to transcribe "three
    hundred and twenty rupees", Whisper writes "$320". Reading that "$" as USD
    would turn every rupee amount into a dollar amount — so symbols are
    ignored entirely and the default stands unless someone said the word.
    """
    code = str(value or "").strip().upper()
    if code in _KNOWN_CURRENCIES:
        return code
    return CURRENCY


def _clean(value, limit=90):
    return re.sub(r"\s+", " ", str(value or "")).strip()[:limit]


def _clean_item(value, medium):
    """What was bought — never how it was paid for.

    With nothing bought actually named, the model fills the item field with
    whatever else it has: "40 rupees at the chai tapri on UPI" came back with
    item "on UPI", which then became the title of a Notion row. An empty item
    is honest and reads fine; a payment method masquerading as a purchase does
    not.
    """
    item = _clean(value, 120)
    if not item:
        return ""

    stripped = re.sub(r"^(on|by|via|through|using|with|in)\s+", "", item, flags=re.I)
    stripped = stripped.strip(" .,-")
    if not stripped:
        return ""

    lowered = stripped.lower()
    if lowered in {m.lower() for m in MEDIUMS} or lowered in MEDIUM_ALIASES:
        return ""
    # "card payment", "upi transfer" — the medium plus a word for paying.
    if re.fullmatch(r"(?:{})\s+(payment|transfer|txn|transaction)".format(
            "|".join(re.escape(a) for a in
                     list(MEDIUM_ALIASES) + [m.lower() for m in MEDIUMS])),
            lowered):
        return ""
    return item


def extract(transcript, when=None):
    """Spending records from a transcript. Never raises; [] when unsure."""
    transcript = (transcript or "").strip()
    when = when or datetime.datetime.now().astimezone()
    if len(transcript) < 8:
        return []

    prompt = EXPENSE_PROMPT.format(
        when=when.strftime("%A %d %B %Y at %H:%M"),
        transcript=transcript[:4000],
        mediums=", ".join(MEDIUMS),
        categories=", ".join(CATEGORIES),
    )
    try:
        data = llm.generate_json(prompt, system=EXPENSE_SYSTEM, max_tokens=700)
    except Exception as exc:  # noqa: BLE001
        print(f"  expense extraction failed: {exc}", flush=True)
        return []

    if not isinstance(data, dict):
        return []
    rows = data.get("expenses")
    if not isinstance(rows, list):
        return []

    records = []
    for row in rows[:10]:
        if not isinstance(row, dict):
            continue
        amount = _to_amount(row.get("amount"))
        merchant = normalise_merchant(_clean(row.get("merchant"), 80))
        medium = _canonical_medium(row.get("medium"), transcript)
        item = _clean_item(row.get("item"), medium)

        # A record with no amount AND nothing bought is not a purchase, it is
        # the model padding the list.
        if amount is None and not item:
            continue

        category = _clean(row.get("category"), 40)
        if category not in CATEGORIES:
            category = "Other"

        records.append({
            "amount": amount,
            "currency": _currency_for(row.get("currency")),
            "merchant": merchant,
            "medium": medium,
            "item": item,
            "category": category,
            "note": _clean(row.get("note"), 160),
            "when": when.isoformat(timespec="seconds"),
            # Filled in by record(), once the note it came from has a path.
            # This is what makes deleting the note delete the expense: without
            # a link back, a row in the ledger has no way of knowing that the
            # thing it describes is gone.
            "source": "",
            "source_path": "",
            # Identity, for two-way sync. `id` is stable across rewrites of
            # the ledger; `notion_page_id` is filled in the first time sync.py
            # mirrors the row, and is what every later edit is matched on.
            "id": uuid.uuid4().hex,
            "notion_page_id": "",
            # Flagged rather than dropped: a misheard "fifteen hundred" as
            # "fifteen thousand" should be visible and fixable, not silently
            # absent from the month's total.
            "needs_review": amount is None or amount > IMPLAUSIBLE_ABOVE,
        })
    return records


# ── Formatting ────────────────────────────────────────────────────────────

def _group_indian(whole):
    """1234567 -> '12,34,567'. Last three digits, then twos."""
    if len(whole) <= 3:
        return whole
    head, tail = whole[:-3], whole[-3:]
    parts = []
    while len(head) > 2:
        parts.insert(0, head[-2:])
        head = head[:-2]
    if head:
        parts.insert(0, head)
    return ",".join(parts) + "," + tail


def money(amount, currency=None):
    """Format an amount the way its currency is actually written."""
    currency = currency or CURRENCY
    symbol = SYMBOLS.get(currency, currency + " ")
    if amount is None:
        return f"{symbol}?"

    # Whole rupees read better than .00 on a 200px panel and in a ledger.
    text = f"{amount:.2f}".rstrip("0").rstrip(".") or "0"
    whole, _, frac = text.partition(".")
    grouped = _group_indian(whole) if currency == "INR" else f"{int(whole):,}"
    return symbol + grouped + (f".{frac}" if frac else "")


def describe_record(record):
    """One line, for the device and for a bullet in the note."""
    bits = [money(record["amount"], record["currency"])]
    if record["merchant"]:
        bits.append(record["merchant"])
    if record["medium"]:
        bits.append(record["medium"])
    if record["item"]:
        bits.append(record["item"])
    line = " · ".join(bits)
    return line + "  (check this)" if record["needs_review"] else line


# ── The ledger ────────────────────────────────────────────────────────────

def append(records):
    """Append to the JSONL ledger. Append-only on purpose: a spend record is
    a historical fact, and rewriting the file to 'fix' one is how a ledger
    silently loses rows."""
    if not records:
        return
    os.makedirs(expenses_dir(), exist_ok=True)
    with open(ledger_path(), "a", encoding="utf-8") as handle:
        for record in records:
            handle.write(json.dumps(record, ensure_ascii=False) + "\n")


def load(month=None, start=None, end=None):
    """Ledger rows, optionally limited to a month or a date range.

    `end` is EXCLUSIVE, so a single day is (d, d+1) and no row can fall in two
    ranges at once.
    """
    try:
        with open(ledger_path(), encoding="utf-8") as handle:
            rows = []
            for line in handle:
                line = line.strip()
                if not line:
                    continue
                try:
                    rows.append(json.loads(line))
                except ValueError:
                    continue  # one bad line must not lose the ledger
    except OSError:
        return []

    if month:
        rows = [r for r in rows if str(r.get("when", "")).startswith(month)]

    if start or end:
        def within(row):
            stamp = str(row.get("when", ""))[:10]
            if not stamp:
                return False
            try:
                when = datetime.date.fromisoformat(stamp)
            except ValueError:
                return False
            if start and when < start:
                return False
            if end and when >= end:
                return False
            return True

        rows = [r for r in rows if within(r)]
    return rows


def totals(records):
    """Sum overall and by category, medium and merchant.

    Records flagged for review are counted — leaving them out would make the
    total quietly disagree with the rows printed beneath it, which is worse
    than a total with a caveat next to it.
    """
    summary = {
        "count": len(records),
        "total": 0.0,
        "unknown": 0,
        "foreign": {},
        "by_category": {},
        "by_medium": {},
        "by_merchant": {},
    }
    for record in records:
        amount = record.get("amount")
        if amount is None:
            summary["unknown"] += 1
            continue

        # Only one currency goes into the total. Adding 40 USD to 3,877 INR
        # gives 3,917 of nothing, and a total that is silently meaningless is
        # exactly the failure this whole module is arranged to avoid. Foreign
        # amounts are summed per currency and reported alongside.
        currency = record.get("currency") or CURRENCY
        if currency != CURRENCY:
            summary["foreign"][currency] = round(
                summary["foreign"].get(currency, 0.0) + amount, 2
            )
            continue

        summary["total"] += amount
        for key, field in (("by_category", "category"),
                           ("by_medium", "medium"),
                           ("by_merchant", "merchant")):
            name = record.get(field) or "Unspecified"
            summary[key][name] = summary[key].get(name, 0.0) + amount
    summary["total"] = round(summary["total"], 2)
    return summary


# ── The monthly page ──────────────────────────────────────────────────────

def _table(records):
    lines = [
        "| Date | Amount | Merchant | Medium | For | Category |",
        "|---|--:|---|---|---|---|",
    ]
    for record in sorted(records, key=lambda r: r.get("when", ""), reverse=True):
        when = str(record.get("when", ""))[:10]
        flag = " ⚠️" if record.get("needs_review") else ""
        lines.append(
            "| {} | {} | {} | {} | {} | {} |".format(
                when,
                money(record.get("amount"), record.get("currency")) + flag,
                record.get("merchant") or "—",
                record.get("medium") or "—",
                record.get("item") or "—",
                record.get("category") or "Other",
            )
        )
    return lines


def _ranked(mapping, limit=None):
    rows = sorted(mapping.items(), key=lambda kv: kv[1], reverse=True)
    return rows[:limit] if limit else rows


def write_month(month, when=None):
    """(Re)generate the page for one month from the ledger.

    Regenerated rather than appended to, because it is derived entirely from
    the JSONL and a stale total is worse than no page. Anything hand-written
    here would be lost, which is why the page says so at the top.
    """
    records = load(month)
    if not records:
        return None

    summary = totals(records)
    label = datetime.datetime.strptime(month, "%Y-%m").strftime("%B %Y")

    lines = [
        "---",
        f'title: "Expenses {month}"',
        "kind: expense-month",
        # Marked so retrieval can tell the model these figures are ALREADY
        # summed. See recall._format_context.
        "totals: precomputed",
        f"month: {month}",
        f"total: {summary['total']}",
        f"currency: {CURRENCY}",
        "---",
        "",
        f"# Expenses — {label}",
        "",
        f"**{money(summary['total'])}** across **{summary['count']}** "
        f"{'entry' if summary['count'] == 1 else 'entries'}.",
        "",
    ]
    if summary["unknown"]:
        lines += [
            f"> {summary['unknown']} entry(s) have no amount and are not in "
            "the total. They are marked ⚠️ below.",
            "",
        ]
    if summary["foreign"]:
        spent = ", ".join(money(a, c) for c, a in sorted(summary["foreign"].items()))
        lines += [
            f"> Also spent {spent}, kept separate — a total mixing currencies "
            "would mean nothing.",
            "",
        ]

    # Totals in prose as well as in the table: this is the part retrieval
    # finds, and a model reading it should not have to add anything up.
    lines.append("## Where it went")
    lines.append("")
    for name, amount in _ranked(summary["by_category"]):
        share = (100.0 * amount / summary["total"]) if summary["total"] else 0
        lines.append(f"- **{name}** — {money(amount)} ({share:.0f}%)")
    lines.append("")

    lines.append("## How it was paid")
    lines.append("")
    for name, amount in _ranked(summary["by_medium"]):
        lines.append(f"- **{name}** — {money(amount)}")
    lines.append("")

    top = _ranked(summary["by_merchant"], 8)
    if top:
        lines.append("## Most spent with")
        lines.append("")
        for name, amount in top:
            lines.append(f"- **{name}** — {money(amount)}")
        lines.append("")

    lines += ["## Every entry", ""] + _table(records) + [""]
    lines += [
        "---",
        "",
        "_Generated from `Expenses/ledger.jsonl` — edits to this page are "
        "overwritten. Correct the ledger instead._",
        "",
    ]

    path = os.path.join(expenses_dir(), f"{month}.md")
    knowledge._atomic_write(path, "\n".join(lines))
    return path


def record(records, when=None, note_path="", note_link=""):
    """Append to the ledger and rebuild the affected month page(s)."""
    if not records:
        return []
    for record_ in records:
        record_["source"] = note_link
        record_["source_path"] = note_path
    append(records)
    months = sorted({str(r.get("when", ""))[:7] for r in records if r.get("when")})
    return [p for p in (write_month(m) for m in months) if p]


# ── The note page ─────────────────────────────────────────────────────────

def as_structured(records, transcript):
    """Shape the expense records like a structured note.

    Reusing `knowledge.render` rather than writing a second renderer: an
    expense is still a note, it still belongs in the day's list and the topic
    index, and it should still be searchable next to everything else. Only the
    body differs, and the body is just its key points.
    """
    total = totals(records)
    if len(records) == 1 and records[0]["merchant"]:
        title = f"{money(records[0]['amount'])} at {records[0]['merchant']}"
    elif len(records) == 1:
        title = f"{money(records[0]['amount'])} — {records[0]['item'] or 'expense'}"
    else:
        title = f"{money(total['total'])} across {len(records)} purchases"

    parts = []
    for record in records:
        piece = money(record["amount"], record["currency"])
        if record["item"]:
            piece += f" on {record['item']}"
        if record["merchant"]:
            piece += f" at {record['merchant']}"
        if record["medium"]:
            piece += f" by {record['medium']}"
        parts.append(piece)

    return {
        "title": title[:90],
        "summary": "Spent " + "; ".join(parts) + ".",
        "key_points": [describe_record(r) for r in records],
        "actions": [],
        "questions": [
            "Confirm the amount — it was not clearly heard."
        ] if any(r["needs_review"] for r in records) else [],
        "entities": [r["merchant"] for r in records if r["merchant"]][:6],
        "topics": sorted({r["category"] for r in records}),
        "structured": True,
        "frontmatter": {
            "expense_total": total["total"],
            "currency": CURRENCY,
            "expense_count": len(records),
        },
    }


def digest(records):
    """What the device shows after an expense note. Confirmation matters more
    here than anywhere else: a wrong amount recorded silently is worse than no
    record at all, so the parsed figure goes back to be checked."""
    lines = [describe_record(r) for r in records]
    if len(records) > 1:
        lines.append(f"Total {money(totals(records)['total'])}")
    return "\n".join(lines)


def format_summary(month=None):
    """Line-oriented totals, for the device and for `curl`.

        month <YYYY-MM> <total> <count> <currency>
        day <0-6> <amount>     # 0=Sunday, current week
        cat <name> <amount>
        med <name> <amount>
        shop <name> <amount>
    """
    month = month or datetime.date.today().strftime("%Y-%m")
    records = load(month)
    summary = totals(records)

    lines = ["month %s %.2f %d %s" % (month, summary["total"],
                                      summary["count"], CURRENCY)]
                                      
    today = datetime.date.today()
    idx = (today.weekday() + 1) % 7
    start_of_week = today - datetime.timedelta(days=idx)
    end_of_week = start_of_week + datetime.timedelta(days=7)
    week_records = load(start=start_of_week, end=end_of_week)
    daily = {i: 0.0 for i in range(7)}
    for r in week_records:
        if r.get("amount") is not None and (r.get("currency") or CURRENCY) == CURRENCY:
            stamp = str(r.get("when", ""))[:10]
            if stamp:
                try:
                    d = datetime.date.fromisoformat(stamp)
                    daily[(d.weekday() + 1) % 7] += r["amount"]
                except ValueError:
                    pass
    for i in range(7):
        lines.append("day %d\t%.2f" % (i, daily[i]))

    for code, amount in sorted(summary["foreign"].items()):
        lines.append("also %s\t%.2f" % (code, amount))
    for name, amount in _ranked(summary["by_category"]):
        lines.append("cat %s\t%.2f" % (name, amount))
    for name, amount in _ranked(summary["by_medium"]):
        lines.append("med %s\t%.2f" % (name, amount))
    for name, amount in _ranked(summary["by_merchant"], 8):
        lines.append("shop %s\t%.2f" % (name, amount))
    return "\n".join(lines) + "\n"


def _rewrite(rows):
    """Replace the ledger wholesale, atomically.

    The ledger is append-only everywhere else, deliberately — a spend record is
    a historical fact and rewriting the file to "fix" one is how a ledger
    silently loses rows. This is the one exception, and it exists because a
    deletion is not a correction: it is the user saying the entry should not be
    there, and honouring that is the whole point.
    """
    os.makedirs(expenses_dir(), exist_ok=True)
    tmp = ledger_path() + ".tmp"
    with open(tmp, "w", encoding="utf-8") as handle:
        for row in rows:
            handle.write(json.dumps(row, ensure_ascii=False) + "\n")
    os.replace(tmp, ledger_path())


def reconcile(verbose=False):
    """Drop ledger rows whose note has been deleted from the vault.

    This is what makes "delete the note in Obsidian" actually remove the
    expense — from the totals, from the month page, and from what the
    assistant will say next time it is asked.

    Rows written before sources were recorded have no `source_path` and are
    kept: there is no way to verify them, and silently dropping a row because
    the software changed would be far worse than keeping one too many.

    Returns the number of rows removed.
    """
    rows = load()
    if not rows:
        return 0

    kept, touched, dropped = [], set(), 0
    for row in rows:
        path = row.get("source_path")
        if path and not os.path.exists(path):
            dropped += 1
            touched.add(str(row.get("when", ""))[:7])
            if verbose:
                print(f"  dropped {money(row.get('amount'))} "
                      f"({row.get('merchant') or 'unspecified'}) — note deleted",
                      flush=True)
            continue
        kept.append(row)

    if not dropped:
        return 0

    _rewrite(kept)
    remaining = {str(r.get("when", ""))[:7] for r in kept}
    for month in touched:
        if month in remaining:
            write_month(month)
        else:
            # Nothing left that month: remove the page rather than leaving one
            # that reports a total of zero.
            page = os.path.join(expenses_dir(), f"{month}.md")
            if os.path.exists(page):
                os.remove(page)
    return dropped


def refresh_stale_months():
    """Regenerate month pages that are older than the ledger.

    Covers the other way a ledger changes: edited by hand. Nothing else would
    notice, and the monthly page — which is what retrieval reads the totals
    from — would keep serving figures that no longer match the records
    underneath it.
    """
    try:
        ledger_mtime = os.path.getmtime(ledger_path())
    except OSError:
        return []

    rebuilt = []
    for month in sorted({str(r.get("when", ""))[:7] for r in load() if r.get("when")}):
        page = os.path.join(expenses_dir(), f"{month}.md")
        try:
            fresh = os.path.getmtime(page) >= ledger_mtime
        except OSError:
            fresh = False
        if not fresh:
            path = write_month(month)
            if path:
                rebuilt.append(path)
    return rebuilt


def sync_from_vault(verbose=False):
    """Bring the ledger and its pages back in line with the vault.

    Cheap: one stat per ledger row plus one per month page, so it can run on
    every reindex — which means before every question.
    """
    dropped = reconcile(verbose=verbose)
    rebuilt = refresh_stale_months()
    return dropped, rebuilt


def rebuild_all():
    """Regenerate every month page from the ledger."""
    months = sorted({str(r.get("when", ""))[:7] for r in load() if r.get("when")})
    return [p for p in (write_month(m) for m in months) if p]


# ── Answering money questions without arithmetic ──────────────────────────
#
# Retrieval plus a prompt rule was tried first and it does not work. Handed the
# monthly summary AND the individual notes, an 8B model asked to split spending
# by payment method produced "Card ₹3,347" against a true ₹2,448; told plainly
# not to add summary figures to line items, it produced "₹2,448 (UPI) and
# ₹1,279 (cash) minus ₹799" — the same wrong answer with its working shown.
#
# The mistake was asking a language model to do sums at all. So it no longer
# does: every aggregate anyone is likely to ask for is computed here, exactly,
# and the model's only job is to pick the right line and write a sentence
# around it. Nothing to add up means nothing to add up wrongly.

_SPEND_WORDS = re.compile(
    r"\b(spen[dt]|spending|cost|costs?|paid|pay|expense|expenses|budget|"
    r"money|rupees?|rs\.?|upi|cash|card|netbanking|wallet|"
    r"afford|outgoing|bill|bills)\b|₹",
    re.I,
)

_MONTH_NAMES = {
    m.lower(): i
    for i, m in enumerate(
        ["January", "February", "March", "April", "May", "June", "July",
         "August", "September", "October", "November", "December"], 1)
}


# "How much this month?" names no money word at all — the subject is implied by
# the conversation. On a device whose main numeric record IS spending, that
# ellipsis is nearly always about money, so "how much" plus a period counts.
#
# Nearly always, not always: "how much time did I spend this week" is a real
# sentence with a different subject, so the obvious non-money nouns are
# excluded rather than being answered with a rupee figure.
_HOW_MUCH = re.compile(r"\bhow much\b", re.I)
_PERIOD_WORDS = re.compile(
    r"\b(today|yesterday|this week|last week|this month|last month|"
    r"this year|last year|so far|past \d+ days|last \d+ days)\b", re.I)
_NOT_MONEY = re.compile(
    r"\bhow much\s+(time|ram|memory|space|storage|disk|water|sleep|"
    r"battery|weight|data)\b", re.I)


def is_spending_question(question):
    """Is this a question about money that the ledger can answer exactly?"""
    text = question or ""
    if _NOT_MONEY.search(text):
        return False
    if _SPEND_WORDS.search(text):
        return True
    return bool(_HOW_MUCH.search(text) and _PERIOD_WORDS.search(text))


_ORDINAL_DAY = re.compile(r"\b(?:on\s+)?the\s+(\d{1,2})(?:st|nd|rd|th)\b", re.I)


def range_for(question, today=None):
    """The dates a spending question is about: (start, end_exclusive, label).

    "How much did I spend today" used to answer with the whole month, because
    the only period this understood was a month — it read the question for a
    month name, found none, and fell back to the current one. The figure was
    real and the answer was wrong, which is the worst shape a wrong answer can
    take.

    Ordered most specific first: "last week" must be tested before "week", and
    an explicit date before anything relative.
    """
    today = today or datetime.date.today()
    text = " " + (question or "").lower() + " "
    day = datetime.timedelta(days=1)

    def span(start, end_inclusive, label):
        return start, end_inclusive + day, label

    # ── An explicit date ─────────────────────────────────────────────────
    iso = re.search(r"\b(20\d{2})-(\d{2})-(\d{2})\b", text)
    if iso:
        when = datetime.date(*(int(p) for p in iso.groups()))
        return span(when, when, when.strftime("%-d %B %Y"))

    month_iso = re.search(r"\b(20\d{2})-(0[1-9]|1[0-2])\b", text)
    if month_iso:
        year, month = int(month_iso.group(1)), int(month_iso.group(2))
        start = datetime.date(year, month, 1)
        end = (start.replace(day=28) + datetime.timedelta(days=4))
        end = end.replace(day=1) - day
        return span(start, end, start.strftime("%B %Y"))

    # ── Relative days ────────────────────────────────────────────────────
    if re.search(r"\btoday(?:'s|s)?\b|\bso far today\b", text):
        return span(today, today, "today")
    if re.search(r"\byesterday(?:'s|s)?\b", text):
        return span(today - day, today - day, "yesterday")

    # ── Weeks ────────────────────────────────────────────────────────────
    # Monday-start, matching how a week is normally meant when spoken.
    monday = today - datetime.timedelta(days=today.weekday())
    if re.search(r"\blast week(?:'s|s)?\b|\bprevious week(?:'s|s)?\b", text):
        start = monday - datetime.timedelta(days=7)
        return span(start, monday - day, "last week")
    if re.search(r"\bthis week(?:'s|s)?\b|\bthe week(?:'s|s)?\b", text):
        return span(monday, today, "this week")
    if re.search(r"\bpast (?:7|seven) days\b|\blast (?:7|seven) days\b", text):
        start = today - datetime.timedelta(days=6)
        return span(start, today, "the last 7 days")

    rolling = re.search(r"\b(?:past|last)\s+(\d{1,3})\s+days\b", text)
    if rolling:
        count = max(1, min(365, int(rolling.group(1))))
        start = today - datetime.timedelta(days=count - 1)
        return span(start, today, f"the last {count} days")

    # ── Months ───────────────────────────────────────────────────────────
    if re.search(r"\blast month(?:'s|s)?\b|\bprevious month(?:'s|s)?\b", text):
        first = today.replace(day=1)
        end = first - day
        return span(end.replace(day=1), end, end.strftime("%B %Y"))

    for name, number in _MONTH_NAMES.items():
        if re.search(rf"\b{name}(?:'s|s)?\b", text):
            year = today.year
            # A month later in the calendar than today is most likely last
            # year, not a prediction about the future.
            if number > today.month:
                year -= 1
            start = datetime.date(year, number, 1)
            end = (start.replace(day=28) + datetime.timedelta(days=4))
            end = end.replace(day=1) - day
            return span(start, end, start.strftime("%B %Y"))

    # ── A bare day-of-month: "on the 5th" ────────────────────────────────
    ordinal = _ORDINAL_DAY.search(text)
    if ordinal:
        number = int(ordinal.group(1))
        if 1 <= number <= 31:
            try:
                when = today.replace(day=number)
            except ValueError:
                when = None
            if when:
                # A day later this month has not happened yet; mean last month.
                if when > today:
                    previous = today.replace(day=1) - day
                    try:
                        when = previous.replace(day=number)
                    except ValueError:
                        when = previous
                return span(when, when, when.strftime("%-d %B"))

    # ── Everything else ──────────────────────────────────────────────────
    start = today.replace(day=1)
    return span(start, today, "this month")


def month_for(question, today=None):
    """The month a question falls in. Kept for callers that want a page."""
    start, _end, _label = range_for(question, today)
    return start.strftime("%Y-%m")


def fact_sheet(month=None, question=""):
    """Every aggregate for the period the question asks about, precomputed.

    Deliberately exhaustive: total, per category, per payment medium, per
    merchant, plus the raw entries. If a figure the question needs is on this
    sheet, the model never has to derive it.

    Returns "" only when there is no ledger at all. A period with nothing in it
    returns a sheet SAYING so — "you spent nothing today" is the right answer
    to "how much did I spend today", and falling through to ordinary retrieval
    there would answer from whatever notes happen to mention money.
    """
    if month:
        start = datetime.datetime.strptime(month, "%Y-%m").date()
        end = (start.replace(day=28) + datetime.timedelta(days=4)).replace(day=1)
        label = start.strftime("%B %Y")
    else:
        start, end, label = range_for(question)

    if not load():
        return ""  # nothing has ever been recorded; not our question to answer

    records = load(start=start, end=end)
    if not records:
        return (f"Exact figures for {label}, computed from the expense "
                f"ledger.\n\nNOTHING was spent in that period: no expenses "
                f"were recorded {label}. The total is zero.")

    summary = totals(records)

    lines = [
        f"Exact figures for {label}, computed from the expense ledger.",
        "Every total below is already summed. Use them as they are.",
        "",
        f"PERIOD: {label} ({start} to {end - datetime.timedelta(days=1)})",
        f"TOTAL SPENT: {money(summary['total'])} across "
        f"{summary['count']} entries.",
        "",
        "BY CATEGORY (these add up to the total):",
    ]
    for name, amount in _ranked(summary["by_category"]):
        lines.append(f"  {name}: {money(amount)}")

    lines += ["", "BY PAYMENT METHOD (these add up to the total):"]
    for name, amount in _ranked(summary["by_medium"]):
        lines.append(f"  {name or 'Unspecified'}: {money(amount)}")

    lines += ["", "BY MERCHANT (these add up to the total):"]
    for name, amount in _ranked(summary["by_merchant"]):
        lines.append(f"  {name or 'Unspecified'}: {money(amount)}")

    lines += ["", "INDIVIDUAL ENTRIES (already included in every total above):"]
    for record in sorted(records, key=lambda r: r.get("when", ""), reverse=True):
        lines.append(
            "  {} — {} at {} by {} for {} [{}]".format(
                str(record.get("when", ""))[:10],
                money(record.get("amount"), record.get("currency")),
                record.get("merchant") or "unspecified",
                record.get("medium") or "unspecified",
                record.get("item") or "unspecified",
                record.get("category") or "Other",
            )
        )
    if summary["unknown"]:
        lines += ["", f"{summary['unknown']} entry(s) had no clear amount and "
                      "are NOT in the totals above."]
    if summary["foreign"]:
        lines += ["", "SPENT IN OTHER CURRENCIES (NOT in the totals above, and "
                      "not to be added to them):"]
        for code, amount in sorted(summary["foreign"].items()):
            lines.append(f"  {money(amount, code)}")
    return "\n".join(lines)


# ── Teaching the transcriber your proper nouns ────────────────────────────
#
# Whisper has never heard of Croma, or a chai tapri. Given real audio it
# returned "Chrome" and "Chaita 3" — the amounts and payment mediums were
# perfect and the shop names were wrong, which for a ledger is the half that
# makes it unsearchable.
#
# `initial_prompt` fixes this: Whisper conditions its decoding on the text,
# so a list of names you actually use makes them far more likely to be
# recognised. The list is built from the merchants already in the ledger, which
# means the system gets better at your shops the more you use it, plus a file
# you can add anything else to.

VOCABULARY_PATH = os.path.expanduser("~/.atomic-note-vocabulary.txt")

# Whisper's prompt window is 224 tokens and the rest is silently discarded.
# Most-spent-with first, so if it truncates it keeps the names that matter.
MAX_VOCABULARY = 48


def known_merchants(limit=MAX_VOCABULARY):
    """Merchants in the ledger, most spent with first."""
    spend = {}
    for record in load():
        name = (record.get("merchant") or "").strip()
        if name:
            spend[name] = spend.get(name, 0.0) + (record.get("amount") or 0.0)
    return [name for name, _ in
            sorted(spend.items(), key=lambda kv: kv[1], reverse=True)[:limit]]


def load_vocabulary():
    """Parse the vocabulary file into (names, aliases).

    Two line shapes:

        Croma                       just a name to expect
        Croma = Chrome, Chroma      a name, and what it gets misheard as

    Aliases are EXPLICIT rather than fuzzy-matched, and that is a decision
    made against data. Measured with difflib on real mis-hearings, the
    correction that should happen — "Chrome" to "Croma" — scores 0.727, while
    one that should NOT — "Amazon" to "Amazon Pay", two different things —
    scores 0.750. The distributions overlap, so no threshold separates them
    and any automatic rule silently merges shops that are not the same shop.
    A ledger that quietly attributes spending to the wrong merchant is worse
    than one with an obvious typo in it.

    So: near-identical spellings are folded automatically (see
    NEAR_IDENTICAL below), and anything further apart is something you tell it
    once, here.
    """
    names, aliases = [], {}
    try:
        with open(VOCABULARY_PATH, encoding="utf-8") as handle:
            lines = handle.readlines()
    except OSError:
        return names, aliases

    for line in lines:
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        canonical, sep, rest = line.partition("=")
        canonical = canonical.strip()
        if not canonical:
            continue
        names.append(canonical)
        if sep:
            for alias in rest.split(","):
                alias = alias.strip()
                if alias:
                    aliases[alias.lower()] = canonical
    return names, aliases


def user_vocabulary():
    return load_vocabulary()[0]


# Only fold spellings this close together. 0.90 catches "Chroma" for "Croma"
# and a doubled letter; it does not reach "Amazon" / "Amazon Pay" at 0.750,
# which is exactly the merge that must not happen automatically.
NEAR_IDENTICAL = 0.90


def normalise_merchant(name):
    """Map a possibly mis-heard merchant onto one you already use.

    Explicit aliases first, then near-identical spellings against names
    already known. Anything else is left exactly as heard — a wrong name you
    can see is fixable, a wrong name silently merged into another shop is not.
    """
    name = (name or "").strip()
    if not name:
        return name

    known, aliases = load_vocabulary()
    if name.lower() in aliases:
        return aliases[name.lower()]

    candidates = known + known_merchants()
    seen = {}
    for candidate in candidates:
        seen.setdefault(candidate.lower(), candidate)
    if name.lower() in seen:
        return seen[name.lower()]

    import difflib

    best, score = None, 0.0
    for candidate in seen.values():
        ratio = difflib.SequenceMatcher(None, name.lower(),
                                        candidate.lower()).ratio()
        if ratio > score:
            best, score = candidate, ratio
    return best if best and score >= NEAR_IDENTICAL else name


def vocabulary_prompt():
    """An initial_prompt for Whisper, or "" if there is nothing to say.

    Written as a sentence rather than a bare list: Whisper conditions on style
    as well as content, and a comma-separated dump of nouns encourages it to
    transcribe in fragments.
    """
    names = []
    for name in user_vocabulary() + known_merchants():
        if name.lower() not in {n.lower() for n in names}:
            names.append(name)
    if not names:
        return ""
    return ("Names that may come up: " + ", ".join(names[:MAX_VOCABULARY]) + ".")
