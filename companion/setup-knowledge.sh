#!/usr/bin/env bash
#
# One-time setup for the knowledge base: Python packages, local models, the
# neural voice, and the vault folder.
#
#   ./companion/setup-knowledge.sh           install and report
#   ./companion/setup-knowledge.sh --check    report only, change nothing
#   ./companion/setup-knowledge.sh --notion   configure the Notion mirror
#
# Everything here is optional in the sense that the service degrades rather
# than fails: no embedding model means keyword-only search, no Kokoro means the
# macOS voice, no vault means transcription without filing. This script is what
# turns "works" into "works well".

set -euo pipefail

CHECK_ONLY=false
[[ "${1:-}" == "--check" ]] && CHECK_ONLY=true

VAULT="${ATOMIC_VAULT:-$HOME/AtomicNote}"
VAULT_ROOT="${ATOMIC_VAULT_ROOT:-Atomic Note}"

# Models are ranked in llm.py; these are the two worth having on a machine with
# 16 GB or more. The chat model does the structuring, which is the step whose
# quality you actually see in the notes.
CHAT_MODEL="${ATOMIC_SETUP_CHAT:-qwen3:8b}"
EMBED_MODEL="${ATOMIC_SETUP_EMBED:-embeddinggemma}"

say()  { printf '\n\033[1m%s\033[0m\n' "$*"; }
ok()   { printf '  \033[32m✓\033[0m %s\n' "$*"; }
warn() { printf '  \033[33m!\033[0m %s\n' "$*"; }
bad()  { printf '  \033[31m✗\033[0m %s\n' "$*"; }

# ── Notion ────────────────────────────────────────────────────────────────
# Delegated to a Python helper. The flow grew a second database and the ability
# to reuse an already-saved token, and a bash heredoc feeding a python heredoc
# was the wrong place for either.
if [[ "${1:-}" == "--notion" ]]; then
  HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  PYTHON="$(for p in $(which -a python3); do
    "$p" -c 'import json' >/dev/null 2>&1 && echo "$p" && break
  done)"
  exec "$PYTHON" "$HERE/notion_setup.py"
fi

# ── Python packages ───────────────────────────────────────────────────────
say "Python"

PYTHON="$(for p in $(which -a python3); do
  "$p" -c 'import faster_whisper' >/dev/null 2>&1 && echo "$p" && break
done)"
if [[ -z "$PYTHON" ]]; then
  PYTHON="$(command -v python3)"
  warn "faster-whisper is not installed for $PYTHON"
  if ! $CHECK_ONLY; then
    if "$PYTHON" -m pip install --quiet faster-whisper; then
      ok "installed faster-whisper"
    else
      bad "could not install faster-whisper — transcription will not work"
    fi
  fi
else
  ok "faster-whisper ($PYTHON)"
fi

for pkg in numpy icalendar recurring_ical_events; do
  if "$PYTHON" -c "import $pkg" >/dev/null 2>&1; then
    ok "$pkg"
  elif $CHECK_ONLY; then
    warn "$pkg missing"
  elif "$PYTHON" -m pip install --quiet "${pkg//_/-}"; then
    ok "installed $pkg"
  else
    bad "could not install $pkg"
  fi
done

# Kokoro is the one dependency that is genuinely optional: without it the
# service speaks with the macOS voice, which works and sounds worse.
if "$PYTHON" -c "import kokoro_onnx" >/dev/null 2>&1; then
  ok "kokoro-onnx"
else
  warn "kokoro-onnx missing — answers would be spoken by macOS 'say'"
  if ! $CHECK_ONLY; then
    if "$PYTHON" -m pip install --quiet kokoro-onnx; then
      ok "installed kokoro-onnx"
    else
      bad "kokoro-onnx would not install; macOS 'say' will be used instead"
    fi
  fi
fi

# ── The voice ─────────────────────────────────────────────────────────────
say "Voice"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if "$PYTHON" -c "
import sys; sys.path.insert(0, '$HERE')
import speech; sys.exit(0 if speech.kokoro_ready() else 1)" 2>/dev/null; then
  ok "Kokoro weights present"
elif $CHECK_ONLY; then
  warn "Kokoro weights not downloaded (~350 MB, fetched on first use)"
else
  "$PYTHON" -c "
import sys; sys.path.insert(0, '$HERE')
import speech; speech.ensure_kokoro_files()" && ok "downloaded Kokoro weights"
fi

# ── Local models ──────────────────────────────────────────────────────────
say "Models"
if ! command -v ollama >/dev/null 2>&1; then
  bad "ollama not installed (brew install ollama)"
else
  HAVE="$(ollama list 2>/dev/null | tail -n +2 | awk '{print $1}')"

  for model in "$CHAT_MODEL" "$EMBED_MODEL"; do
    if grep -qx "$model" <<<"$HAVE" || grep -q "^${model%%:*}:" <<<"$HAVE"; then
      ok "$model"
    elif $CHECK_ONLY; then
      warn "$model not pulled"
    else
      echo "  pulling $model ..."
      if ollama pull "$model" >/dev/null; then
        ok "pulled $model"
      else
        bad "could not pull $model"
      fi
    fi
  done
fi

# ── The vault ─────────────────────────────────────────────────────────────
say "Knowledge base"
if [[ -d "$VAULT" ]]; then
  ok "vault $VAULT"
  if ! $CHECK_ONLY; then
    mkdir -p "$VAULT/$VAULT_ROOT/Notes" "$VAULT/$VAULT_ROOT/Topics" "$VAULT/$VAULT_ROOT/Daily"
    ok "notes will be written to $VAULT/$VAULT_ROOT"
  fi
else
  bad "no vault at $VAULT — set ATOMIC_VAULT to your Obsidian vault"
fi

if [[ -f "$HOME/.atomic-note-notion.json" ]]; then
  ok "Notion mirror configured"
else
  warn "Notion mirror not configured — run: $0 --notion"
fi

say "Done"
echo "  Start the service:   python3 companion/transcribe_server.py"
echo "  Or install it:       ./companion/install-service.sh"
echo
