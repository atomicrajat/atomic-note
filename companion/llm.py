#!/usr/bin/env python3
"""The local model, and the one process allowed to start it.

Three callers now want a model — answering questions, structuring a voice note
into a knowledge-base page, and embedding text for retrieval. Each of them
spawning its own Ollama would mean three servers and three copies of the
weights, so lifecycle lives here and everything else borrows it.

**Model choice is resolved, not hardcoded.** Pinning a name means the service
breaks on a machine that has not pulled it, and quietly stays on a weak model
on a machine that has something far better. So: honour ATOMIC_LLM_MODEL if it
is set, otherwise ask Ollama what is installed and take the best from a ranked
list. A 24 GB Mac running a 14B and an 8 GB Mac running a 7B both work with no
configuration.

**Ollama is started on demand and stopped again.** A 7B holds about 5 GB of
weights resident; a laptop should not carry that between questions. The server
is spawned on first use and shut down after an idle period, and the weights are
told to unload sooner still via keep_alive. If something else already started
Ollama — the menu bar app, `brew services` — that instance is used and left
alone, because stopping a server we did not start is not ours to do.
"""

import json
import os
import re
import shutil
import subprocess
import threading
import time
import urllib.error
import urllib.request

OLLAMA_URL = os.environ.get("ATOMIC_OLLAMA_URL", "http://127.0.0.1:11434")
BACKEND = os.environ.get("ATOMIC_LLM", "ollama")

IDLE_SECONDS = int(os.environ.get("ATOMIC_OLLAMA_IDLE", "600"))
KEEP_ALIVE = os.environ.get("ATOMIC_OLLAMA_KEEP_ALIVE", "4m")

# Ranked best-first. Reasoning quality on short, messy, spoken input is what
# matters here — a voice note is one unpunctuated paragraph with false starts
# in it, and the difference between a 7B and a 14B on that task is the
# difference between a usable page and a mangled one.
CHAT_PREFERENCE = [
    "qwen3:14b",
    "qwen2.5:14b",
    "qwen3:8b",
    "llama3.1:8b",
    "gemma3:12b",
    "qwen2.5:7b",
    "llama3.2:3b",
]

# Retrieval quality is set here more than anywhere else, so it gets its own
# ranking rather than reusing the chat model. An 8B asked to embed is both
# slower and worse than a purpose-built 137M encoder.
EMBED_PREFERENCE = [
    "embeddinggemma",
    "nomic-embed-text",
    "mxbai-embed-large",
    "all-minilm",
]

CHAT_OVERRIDE = os.environ.get("ATOMIC_LLM_MODEL", "")
EMBED_OVERRIDE = os.environ.get("ATOMIC_EMBED_MODEL", "")

_lock = threading.Lock()
_process = None       # only ever set when WE started it
_last_used = 0.0
_chat_model = None
_embed_model = None    # "" once we have looked and found nothing


def alive(timeout=2):
    try:
        with urllib.request.urlopen(f"{OLLAMA_URL}/api/version", timeout=timeout):
            return True
    except (urllib.error.URLError, OSError):
        return False


def ensure():
    """Start Ollama if nothing is answering, and wait until it does."""
    global _process

    with _lock:
        if alive():
            return

        binary = shutil.which("ollama") or "/opt/homebrew/bin/ollama"
        if not os.path.exists(binary):
            raise RuntimeError("Ollama is not installed (brew install ollama)")

        print("starting ollama...", flush=True)
        _process = subprocess.Popen(
            [binary, "serve"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            env={**os.environ, "OLLAMA_FLASH_ATTENTION": "1"},
        )
        for _ in range(40):  # up to 20 seconds
            if alive():
                print("  ollama ready", flush=True)
                return
            time.sleep(0.5)
        raise RuntimeError("Ollama did not start in time")


def stop_if_idle():
    global _process

    with _lock:
        if _process is None:
            return
        if time.time() - _last_used < IDLE_SECONDS:
            return
        print("stopping ollama (idle)", flush=True)
        _process.terminate()
        try:
            _process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            _process.kill()
        _process = None


def start_watchdog():
    def loop():
        while True:
            time.sleep(30)
            try:
                stop_if_idle()
            except Exception:  # noqa: BLE001 - a watchdog must never die
                pass

    threading.Thread(target=loop, daemon=True).start()


def installed():
    """The model names Ollama actually holds, exactly as it reports them."""
    try:
        with urllib.request.urlopen(f"{OLLAMA_URL}/api/tags", timeout=5) as response:
            body = json.loads(response.read())
    except (urllib.error.URLError, OSError, ValueError):
        return set()
    return {m.get("name") for m in body.get("models", []) if m.get("name")}


def _resolve(preference, override, fallback):
    """Pick the best model that is genuinely present.

    Matching has to be exact-first. An earlier version also matched on the base
    name, so a machine holding only `qwen2.5:7b` resolved the preference entry
    `qwen2.5:14b` — same base, different weights — and every request 404'd. A
    model selector that names something not on disk is worse than one that
    picks a weaker model, because the failure is silent until first use.

    Base-name matching survives as a second pass, but it returns the name that
    is INSTALLED rather than the one asked for. That is what lets a preference
    for `qwen3:8b` still find a `qwen3:8b-instruct-q4_K_M` the user pulled.
    """
    if override:
        return override

    names = installed()
    if not names:
        return fallback

    for candidate in preference:
        if candidate in names:
            return candidate

    by_base = {}
    for name in names:
        by_base.setdefault(name.split(":")[0], []).append(name)
    for candidate in preference:
        matches = by_base.get(candidate.split(":")[0])
        if matches:
            return sorted(matches)[0]

    # Nothing preferred is here. Whatever the user has beats failing outright.
    return fallback if fallback in names else sorted(names)[0]


def chat_model():
    global _chat_model
    if _chat_model is None:
        ensure()
        _chat_model = _resolve(CHAT_PREFERENCE, CHAT_OVERRIDE, "qwen2.5:7b")
        print(f"  chat model: {_chat_model}", flush=True)
    return _chat_model


def embed_model():
    """The embedding model, or None if the machine has not pulled one.

    None is a supported state, not an error: retrieval degrades to keyword
    search, which is worse but entirely usable. Making embeddings mandatory
    would mean a 300 MB download stands between a fresh checkout and a working
    device.
    """
    global _embed_model
    if _embed_model is None:
        ensure()
        # No fallback here, deliberately: an embedding model is either
        # installed or it is not. Falling back to whatever is present would
        # silently embed with a chat model, which is slow and much worse.
        names = installed()
        found = EMBED_OVERRIDE or next(
            (
                candidate
                for candidate in EMBED_PREFERENCE
                if candidate in names
                or any(n.split(":")[0] == candidate.split(":")[0] for n in names)
            ),
            "",
        )
        if found and found not in names:
            found = sorted(
                n for n in names if n.split(":")[0] == found.split(":")[0]
            )[0]
        _embed_model = found
        print(
            f"  embed model: {_embed_model or 'none (keyword search only)'}",
            flush=True,
        )
    return _embed_model or None


def _post(path, payload, timeout):
    request = urllib.request.Request(
        f"{OLLAMA_URL}{path}",
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.loads(response.read())
    except urllib.error.HTTPError as exc:
        # Caught before URLError, which is its base class. Ollama puts the
        # useful part in the body — "model X not found, try pulling it first" —
        # and without this it surfaced as a bare 404.
        detail = exc.read().decode("utf-8", "replace")[:200]
        raise RuntimeError(f"Ollama {exc.code}: {detail}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"Ollama request failed: {exc}") from exc


def _strip_thinking(text):
    """Drop a reasoning model's scratchpad.

    Qwen3 and its kin emit <think>...</think> before the answer. Ollama is
    asked to suppress it below, but older builds ignore that flag and an
    unclosed block on a truncated reply would otherwise be shown to the user as
    the answer.
    """
    text = re.sub(r"<think>[\s\S]*?</think>", "", text, flags=re.I)
    text = re.sub(r"^[\s\S]*?</think>", "", text, flags=re.I)
    return text.strip()


def generate(prompt, system=None, max_tokens=400, temperature=0.6,
             fmt=None, timeout=240):
    """One completion from the local model.

    `fmt="json"` constrains decoding to valid JSON, which is what makes the
    note-structuring step reliable enough to write to disk without a repair
    pass.
    """
    global _last_used

    if BACKEND == "openai":
        return _generate_openai(prompt, system, max_tokens, temperature, fmt)

    ensure()
    _last_used = time.time()

    payload = {
        "model": chat_model(),
        "prompt": prompt,
        "stream": False,
        "keep_alive": KEEP_ALIVE,
        # Thinking is off: it costs seconds and hundreds of tokens on tasks
        # this small, and the device is waiting the whole time.
        "think": False,
        "options": {"num_predict": max_tokens, "temperature": temperature},
    }
    if system:
        payload["system"] = system
    if fmt:
        payload["format"] = fmt

    body = _post("/api/generate", payload, timeout)
    return _strip_thinking((body.get("response") or "").strip())


def _generate_openai(prompt, system, max_tokens, temperature, fmt):
    from openai import OpenAI

    client = OpenAI()
    messages = []
    if system:
        messages.append({"role": "system", "content": system})
    messages.append({"role": "user", "content": prompt})

    kwargs = {
        "model": CHAT_OVERRIDE or "gpt-4o-mini",
        "messages": messages,
        "max_tokens": max_tokens,
        "temperature": temperature,
    }
    if fmt == "json":
        kwargs["response_format"] = {"type": "json_object"}
    result = client.chat.completions.create(**kwargs)
    return (result.choices[0].message.content or "").strip()


def generate_json(prompt, system=None, max_tokens=700, temperature=0.2):
    """A completion parsed as JSON, or None if it will not parse.

    Temperature is low because this is extraction rather than writing. None is
    returned rather than raising: every caller has a usable non-LLM fallback,
    and a knowledge base that silently stops accepting notes because a model
    had an off day is worse than one with a plainer page in it.
    """
    raw = generate(
        prompt, system=system, max_tokens=max_tokens,
        temperature=temperature, fmt="json",
    )
    if not raw:
        return None
    try:
        return json.loads(raw)
    except ValueError:
        # Constrained decoding occasionally still wraps the object in prose.
        match = re.search(r"\{[\s\S]*\}", raw)
        if not match:
            return None
        try:
            return json.loads(match.group(0))
        except ValueError:
            return None


def embed(texts):
    """Embeddings for a list of strings, or None if no encoder is installed."""
    global _last_used

    model = embed_model()
    if not model or not texts:
        return None

    ensure()
    _last_used = time.time()
    body = _post(
        "/api/embed",
        {"model": model, "input": texts, "keep_alive": KEEP_ALIVE},
        120,
    )
    vectors = body.get("embeddings")
    if not vectors or len(vectors) != len(texts):
        return None
    return vectors


def describe():
    if BACKEND == "openai":
        return f"openai ({CHAT_OVERRIDE or 'gpt-4o-mini'})"
    if CHAT_OVERRIDE:
        return f"ollama ({CHAT_OVERRIDE}, started on demand)"
    return "ollama (best installed, started on demand)"
