#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Regenerate every macOS branding image in browser/branding/m6e from the
# M6E-Icon.png artwork in the repository root. Output names never change, so
# replacing the artwork and rerunning this script is the whole update.
#
#   tools/m6e/branding/make-icons.sh [source.png]

set -eu

top=$(cd "$(dirname "$0")/../../.." && pwd)
source=${1:-$top/M6E-Icon.png}
tool=$top/tools/m6e/branding/icon-tool.swift
out=$top/browser/branding/m6e
work=$(mktemp -d "${TMPDIR:-/tmp}/m6e-icons.XXXXXX")
trap 'rm -rf "$work"' EXIT

mkdir -p "$out/content"

swift "$tool" master "$source" "$out/m6e-icon-1024.png"
swift "$tool" document "$out/m6e-icon-1024.png" "$work/document-1024.png"
swift "$tool" dmg-background "$out/background.png" 1440 880

# make_iconset <1024 px png> <output .iconset directory>
make_iconset() {
    mkdir -p "$2"
    for size in 16 32 128 256 512; do
        sips -z $size $size "$1" --out "$2/icon_${size}x${size}.png" >/dev/null
        double=$((size * 2))
        sips -z $double $double "$1" --out "$2/icon_${size}x${size}@2x.png" >/dev/null
    done
}

make_iconset "$out/m6e-icon-1024.png" "$work/app.iconset"
iconutil -c icns "$work/app.iconset" -o "$out/firefox.icns"
cp "$out/firefox.icns" "$out/disk.icns"
make_iconset "$work/document-1024.png" "$work/document.iconset"
iconutil -c icns "$work/document.iconset" -o "$out/document.icns"
sips -s format ico "$work/document.iconset/icon_256x256.png" --out "$out/document.ico" >/dev/null

# Info.plist names the icon AppIcon in an asset catalog (CFBundleIconName).
catalog=$work/Assets.xcassets
appicon=$catalog/AppIcon.appiconset
mkdir -p "$appicon"
printf '{"info":{"version":1,"author":"xcode"}}\n' > "$catalog/Contents.json"
entries=""
for size in 16 32 128 256 512; do
    for scale in 1 2; do
        suffix=""
        if [ $scale = 2 ]; then suffix=@2x; fi
        name=icon_${size}x${size}$suffix.png
        cp "$work/app.iconset/$name" "$appicon/$name"
        entries="$entries{\"idiom\":\"mac\",\"size\":\"${size}x${size}\",\"scale\":\"${scale}x\",\"filename\":\"$name\"},"
    done
done
printf '{"images":[%s],"info":{"version":1,"author":"xcode"}}\n' "${entries%,}" > "$appicon/Contents.json"
xcrun actool --compile "$work" --platform macosx --minimum-deployment-target 10.15 \
    --app-icon AppIcon --output-partial-info-plist "$work/partial.plist" \
    --errors --warnings "$catalog" >/dev/null
cp "$work/Assets.car" "$out/Assets.car"

for size in 16 22 24 32 48 64 128 256; do
    sips -z $size $size "$out/m6e-icon-1024.png" --out "$out/default$size.png" >/dev/null
done
sips -z 192 192 "$out/m6e-icon-1024.png" --out "$out/content/about-logo.png" >/dev/null
sips -z 384 384 "$out/m6e-icon-1024.png" --out "$out/content/about-logo@2x.png" >/dev/null
cp "$out/content/about-logo.png" "$out/content/about-logo-private.png"
cp "$out/content/about-logo@2x.png" "$out/content/about-logo-private@2x.png"
sips -z 236 236 "$out/m6e-icon-1024.png" --out "$work/about.png" >/dev/null
sips -p 236 300 "$work/about.png" --out "$out/content/about.png" >/dev/null

# svg_with_png <png> <size> <output.svg>
svg_with_png() {
    sips -z $2 $2 "$1" --out "$work/embed.png" >/dev/null
    data=$(base64 -i "$work/embed.png" | tr -d '\n')
    printf '<svg xmlns="http://www.w3.org/2000/svg" width="%s" height="%s" viewBox="0 0 %s %s"><image width="%s" height="%s" href="data:image/png;base64,%s"/></svg>\n' \
        $2 $2 $2 $2 $2 $2 "$data" > "$3"
}
svg_with_png "$out/m6e-icon-1024.png" 512 "$out/content/about-logo.svg"
svg_with_png "$work/document-1024.png" 256 "$out/content/document_pdf.svg"

# The wordmark is text in the system font, drawn at display time.
for name in about-wordmark firefox-wordmark; do
    cat > "$out/content/$name.svg" <<'SVG'
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 196 99" fill="context-fill #1d1d1f"><text x="0" y="82" font-family="-apple-system, BlinkMacSystemFont, system-ui, sans-serif" font-size="96" font-weight="700" letter-spacing="-1">M6E</text></svg>
SVG
done

echo "Branding images written to $out"
