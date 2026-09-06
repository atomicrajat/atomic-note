#!/usr/bin/env bash
#
# Build the menu bar app.
#
#   ./menubar/build.sh            build into menubar/build/
#   ./menubar/build.sh --install  build, then copy to /Applications and launch
#
# Produces a normal .app bundle. It is unsigned, so the first launch needs
# right-click → Open (Gatekeeper blocks a double-click on an unsigned app, and
# says almost nothing useful about why).

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NAME="Atomic Note"
APP="$HERE/build/$NAME.app"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

# LSUIElement is what makes it a menu bar accessory: no dock icon, no app
# switcher entry, no window. Without it macOS treats it as a normal app that
# happens to never open a window, which looks broken.
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>$NAME</string>
  <key>CFBundleDisplayName</key><string>$NAME</string>
  <key>CFBundleIdentifier</key><string>com.atomicnote.menubar</string>
  <key>CFBundleExecutable</key><string>AtomicNoteMenu</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>LSUIElement</key><true/>
</dict>
</plist>
PLIST

echo "compiling..."
swiftc -O \
  -o "$APP/Contents/MacOS/AtomicNoteMenu" \
  "$HERE/AtomicNoteMenu.swift" \
  -framework Cocoa

# Ad-hoc signature. Not a Developer ID, but without any signature at all
# recent macOS refuses to run the binary rather than merely warning.
codesign --force --deep --sign - "$APP" 2>/dev/null || true

echo "built: $APP"

if [[ "${1:-}" == "--install" ]]; then
  rm -rf "/Applications/$NAME.app"
  cp -R "$APP" /Applications/
  echo "installed: /Applications/$NAME.app"
  open "/Applications/$NAME.app"
  echo "look for the microphone icon in your menu bar"
fi
