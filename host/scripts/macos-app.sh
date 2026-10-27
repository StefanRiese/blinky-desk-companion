#!/usr/bin/env bash
# Builds "Buddy Link.app" (buddyd for Intel and Apple Silicon, with icon and menu bar icon)
# and zips it. Runs on macOS only: the menu bar needs cgo (Cocoa), lipo joins the two builds.
#
#   host/scripts/macos-app.sh VERSION OUTDIR      # → OUTDIR/Buddy-Link-macos-universal.zip
#
# The app is signed ad-hoc (no Apple developer certificate yet): macOS asks once before the
# first start (System Settings → Privacy & Security → Open Anyway).
set -euo pipefail

ver="${1:?version}"
out="$(mkdir -p "${2:?output dir}" && cd "$2" && pwd)"
host="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

app="$work/Buddy Link.app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"

for arch in amd64 arm64; do
  (cd "$host" && CGO_ENABLED=1 GOOS=darwin GOARCH="$arch" go build -trimpath \
    -ldflags "-s -w -X main.version=$ver" -o "$work/buddyd-$arch" ./cmd/buddyd)
done
lipo -create -output "$app/Contents/MacOS/buddyd" "$work/buddyd-amd64" "$work/buddyd-arm64"
cp "$host/assets/buddyd.icns" "$app/Contents/Resources/buddyd.icns"

# CFBundleVersion must be numbers and dots: 0.1.3, or 0.0.0 for dev builds.
short="${ver%%[-+]*}"
cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Buddy Link</string>
  <key>CFBundleDisplayName</key><string>Buddy Link</string>
  <key>CFBundleIdentifier</key><string>io.github.stefanriese.buddylink</string>
  <key>CFBundleExecutable</key><string>buddyd</string>
  <key>CFBundleIconFile</key><string>buddyd</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$short</string>
  <key>CFBundleVersion</key><string>$short</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>LSUIElement</key><true/>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSHumanReadableCopyright</key><string>Blinky desk companion — github.com/StefanRiese/blinky-desk-companion</string>
</dict>
</plist>
PLIST

plutil -lint "$app/Contents/Info.plist"
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict "$app"
lipo -info "$app/Contents/MacOS/buddyd"
"$app/Contents/MacOS/buddyd" version

# ditto keeps the bundle, its signature and the executable bit inside the zip.
ditto -c -k --sequesterRsrc --keepParent "$app" "$out/Buddy-Link-macos-universal.zip"
ls -la "$out/Buddy-Link-macos-universal.zip"
