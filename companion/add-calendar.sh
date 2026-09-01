#!/usr/bin/env bash
#
# Adds a calendar feed to the companion.
#
#   ./companion/add-calendar.sh 'https://calendar.google.com/.../basic.ics'
#   ./companion/add-calendar.sh            (prompts, and does not echo the URL)
#   ./companion/add-calendar.sh --list
#   ./companion/add-calendar.sh --clear
#
# A secret iCal URL is a credential: anyone holding it can read the whole
# calendar. It is written straight to a file in your home directory and never
# passed on the command line unless you choose to.

set -euo pipefail

CONFIG="$HOME/.atomic-note-calendars.json"
PORT="${ATOMIC_PORT:-8710}"
PYTHON="$(for p in $(which -a python3); do
  "$p" -c 'import json' >/dev/null 2>&1 && echo "$p" && break
done)"

case "${1:-}" in
  --list)
    "$PYTHON" - "$CONFIG" <<'PY'
import json, sys
try:
    urls = json.load(open(sys.argv[1])).get("urls", [])
except Exception:
    urls = []
if not urls:
    print("no calendars configured")
for i, u in enumerate(urls):
    # Show enough to identify it, not enough to reuse it.
    print(f"  {i}: {u[:52]}...{u[-12:]}" if len(u) > 64 else f"  {i}: {u}")
PY
    exit 0
    ;;
  --clear)
    echo '{"urls": []}' > "$CONFIG"
    echo "cleared"
    ;;
  "")
    # Read without echoing: this is a secret, and shells keep history.
    printf 'Paste the secret iCal URL (input hidden): '
    read -rs URL
    printf '\n'
    ;;
  *)
    URL="$1"
    ;;
esac

if [[ -n "${URL:-}" ]]; then
  if [[ ! "$URL" =~ ^https?:// ]]; then
    echo "That does not look like a URL." >&2
    exit 1
  fi

  # The Integrate calendar panel offers a PUBLIC address above the secret one,
  # and the two look almost identical. The public one only resolves if the whole
  # calendar is shared publicly, so on a normal private calendar it 404s --
  # which otherwise shows up as a silently empty agenda.
  case "$URL" in
    */public/basic.ics)
      echo >&2
      echo "That is the PUBLIC address; it only works for a public calendar." >&2
      echo "Further down the same panel is 'Secret address in iCal format'." >&2
      echo "The secret one contains /private-<token>/ instead of /public/." >&2
      exit 1
      ;;
  esac
  # Append rather than replace, so several calendars can be merged. Written via
  # Python so the URL is JSON-escaped properly whatever it contains.
  "$PYTHON" - "$CONFIG" "$URL" <<'PY'
import json, os, sys
path, url = sys.argv[1], sys.argv[2]
try:
    data = json.load(open(path))
except Exception:
    data = {}
urls = [u for u in data.get("urls", []) if isinstance(u, str)]
if url not in urls:
    urls.append(url)
json.dump({"urls": urls}, open(path, "w"), indent=2)
os.chmod(path, 0o600)  # readable only by you
print(f"saved ({len(urls)} calendar(s))")
PY
fi

launchctl kickstart -k "gui/$(id -u)/com.atomicnote.companion" 2>/dev/null || true
sleep 4

echo
echo "Events the device will now show:"
curl -s --max-time 40 "http://127.0.0.1:$PORT/calendar" | "$PYTHON" - <<'PY'
import sys, datetime
rows = 0
for line in sys.stdin:
    parts = line.split(None, 3)
    if not parts:
        continue
    if parts[0] == "none":
        print("  no calendars configured"); break
    if parts[0] == "empty":
        print("  none in the next 7 days"); break
    if parts[0] != "event":
        continue
    when = datetime.datetime.fromtimestamp(int(parts[1]))
    tag = "all day" if parts[2] == "1" else when.strftime("%H:%M")
    print(f"  {when.strftime('%a %d %b'):12} {tag:8} {parts[3].strip()}")
    rows += 1
if rows:
    print(f"\n  {rows} event(s)")
PY
