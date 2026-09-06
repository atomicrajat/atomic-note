#!/usr/bin/env bash
#
# Start, stop and inspect the companion service.
#
#   ./tools/service.sh status     is it running, what is loaded, what is it costing
#   ./tools/service.sh start      start it now
#   ./tools/service.sh stop       stop it now (comes back at next login)
#   ./tools/service.sh restart    pick up code or config changes
#   ./tools/service.sh disable    stop it AND stop it starting at login
#   ./tools/service.sh enable     undo disable
#   ./tools/service.sh logs       follow the log
#   ./tools/service.sh awake on   keep answering with the lid closed (needs sudo,
#                                 applies on battery too — read what it prints)
#   ./tools/service.sh awake off  undo that
#
# `stop` and `disable` are deliberately different. Stopping is for right now;
# disabling is for "I do not want this on my machine until I say so". Confusing
# the two is how a service you thought you had turned off comes back after a
# reboot.

set -euo pipefail

LABEL="com.atomicnote.companion"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG="$HOME/Library/Logs/atomic-note/companion.log"
PORT="${ATOMIC_PORT:-8710}"
TARGET="gui/$(id -u)/$LABEL"

ok()   { printf '  \033[32m%s\033[0m %s\n' "✓" "$1"; }
no()   { printf '  \033[31m%s\033[0m %s\n' "✗" "$1"; }
warn() { printf '  \033[33m%s\033[0m %s\n' "!" "$1"; }

installed() { [[ -f "$PLIST" ]]; }
loaded()    { launchctl print "$TARGET" >/dev/null 2>&1; }
answering() { curl -s --max-time 5 "http://127.0.0.1:$PORT/health" >/dev/null 2>&1; }

case "${1:-status}" in
  status)
    printf '\n\033[1mCompanion service\033[0m\n'
    installed && ok "installed  ($PLIST)" || { no "not installed — run ./companion/install-service.sh"; exit 1; }
    loaded && ok "starts at login" || warn "disabled — will not start at login"

    if answering; then
      ok "running and answering on port $PORT"
      printf '     %s\n' "$(curl -s --max-time 5 "http://127.0.0.1:$PORT/health")"
    else
      no "not answering on port $PORT"
    fi

    # What it is actually costing right now. The models load on demand and are
    # released when idle, so an idle service is a few tens of MB, not gigabytes.
    printf '\n\033[1mMemory\033[0m\n'
    ps -eo rss,comm,args 2>/dev/null | grep -E "transcribe_server|ollama" | grep -v grep |
      awk '{ mb=$1/1024; total+=mb;
             name = ($0 ~ /transcribe_server/) ? "companion" : "ollama server";
             printf "  %-16s %6.0f MB\n", name, mb }
           END { if (total) printf "  %-16s %6.0f MB\n", "total", total }'

    printf '\n\033[1mModels resident\033[0m\n'
    curl -s --max-time 5 http://127.0.0.1:11434/api/ps 2>/dev/null | python3 -c '
import json, sys
try:
    models = (json.load(sys.stdin) or {}).get("models") or []
except Exception:
    print("  ollama not answering"); raise SystemExit
if not models:
    print("  none — nothing held in RAM until the next question")
for m in models:
    print("  %-24s %5.1f GB" % (m.get("name", "?"), m.get("size", 0) / 1e9))
' 2>/dev/null || echo "  ollama not running"

    printf '\n\033[1mKnowledge base\033[0m\n'
    curl -s --max-time 20 "http://127.0.0.1:$PORT/kb-status" 2>/dev/null | sed 's/^/  /' ||
      echo "  unavailable"
    echo
    ;;

  start)   launchctl kickstart -k "$TARGET" 2>/dev/null || launchctl bootstrap "gui/$(id -u)" "$PLIST"
           ok "started"; ;;
  stop)    launchctl kill SIGTERM "$TARGET" 2>/dev/null || true
           ok "stopped (it will start again at next login — use 'disable' to prevent that)"; ;;
  restart) launchctl kickstart -k "$TARGET"; ok "restarted"; ;;

  disable) launchctl bootout "$TARGET" 2>/dev/null || true
           ok "disabled — stopped, and will not start at login"; ;;
  enable)  launchctl bootstrap "gui/$(id -u)" "$PLIST"
           ok "enabled — running, and will start at login"; ;;

  logs)    printf 'following %s  (ctrl-C to stop)\n\n' "$LOG"; tail -f "$LOG"; ;;

  # Closing the lid is the one thing the service cannot defend itself against.
  #
  # launchd starts it under `caffeinate -i`, which holds an idle-sleep
  # assertion for as long as it runs — so an open laptop sitting untouched
  # stays reachable. Clamshell sleep ignores that assertion entirely, and
  # overriding it means pmset's disablesleep, which is root-only.
  #
  # Two things about disablesleep that its flags actively mislead you about:
  #
  #   1. It is GLOBAL. It accepts -a/-b/-c like every other pmset key and then
  #      ignores them — the value lands in a single SleepDisabled flag that
  #      does not appear under either heading in `pmset -g custom`. Setting it
  #      "for AC only" silently applies on battery too.
  #
  #   2. It suppresses the whole lid-close event, not just the sleep. The
  #      backlight stays lit behind a shut lid, heating the panel against the
  #      keyboard, until something else turns it off.
  #
  # So (2) is handled here rather than left to the user to discover: turning
  # this on also shortens the display-sleep timer, which is what actually
  # darkens the panel once the closed lid stops producing input. The previous
  # timers are saved and put back on the way out.
  awake)
    state="$HOME/Library/Application Support/atomic-note/displaysleep"
    case "${2:-}" in
      on)
        mkdir -p "$(dirname "$state")"
        if [[ ! -f "$state" ]]; then
          pmset -g custom | awk '
            /^Battery/{p="b"} /^AC Power/{p="c"}
            $1=="displaysleep"{print p, $2}' > "$state"
        fi
        sudo pmset -a disablesleep 1
        sudo pmset -a displaysleep "${ATOMIC_LID_DISPLAYSLEEP:-2}"
        ok "the Mac no longer sleeps — lid open or shut, on power or battery"
        ok "display goes dark after ${ATOMIC_LID_DISPLAYSLEEP:-2} min of no input"
        warn "this is not a setting to leave on; 'awake off' when you are done"
        ;;
      off)
        sudo pmset -a disablesleep 0
        if [[ -f "$state" ]]; then
          while read -r src mins; do
            [[ -n "$src" ]] && sudo pmset "-$src" displaysleep "$mins"
          done < "$state"
          rm -f "$state"
          ok "display timers restored"
        fi
        ok "back to normal — closing the lid sleeps the Mac"
        ;;
      *)
        if [[ "$(pmset -g | awk '$1=="SleepDisabled"{print $2}')" == "1" ]]; then
          ok "sleep is disabled — the Mac stays up with the lid shut"
          warn "this applies on battery too; pmset ignores -a/-b/-c here"
        else
          warn "lid closed: the Mac sleeps and the device loses the service"
        fi
        if pmset -g | grep -q "sleep prevented"; then
          ok "idle sleep held off right now (caffeinate, from the service)"
        else
          warn "nothing is holding idle sleep off — is the service running?"
        fi
        printf '\n  %s awake on   to keep it answering with the lid shut\n' "$0"
        ;;
    esac
    ;;

  *) sed -n '3,22p' "$0" | sed 's|^# \{0,1\}||'; exit 1 ;;
esac
