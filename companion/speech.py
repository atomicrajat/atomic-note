#!/usr/bin/env python3
"""Turning an answer into something worth listening to.

Two separate problems live here, and conflating them is why the first version
sounded like a train announcement.

**The voice.** macOS `say` with Samantha is a concatenative synthesiser from
2009. No amount of prompt work makes it sound present. Kokoro-82M is a small
neural model that runs in about a second on Apple silicon and sounds like a
person, so it is the default and `say` is the fallback for when it is not
installed.

**The words.** A model writing for a screen and a model writing for a speaker
should not produce the same string. Text that reads fine — "e.g.", "~3", a
bulleted list, a bare URL — is actively bad out loud. `naturalize()` rewrites
the surface of an answer for the ear: markdown stripped, abbreviations spoken,
list structure turned into spoken cadence. The model is separately asked for a
conversational register in `knowledge.SPOKEN_SYSTEM`; this handles what the
model cannot, because it is a property of the text rather than of the writing.

Output is always 16 kHz mono 16-bit with a canonical 44-byte header. The
device's player seeks a flat 44 bytes rather than walking RIFF chunks, so
anything with a JUNK or LIST chunk plays as noise. Normalising here means the
firmware never has to care which engine spoke.
"""

import io
import os
import re
import subprocess
import sys
import tempfile
import threading
import wave

# What the device's player expects. Not negotiable downstream, so every engine
# is resampled to it here rather than each caller checking.
DEVICE_RATE = 16000

ENGINE = os.environ.get("ATOMIC_TTS", "kokoro")
VOICE = os.environ.get("ATOMIC_TTS_VOICE", "af_heart")
SPEED = float(os.environ.get("ATOMIC_TTS_SPEED", "1.0"))

# `say` is only ever the fallback now, but the voice stays configurable because
# a machine with Premium voices downloaded does considerably better than the
# default.
SAY_VOICE = os.environ.get("ATOMIC_VOICE", "Samantha")
SAY_RATE_WPM = int(os.environ.get("ATOMIC_VOICE_WPM", "180"))

# Where the Kokoro weights land. Kept out of the repo and out of site-packages:
# they are ~350 MB and belong with the other per-machine state.
TTS_HOME = os.path.expanduser("~/.atomic-note/tts")
KOKORO_MODEL_URL = (
    "https://github.com/thewh1teagle/kokoro-onnx/releases/download/"
    "model-files-v1.0/kokoro-v1.0.onnx"
)
KOKORO_VOICES_URL = (
    "https://github.com/thewh1teagle/kokoro-onnx/releases/download/"
    "model-files-v1.0/voices-v1.0.bin"
)

SPEAK_MAX_CHARS = 1200

_kokoro = None
_kokoro_lock = threading.Lock()
_kokoro_failed = False


# ── Making text speakable ─────────────────────────────────────────────────

# Ordered, because some rules feed the next: markdown has to go before
# punctuation is inspected, and abbreviations before sentence splitting.
_ABBREVIATIONS = [
    (r"\be\.g\.", "for example"),
    (r"\bi\.e\.", "that is"),
    (r"\betc\.", "and so on"),
    (r"\bvs\.?\b", "versus"),
    (r"\bapprox\.", "approximately"),
    (r"\bw/o(?=\s|$)", "without"),
    (r"\bw/(?=\s)", "with"),
    (r"\baka\b", "also known as"),
    (r"\bFYI\b", "just so you know"),
    (r"\bASAP\b", "as soon as possible"),
    (r"\bTBD\b", "to be decided"),
]

# Currency, which a synthesiser either skips or mangles. "₹1,249" is read as
# "1,249" with the symbol silently dropped — the listener hears a number with
# no unit, which for an expense tracker is the one word that matters. The
# symbol precedes the amount in writing and follows it in speech, so this
# moves it as well as naming it.
# `[\d,]*\d` rather than `[\d,]+`, so the amount must END on a digit. The
# greedy version swallowed the comma after "$45, £20" and produced
# "45, dollars 20 pounds" — the separator became part of the number.
_AMOUNT = r"([\d,]*\d(?:\.\d+)?)"
_CURRENCY = [
    (r"₹\s*" + _AMOUNT, r"\1 rupees"),
    (r"\$\s*" + _AMOUNT, r"\1 dollars"),
    (r"£\s*" + _AMOUNT, r"\1 pounds"),
    (r"€\s*" + _AMOUNT, r"\1 euros"),
    (r"\bRs\.?\s*" + _AMOUNT, r"\1 rupees"),
]

# Initialisms a synthesiser tries to pronounce as words. "UPI" becomes "oopy",
# "EMI" becomes "emmy". Spelling them out is the fix, and the list is explicit
# rather than a rule over capitalisation because a general rule would also
# spell out "OK" and "AI" — and reading "A I" for AI is its own small
# annoyance.
_INITIALISMS = ["UPI", "ATM", "EMI", "GST", "NEFT", "IMPS", "QR", "USB",
                "SD", "OTP", "IFSC", "URL", "CPU", "GPU", "RAM", "PDF"]

# Symbols that read as themselves on a screen and as nothing at all out loud.
_SYMBOLS = [
    (r"~(?=\d)", "about "),
    (r"(?<=\d)\s*%", " percent"),
    (r"(?<=\d)\s*x\b", " times"),
    (r"\s*&\s*", " and "),
    (r"(?<=\s)\+(?=\s)", "plus"),
    (r"(?<=\s)/(?=\s)", " or "),
    (r"(?<=\d)-(?=\d)", " to "),
]


def naturalize(text):
    """Rewrite an answer for the ear rather than the eye.

    Everything here is a transformation the model cannot reliably do for
    itself, because it is about the surface form rather than the content: a
    model told "no markdown" still emits a stray asterisk, and no instruction
    makes "e.g." pronounceable.
    """
    if not text:
        return ""

    out = text.strip()

    # Fenced code and inline code read as gibberish. Say that something was
    # skipped rather than silently dropping content.
    out = re.sub(r"```[\s\S]*?```", " (code omitted) ", out)
    out = re.sub(r"`([^`]*)`", r"\1", out)

    # Links: keep the label, drop the URL. A spoken "h t t p colon slash
    # slash" is the single worst thing a synthesiser can do.
    out = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", out)
    out = re.sub(r"https?://\S+", "a link", out)

    # Wikilinks come back from the knowledge base verbatim.
    out = re.sub(r"\[\[([^\]|]+)\|([^\]]+)\]\]", r"\2", out)
    out = re.sub(r"\[\[([^\]]+)\]\]", r"\1", out)

    out = re.sub(r"^\s{0,3}#{1,6}\s*", "", out, flags=re.M)  # headings
    out = re.sub(r"^\s*>\s?", "", out, flags=re.M)            # quotes

    # Emphasis, matched as the pair it is. Stripping the characters
    # individually turned `set_gain` into `setgain` and `2*3` into `23` —
    # markdown emphasis never sits inside a word, so neither does this.
    out = re.sub(r"\*\*([^*]+)\*\*", r"\1", out)
    out = re.sub(r"(?<![\w*])\*([^*\n]+)\*(?![\w*])", r"\1", out)
    out = re.sub(r"(?<![\w_])__([^_]+)__(?![\w_])", r"\1", out)
    out = re.sub(r"(?<![\w_])_([^_\n]+)_(?![\w_])", r"\1", out)

    # A bulleted list spoken as-is runs every item into the next. A full stop
    # between items is what gives the synthesiser somewhere to breathe.
    def bullet(match):
        item = match.group(1).strip()
        if item and item[-1] not in ".!?,;:":
            item += "."
        return item + " "

    out = re.sub(r"^\s*[-*•]\s+(.*)$", bullet, out, flags=re.M)
    out = re.sub(r"^\s*\d+[.)]\s+(.*)$", bullet, out, flags=re.M)
    out = re.sub(r"^\s*\[[ x]\]\s*", "", out, flags=re.M)

    # Currency before the symbol rules, which would otherwise eat the digits
    # around it, and before abbreviations so "Rs." is not read as a sentence
    # ending.
    for pattern, replacement in _CURRENCY:
        out = re.sub(pattern, replacement, out)

    for pattern, replacement in _ABBREVIATIONS:
        out = re.sub(pattern, replacement, out, flags=re.I)
    for pattern, replacement in _SYMBOLS:
        out = re.sub(pattern, replacement, out)

    for word in _INITIALISMS:
        out = re.sub(rf"\b{word}\b", " ".join(word), out)

    # An em dash is a pause in print and a hard stop in most synthesisers; a
    # comma gets the phrasing right.
    out = re.sub(r"\s*[—–]\s*", ", ", out)
    out = out.replace("…", ".")

    # Collapse the whitespace last, once everything else has had its say.
    out = re.sub(r"[ \t]+", " ", out)
    out = re.sub(r"\n{2,}", ". ", out)
    out = re.sub(r"\n", " ", out)
    out = re.sub(r"\s+([.,;:!?])", r"\1", out)
    out = re.sub(r"\.{2,}", ".", out)
    out = re.sub(r"\s{2,}", " ", out).strip()

    if out and out[-1] not in ".!?":
        out += "."
    return out


# ── WAV plumbing ──────────────────────────────────────────────────────────

def _encode_wav(samples, rate):
    """int16 numpy array -> canonical 44-byte-header WAV bytes."""
    out = io.BytesIO()
    with wave.open(out, "wb") as dst:
        dst.setnchannels(1)
        dst.setsampwidth(2)
        dst.setframerate(rate)
        dst.writeframes(samples.tobytes())
    return out.getvalue()


def _resample(samples, src_rate, dst_rate):
    """Float samples from one rate to another, band-limited.

    Kokoro speaks at 24 kHz and the device listens at 16 kHz. Plain
    interpolation would fold everything above 8 kHz back into the audible band
    as a metallic ring, so the signal is low-passed first. A windowed-sinc FIR
    is a few lines of numpy and entirely adequate for speech; pulling in scipy
    for one filter is not worth the dependency.
    """
    import numpy as np

    if src_rate == dst_rate:
        return samples

    if dst_rate < src_rate:
        # Cutoff slightly under the new Nyquist, so the transition band has
        # somewhere to land instead of clipping the top of the speech.
        cutoff = 0.45 * dst_rate / src_rate
        taps = 63
        n = np.arange(taps) - (taps - 1) / 2.0
        kernel = np.sinc(2 * cutoff * n) * np.hamming(taps)
        kernel /= kernel.sum()
        samples = np.convolve(samples, kernel, mode="same")

    duration = len(samples) / float(src_rate)
    target_len = int(duration * dst_rate)
    if target_len <= 0:
        return np.zeros(0, dtype=np.float32)

    src_x = np.arange(len(samples), dtype=np.float64)
    dst_x = np.linspace(0, len(samples) - 1, target_len)
    return np.interp(dst_x, src_x, samples).astype(np.float32)


def _to_int16(samples):
    import numpy as np

    # Normalise only when the signal would otherwise clip. Scaling every
    # utterance to full scale would make quiet answers as loud as emphatic
    # ones, which is its own kind of unnatural.
    peak = float(np.max(np.abs(samples))) if len(samples) else 0.0
    if peak > 1.0:
        samples = samples / peak
    return (np.clip(samples, -1.0, 1.0) * 32767.0).astype(np.int16)


# ── Kokoro ────────────────────────────────────────────────────────────────

def _download(url, path):
    import urllib.request

    tmp = path + ".part"
    print(f"  downloading {os.path.basename(path)} ...", flush=True)
    with urllib.request.urlopen(url, timeout=300) as response, open(
        tmp, "wb"
    ) as handle:
        while True:
            block = response.read(1 << 20)
            if not block:
                break
            handle.write(block)
    os.replace(tmp, path)


def kokoro_ready():
    """Are the weights on disk? Used by setup to report without downloading."""
    return all(
        os.path.exists(os.path.join(TTS_HOME, name))
        for name in ("kokoro-v1.0.onnx", "voices-v1.0.bin")
    )


def ensure_kokoro_files():
    os.makedirs(TTS_HOME, exist_ok=True)
    model = os.path.join(TTS_HOME, "kokoro-v1.0.onnx")
    voices = os.path.join(TTS_HOME, "voices-v1.0.bin")
    if not os.path.exists(model):
        _download(KOKORO_MODEL_URL, model)
    if not os.path.exists(voices):
        _download(KOKORO_VOICES_URL, voices)
    return model, voices


def _load_kokoro():
    """Load once, on first spoken answer.

    Deferred for the same reason the Whisper model is: the server should answer
    /health immediately, and the one-time cost lands on a request where the
    device is already waiting.
    """
    global _kokoro, _kokoro_failed

    with _kokoro_lock:
        if _kokoro is not None or _kokoro_failed:
            return _kokoro
        try:
            from kokoro_onnx import Kokoro

            model, voices = ensure_kokoro_files()
            print("loading kokoro...", flush=True)
            _kokoro = Kokoro(model, voices)
            print("  kokoro ready", flush=True)
        except Exception as exc:  # noqa: BLE001
            # Never fatal. A missing synthesiser costs the user the audio, and
            # they can still read the answer that is already on the screen.
            print(f"  kokoro unavailable ({exc}); falling back to say", flush=True)
            _kokoro_failed = True
            _kokoro = None
        return _kokoro


def _speak_kokoro(text):
    engine = _load_kokoro()
    if engine is None:
        return _speak_say(text)

    samples, rate = engine.create(text, voice=VOICE, speed=SPEED, lang="en-us")
    samples = _resample(samples, rate, DEVICE_RATE)
    return _encode_wav(_to_int16(samples), DEVICE_RATE)


# ── macOS say ─────────────────────────────────────────────────────────────

def _speak_say(text):
    """The fallback. Re-emitted with a plain header for the device's player.

    `say` writes a WAV with the header padded out to 4096 bytes by a JUNK
    chunk. The firmware seeks a flat 44 bytes, so the raw file plays as noise.
    Rewriting it here is cheaper than teaching a device with 512 KB of RAM to
    walk RIFF chunks.
    """
    if sys.platform != "darwin":
        raise RuntimeError("no speech engine available")

    raw = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
    raw.close()
    try:
        subprocess.run(
            [
                "say",
                "-v", SAY_VOICE,
                "-r", str(SAY_RATE_WPM),
                "-o", raw.name,
                f"--data-format=LEI16@{DEVICE_RATE}",
                text,
            ],
            check=True,
            capture_output=True,
            timeout=120,
        )
        with wave.open(raw.name, "rb") as src:
            if src.getnchannels() != 1 or src.getsampwidth() != 2:
                raise RuntimeError("unexpected synth format")
            frames = src.readframes(src.getnframes())
            rate = src.getframerate()
    finally:
        if os.path.exists(raw.name):
            os.unlink(raw.name)

    out = io.BytesIO()
    with wave.open(out, "wb") as dst:
        dst.setnchannels(1)
        dst.setsampwidth(2)
        dst.setframerate(rate)
        dst.writeframes(frames)
    return out.getvalue()


# ── Piper ─────────────────────────────────────────────────────────────────

def _speak_piper(text):
    binary = os.environ.get("ATOMIC_PIPER", "piper")
    model = os.environ.get("ATOMIC_PIPER_MODEL", "")
    if not model:
        raise RuntimeError("set ATOMIC_PIPER_MODEL to a .onnx voice")

    result = subprocess.run(
        [binary, "--model", model, "--output_file", "-"],
        input=text.encode("utf-8"),
        capture_output=True,
        check=True,
        timeout=120,
    )
    with wave.open(io.BytesIO(result.stdout), "rb") as src:
        frames = src.readframes(src.getnframes())
        rate = src.getframerate()

    import numpy as np

    samples = np.frombuffer(frames, dtype=np.int16).astype(np.float32) / 32768.0
    samples = _resample(samples, rate, DEVICE_RATE)
    return _encode_wav(_to_int16(samples), DEVICE_RATE)


# ── Entry point ───────────────────────────────────────────────────────────

def synthesize(text, natural=True):
    """Speakable WAV bytes for `text`, in whatever engine is configured."""
    spoken = naturalize(text) if natural else text.strip()
    spoken = spoken[:SPEAK_MAX_CHARS]
    if not spoken:
        raise RuntimeError("nothing to say")

    if ENGINE == "say":
        return _speak_say(spoken)
    if ENGINE == "piper":
        return _speak_piper(spoken)
    return _speak_kokoro(spoken)


def describe():
    if ENGINE == "kokoro":
        state = "weights present" if kokoro_ready() else "weights download on first use"
        return f"kokoro ({VOICE}, {state})"
    if ENGINE == "piper":
        return f"piper ({os.environ.get('ATOMIC_PIPER_MODEL', 'no model set')})"
    return f"say ({SAY_VOICE})"


if __name__ == "__main__":
    # `python3 speech.py "some text"` writes /tmp/atomic-speech.wav and plays
    # it, which is the fastest way to audition a voice without a device.
    sample = sys.argv[1] if len(sys.argv) > 1 else (
        "Here's what I found — you noted three things about the drone project "
        "last week, e.g. the ~20% battery drop."
    )
    print(f"engine : {describe()}")
    print(f"spoken : {naturalize(sample)}")
    data = synthesize(sample)
    path = "/tmp/atomic-speech.wav"
    with open(path, "wb") as handle:
        handle.write(data)
    print(f"wrote  : {path} ({len(data)} bytes)")
    if sys.platform == "darwin":
        subprocess.run(["afplay", path], check=False)
