# Atomic Note 🎙️ Display-First E-Paper Voice & Knowledge Companion

**Atomic Note** is an open-source, voice-first, distraction-free pocket companion built on an **ESP32-S3 with a 1.54" e-paper display (200×200)**, an **ES8311 audio codec**, an **SD card module**, a **hardware RTC**, and onboard environment sensors.

Hold a button, speak a thought or expense, and Atomic Note automatically structures it, updates your expense ledger, logs it into an **Obsidian vault**, optionally mirrors it to **Notion**, and allows you to query your personal knowledge base out loud.

---

## Key Features

### 🎙️ Instant Voice Capture & Offline Auto-Sync
- **1-Button Recording**: Hold **Button A** to record voice notes instantly.
- **Offline SD Card Storage**: If Wi-Fi is unavailable or you're away from home, voice notes are automatically saved to the onboard SD card.
- **Automatic Background Sync**: As soon as your device connects to your local Wi-Fi, it automatically uploads pending offline recordings to your companion server, transcribing and tagging them in your vault.

### 📊 Expense Tracking & Budget App
- **7-Day Bar Graph**: View your daily spending for the current week (Sunday through Saturday) directly on the e-paper display, complete with numeric amount labels over each bar.
- **Monthly Spend Total**: View your current month's cumulative expenses at a glance.
- **Toggleable Monthly Budget**: Switch to Budget Mode by tapping **Button A** to dial in your monthly budget in ₹1,000 increments. Shows exact budget percentage used.
- **Persistent NVS Cache**: Cached locally in persistent storage (NVS) so the graph displays instantly without waiting for network connectivity.

### 🗣️ Smart Voice Assistant ("Ask") & Natural Language RAG
- **Ask Questions Out Loud**: Ask questions like *"How much did I spend today?"*, *"What are my tasks for this week?"*, or query any note in your vault.
- **Natural Date Range Parsing**: Understood relative periods like *"today's expenses"*, *"yesterday"*, *"this week"*, or *"last month"*.
- **Exact Ledger Arithmetic**: Expense aggregates are precomputed by python companion logic so the model never makes math errors.
- **Neural TTS Output**: Answers are read out loud using **Kokoro-82M** high-quality neural voice synthesis and rendered as formatted text on the e-paper screen.

### 🌐 Built-in Web Portal (Port 80)
- Hosted directly on the ESP32 device (`http://<device-ip>`).
- Manage tasks, toggle completion, set reminders, view note transcripts, configure Wi-Fi credentials, and set companion endpoints without cloud dependencies.

### 🔌 Add-On Sensor Detection
- Auto-probes I2C bus on startup for additional modular sensors (MPU6050/ADXL345 accelerometers, HMC5883 compass, VL53L0X distance sensor, APDS9960 gesture sensor, BME280 environment sensor).

---

## Hardware & Architecture

### ESP32-S3 Firmware (`firmware/atomic_note`)
- **Display Driver**: Custom 1bpp framebuffer `Canvas` driving an SSD1681 200×200 e-paper display.
- **UI Stack**: Non-blocking `Router` driving state machines for screens (`Screen`) with automatic e-paper refresh management and deep-sleep power savings.
- **Audio Codec**: I2S duplex stream (16 kHz mono) via ES8311 codec chip.

### Python Companion Server (`companion/`)
- **Transcriber (`transcribe_server.py`)**: Lightweight HTTP server listening on port `8710`.
- **STT Engine (`speech.py`)**: Local `faster-whisper` (`distil-large-v3`) for fast, offline speech-to-text.
- **LLM Engine (`llm.py`)**: Local **Ollama** server supporting models like `qwen2.5:14b`, `llama3.1:8b`, or `gemma3:12b`.
- **Notion Mirror (`notion_sync.py`)**: Optional background integration for mirroring notes and expenses to custom Notion databases.

---

## Quick Setup Guide

### 1. Firmware Prerequisites & Build

Install `arduino-cli` and required libraries:

```bash
brew install arduino-cli
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.11
arduino-cli lib install "Adafruit GFX Library"
```

Compile and flash the firmware:

```bash
./tools/build.sh     # Compile firmware
./tools/flash.sh     # Compile & upload via USB
./tools/monitor.sh   # Serial monitor (115200 baud)
```

*Note: If auto-detection fails, specify your port: `PORT=/dev/cu.usbmodem101 ./tools/flash.sh`.*

---

### 2. Companion Service Setup

Set up the Python environment and models:

```bash
./companion/setup-knowledge.sh
```

Run the companion server:

```bash
python3 companion/transcribe_server.py
```

### 3. (Optional) Custom Notion Integration

To mirror notes and expenses to your own Notion workspace:
1. Create a Notion Integration token at [notion.so/my-integrations](https://www.notion.so/my-integrations).
2. Create `~/.atomic-note-notion.json` with your credentials:

```json
{
  "token": "secret_YOUR_NOTION_INTEGRATION_TOKEN",
  "database_id": "YOUR_NOTES_DATABASE_ID",
  "expenses_database_id": "YOUR_EXPENSES_DATABASE_ID"
}
```

---

## Directory Overview

```
firmware/atomic_note/
  atomic_note.ino        Boot sequence and screen initialization
  src/board/             Pin definitions and power management
  src/display/           SSD1681 e-paper panel driver & Canvas surface
  src/input/             Non-blocking button state machine
  src/ui/                Screen interface, Router, and Menu UI
  src/apps/              Application screens (Expenses, Ask, Tasks, etc.)
  src/services/          NVS Settings, Storage, RTC, Wi-Fi, Web App
companion/
  transcribe_server.py   Companion HTTP server
  llm.py                 Local Ollama LLM integration
  knowledge.py           Transcript parsing & Obsidian page generator
  expenses.py            Expense ledger math & relative date range parser
  recall.py              Hybrid search RAG pipeline over vault
  speech.py              Kokoro-82M TTS & audio naturalizer
  notion_sync.py         Background queue for Notion database mirroring
tools/                   Build, flash, and serial monitor scripts
docs/                    Hardware specifications and pin maps
```

---

## License

MIT License. Free to use, modify, and build upon.
