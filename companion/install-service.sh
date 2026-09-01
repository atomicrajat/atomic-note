#!/usr/bin/env bash
#
# Installs the Atomic Note companion as a macOS LaunchAgent, so it starts at
# login and restarts itself if it stops. Without this the service only runs for
# as long as a terminal is open, and the device silently stops transcribing.
#
#   ./companion/install-service.sh            install and start
#   ./companion/install-service.sh uninstall  stop and remove

set -euo pipefail

LABEL="com.atomicnote.companion"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT="$REPO/companion/transcribe_server.py"
LOG_DIR="$HOME/Library/Logs/atomic-note"

if [[ "${1:-}" == "uninstall" ]]; then
  launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
  rm -f "$PLIST"
  echo "removed $LABEL"
  exit 0
fi

# Find an interpreter that actually has the backend installed. `which python3`
# is not enough — the transcription library usually lives in one specific
# environment, and picking the wrong one gives a service that starts and then
# fails on the first request.
PYTHON=""
for candidate in $(which -a python3); do
  if "$candidate" -c "import faster_whisper, numpy" >/dev/null 2>&1; then
    PYTHON="$candidate"
    break
  fi
done
if [[ -z "$PYTHON" ]]; then
  echo "No python3 with faster-whisper found." >&2
  echo "Install it first:  pip3 install faster-whisper numpy" >&2
  echo "Or run:            ./companion/setup-knowledge.sh" >&2
  echo "Or, to use the API instead, set ATOMIC_BACKEND=openai below." >&2
  exit 1
fi

mkdir -p "$LOG_DIR" "$HOME/Library/LaunchAgents"

cat > "$PLIST" <<PLIST_EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>

  <key>ProgramArguments</key>
  <array>
    <string>$PYTHON</string>
    <string>$SCRIPT</string>
  </array>

  <key>WorkingDirectory</key><string>$REPO</string>

  <!-- Start at login, and bring it back if it ever exits. -->
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>

  <!-- Do not spin if it is failing to start. -->
  <key>ThrottleInterval</key><integer>10</integer>

  <key>StandardOutPath</key><string>$LOG_DIR/companion.log</string>
  <key>StandardErrorPath</key><string>$LOG_DIR/companion.log</string>

  <key>EnvironmentVariables</key>
  <dict>
    <key>ATOMIC_BACKEND</key><string>${ATOMIC_BACKEND:-faster-whisper}</string>
    <key>ATOMIC_MODEL</key><string>${ATOMIC_MODEL:-distil-large-v3}</string>
    <key>ATOMIC_PORT</key><string>${ATOMIC_PORT:-8710}</string>

    <!-- Assistant. Ollama is started on demand and stopped when unused, so
         nothing here keeps a model resident between questions.

         ATOMIC_LLM_MODEL is deliberately EMPTY by default. Left unset, the
         service asks Ollama what is installed and picks the best of them, so
         pulling a stronger model is all it takes to upgrade. Setting it here
         pins that choice, and pins it to a name that may not be on the
         machine. -->
    <key>ATOMIC_LLM</key><string>${ATOMIC_LLM:-ollama}</string>
    <key>ATOMIC_LLM_MODEL</key><string>${ATOMIC_LLM_MODEL:-}</string>
    <key>ATOMIC_OLLAMA_IDLE</key><string>${ATOMIC_OLLAMA_IDLE:-600}</string>
    <key>ATOMIC_OLLAMA_KEEP_ALIVE</key><string>${ATOMIC_OLLAMA_KEEP_ALIVE:-4m}</string>

    <!-- Knowledge base. Notes are written under "Atomic Note/" inside this
         vault; nothing else in it is touched. -->
    <key>ATOMIC_VAULT</key><string>${ATOMIC_VAULT:-$HOME/atomicrajat}</string>
    <key>ATOMIC_VAULT_ROOT</key><string>${ATOMIC_VAULT_ROOT:-Atomic Note}</string>

    <!-- Extra vaults to SEARCH but never write to, comma separated. -->
    <key>ATOMIC_VAULT_READ</key><string>${ATOMIC_VAULT_READ:-}</string>

    <!-- Voice. kokoro is a small neural synthesiser; "say" is the fallback. -->
    <key>ATOMIC_TTS</key><string>${ATOMIC_TTS:-kokoro}</string>
    <key>ATOMIC_TTS_VOICE</key><string>${ATOMIC_TTS_VOICE:-af_heart}</string>

    <!-- brew's bin must be on PATH for the service to find ollama. -->
    <key>PATH</key><string>/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin</string>
    <key>HOME</key><string>$HOME</string>
  </dict>
</dict>
</plist>
PLIST_EOF

# Replace any previous copy rather than erroring on a duplicate label.
launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PLIST"

sleep 2
PORT="${ATOMIC_PORT:-8710}"
IP=$("$PYTHON" - <<'PY'
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
try:
    s.connect(("8.8.8.8", 80)); print(s.getsockname()[0])
except OSError:
    print("127.0.0.1")
finally:
    s.close()
PY
)

echo "installed $LABEL"
echo "  python : $PYTHON"
echo "  address: http://$IP:$PORT"
echo "  logs   : $LOG_DIR/companion.log"
echo
if curl -s --max-time 5 "http://127.0.0.1:$PORT/health" >/dev/null 2>&1; then
  echo "service is answering: $(curl -s --max-time 5 http://127.0.0.1:$PORT/health)"
else
  echo "not answering yet - check the log above"
fi
