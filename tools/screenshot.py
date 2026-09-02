#!/usr/bin/env python3
"""Capture real screenshots from the device over serial.

Photographing e-paper is a poor way to document a 200x200 layout: room lighting
and the panel's off-white ground turn crisp 1-bit graphics into something grey
and slightly warped. These are the actual framebuffer bytes, so what comes out
is exactly what the Canvas drew.

    ./tools/screenshot.py                  every screen, into docs/screens/
    ./tools/screenshot.py dashboard ask    just those
    ./tools/screenshot.py --list           what can be captured

The device does the navigating (`screen <name>` on its console), so nothing
here needs anyone pressing buttons in the right order.
"""

import argparse
import os
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is needed: pip3 install pyserial")

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is needed: pip3 install Pillow")

BAUD = 115200

# A panel refresh is around half a second, and the screen is drawn from the
# main loop rather than from the command. Waiting less than this captures the
# previous screen, which is a confusing thing to debug.
SETTLE_S = 1.6

DEFAULT_SCREENS = [
    "dashboard", "menu", "notes", "ask", "expenses", "measure",
    "dice", "settings", "tasks", "agenda", "calendar", "timer",
    "links", "banner", "sync", "wifi", "usage", "buttons",
]


def find_port():
    import glob
    ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
    if not ports:
        sys.exit("no device found — is it plugged in and awake?")
    return ports[0]


def read_until(ser, marker, timeout):
    """Lines until `marker`, or None if it never arrives."""
    lines, deadline = [], time.time() + timeout
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", "replace").strip()
        if not line:
            continue
        if line == marker:
            return lines
        lines.append(line)
    return None


def capture(ser, scale):
    ser.reset_input_buffer()
    ser.write(b"shot\n")
    ser.flush()

    header = None
    deadline = time.time() + 8
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", "replace").strip()
        if line.startswith("SHOT "):
            header = line
            break
    if not header:
        return None

    _, width, height = header.split()
    width, height = int(width), int(height)
    rows = read_until(ser, "ENDSHOT", timeout=15)
    if rows is None or len(rows) < height:
        return None

    stride = (width + 7) // 8
    # 0 is black: fill(kBlack) memsets to 0x00, so a set bit is white.
    image = Image.new("1", (width, height), 1)
    pixels = image.load()
    for y in range(height):
        row = bytes.fromhex(rows[y])
        if len(row) != stride:
            return None
        for x in range(width):
            if not (row[x >> 3] & (0x80 >> (x & 7))):
                pixels[x, y] = 0
    if scale > 1:
        image = image.resize((width * scale, height * scale), Image.NEAREST)
    return image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("screens", nargs="*", default=None)
    parser.add_argument("--port")
    parser.add_argument("--out", default="docs/screens")
    parser.add_argument("--scale", type=int, default=2,
                        help="nearest-neighbour upscale; 1-bit art must not "
                             "be smoothed")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    port = args.port or find_port()
    ser = serial.Serial(port, BAUD, timeout=1)
    time.sleep(1.5)
    ser.reset_input_buffer()

    if args.list:
        ser.write(b"screen\n")
        ser.flush()
        time.sleep(0.6)
        print(ser.read_all().decode("utf-8", "replace").strip())
        return 0

    wanted = args.screens or DEFAULT_SCREENS
    os.makedirs(args.out, exist_ok=True)
    print(f"capturing from {port} -> {args.out}/\n")

    saved, failed = 0, []
    for name in wanted:
        ser.reset_input_buffer()
        ser.write(f"screen {name}\n".encode())
        ser.flush()
        time.sleep(SETTLE_S)

        image = capture(ser, args.scale)
        if image is None:
            print(f"  {name:<12} FAILED")
            failed.append(name)
            continue
        path = os.path.join(args.out, f"{name}.png")
        image.save(path)
        print(f"  {name:<12} {image.size[0]}x{image.size[1]}  {path}")
        saved += 1

    # Leave it where it started rather than parked on whatever was last.
    ser.write(b"screen dashboard\n")
    ser.flush()
    ser.close()

    print(f"\n{saved} saved" + (f", {len(failed)} failed: {', '.join(failed)}"
                                if failed else ""))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
