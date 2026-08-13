#!/usr/bin/env bash
set -euo pipefail

bundle=${1:?usage: verify-bundle.sh /path/to/Flick.app}
executable="$bundle/Contents/MacOS/Flick"

test -d "$bundle"
test -x "$executable"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$bundle/Contents/Info.plist")" = "org.flick.Flick"
test "$(/usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersion' "$bundle/Contents/Info.plist")" = "13.0"
document_types=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleDocumentTypes:0:CFBundleTypeExtensions' "$bundle/Contents/Info.plist")
for extension in jpg jpeg png gif bmp webp; do
    grep -q "$extension" <<< "$document_types"
done

architectures=$(lipo -archs "$executable")
[[ " $architectures " == *" arm64 "* ]]
[[ " $architectures " == *" x86_64 "* ]]

test -d "$bundle/Contents/Frameworks/QtWidgets.framework"
test -f "$bundle/Contents/PlugIns/platforms/libqcocoa.dylib"
for plugin in libqjpeg.dylib libqgif.dylib libqwebp.dylib; do
    test -f "$bundle/Contents/PlugIns/imageformats/$plugin"
done

if strings "$executable" | grep -q FLICK_ENABLE_TEST_HARNESS; then
    echo "production bundle contains the test harness marker" >&2
    exit 1
fi

codesign --verify --deep --strict "$bundle"
