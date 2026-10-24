#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Sign a packaged M6E.app ad hoc (D8) so it launches on Apple Silicon, then
# verify the result and print the entitlements of every code object.
#
#   tools/m6e/package/sign-adhoc.sh [--no-runtime] <path to M6E.app>
#
# First runs finalize-app.sh, which removes the keys that must not ship from
# the Info.plist files, then signs the way Mozilla signs its macOS builds
# (taskcluster/config.yml, mac-signing): no --deep, hardened runtime (the
# default; --no-runtime turns it off), nested code first and the bundle last.
# The entitlements are Mozilla's release set (security/mac/hardenedruntime/
# production/firefox.browser.xml and the helper files next to it) minus
# application-identifier and web-browser.public-key-credential, which name
# Mozilla's Team ID and need a provisioning profile from a paid Apple
# Developer account. (The nightly and Developer Edition sets also carry
# application-groups, and the nightly set has no device.usb.) device.usb and
# smartcard could be dropped under D22 if M6E does not need USB devices or
# smartcards; whether they have any effect without the App Sandbox is not
# tested. Ad-hoc code has no Team ID either, so every executable that loads
# the bundled libraries also gets com.apple.security.cs.disable-library-
# validation; Mozilla only needs that for the main app and the media and
# security-module helpers.
#
# Sign the app before building the DMG, and sign again after any change to
# the bundle: a modified file no longer matches its signature and is refused.
# The script works in place. If it fails, the bundle is partly re-signed:
# fix the cause and run it again, which is safe on an already signed app.

set -eu

runtime=yes
case ${1:-} in
    --no-runtime) runtime=no; shift ;;
esac
case ${1:-} in
    -*) set -- ;;
esac
[ $# -eq 1 ] || { echo "usage: $0 [--no-runtime] <path to M6E.app>" >&2; exit 2; }
[ -d "$1" ] || { echo "error: $1 is not a directory" >&2; exit 1; }

here=$(cd "$(dirname "$0")" && pwd -P)
app=$(cd "$1" && pwd -P)
case $app in
    *.app) ;;
    *) echo "error: $app is not an .app bundle" >&2; exit 1 ;;
esac

work=$(mktemp -d "${TMPDIR:-/tmp}/m6e-sign.XXXXXX")
trap 'rm -rf "$work"' EXIT

[ -f "$app/Contents/Info.plist" ] || { echo "error: $app has no Contents/Info.plist" >&2; exit 1; }
exe=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$app/Contents/Info.plist")
[ -x "$app/Contents/MacOS/$exe" ] || { echo "error: no executable $exe in $app" >&2; exit 1; }

cd "$app"

if [ -n "$(find . -type l -lname '/*' -print -quit)" ]; then
    echo "error: $app links to files outside itself (a build tree, not a package)." >&2
    echo "Sign the app that ./mach package produces, never obj-m6e/dist/M6E.app." >&2
    exit 1
fi

# plist <name> <key>...
plist() {
    out=$work/$1.plist
    shift
    {
        echo '<?xml version="1.0" encoding="UTF-8"?>'
        echo '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">'
        echo '<plist version="1.0"><dict>'
        for key in "$@"; do echo "<key>com.apple.security.$key</key><true/>"; done
        echo '</dict></plist>'
    } > "$out"
}

plist main cs.allow-jit cs.disable-library-validation device.audio-input \
    device.camera device.usb personal-information.location smartcard
plist plugin-container cs.allow-jit cs.disable-library-validation
plist media-plugin-helper cs.disable-library-validation
plist security-module-helper cs.disable-library-validation smartcard
plist links-bundled-libs cs.disable-library-validation

links_bundled_libs() {
    otool -L "$1" | sed 1d | grep -q '@\(rpath\|executable_path\|loader_path\)/'
}

# entitlements_for <path relative to the app> prints a plist path or nothing
entitlements_for() {
    case $1 in
        .) echo "$work/main.plist" ;;
        */plugin-container.app) echo "$work/plugin-container.plist" ;;
        */media-plugin-helper.app) echo "$work/media-plugin-helper.plist" ;;
        */security-module-helper.app) echo "$work/security-module-helper.plist" ;;
        *.app) echo "$work/links-bundled-libs.plist" ;;
        *.dylib|*.so|*.framework) ;;
        *) case $(file -b "$1") in
               *"shared library"*) ;;
               *) if links_bundled_libs "$1"; then echo "$work/links-bundled-libs.plist"; fi ;;
           esac ;;
    esac
}

# A bundle's main executable is signed together with its bundle.
is_bundle_exe() {
    name=$(basename "$1")
    case $1 in
        */Contents/MacOS/*)
            info=${1%/Contents/MacOS/*}/Contents/Info.plist
            [ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$info" 2>/dev/null)" = "$name" ]
            ;;
        *.framework/*)
            [ "$(basename "${1%.framework/*}")" = "$name" ]
            ;;
        *) return 1 ;;
    esac
}

sign() {
    path=$1
    ents=$(entitlements_for "$path")
    set -- --force --sign - --timestamp=none
    if [ "$runtime" = yes ]; then
        set -- "$@" --options runtime
        [ -z "$ents" ] || set -- "$@" --entitlements "$ents"
    fi
    if ! codesign "$@" "$path" 2> "$work/codesign.err"; then
        echo "error: codesign failed for $path" >&2
        cat "$work/codesign.err" >&2
        echo "The bundle was changed in place and may be partly re-signed. Fix the cause" >&2
        echo "and run this script again; it is safe on an already signed app." >&2
        if [ "$path" = . ]; then
            echo "A file in Contents/ other than Info.plist, PkgInfo, MacOS, Resources," >&2
            echo "Frameworks and Library, or a file in Contents/MacOS that is not a binary" >&2
            echo "(the build tree's Contents/moz.build and MacOS/gtest are examples), counts" >&2
            echo "as unsigned code. Remove it from the package." >&2
        fi
        exit 1
    fi
}

"$here/finalize-app.sh" "$app"
echo

echo "Signing $app (hardened runtime: $runtime)"
xattr -cr .

find ./Contents -type f \( -perm -u+x -o -name '*.dylib' -o -name '*.so' \) |
    while IFS= read -r f; do
        case $(file -b "$f") in
            Mach-O*) is_bundle_exe "$f" || echo "$f" ;;
        esac
    done > "$work/machos"
find ./Contents \( -name '*.app' -o -name '*.framework' \) -type d >> "$work/machos"

# Deepest first, so that nested code is signed before the code that contains it.
awk -F/ '{ print NF "\t" $0 }' "$work/machos" | sort -t "$(printf '\t')" -k1,1nr -k2 |
    cut -f2- > "$work/order"

count=0
while IFS= read -r path; do
    sign "$path"
    count=$((count + 1))
done < "$work/order"
sign .
echo "Signed $((count + 1)) code objects"

echo
echo "Verifying"
status=0
if codesign --verify --deep --strict --verbose=2 . > "$work/verify.out" 2>&1; then
    grep -v '^--' "$work/verify.out" | sed 's/^/  /'
else
    sed 's/^/  /' "$work/verify.out" >&2
    echo "error: the bundle does not verify" >&2
    status=1
fi

echo
echo "Code objects (flags; entitlements)"
{ cat "$work/order"; echo .; } | sort > "$work/all"
while IFS= read -r path; do
    if ! codesign --verify --strict "$path" 2> "$work/codesign.err"; then
        echo "error: $path does not verify" >&2
        cat "$work/codesign.err" >&2
        status=1
    fi
    flags=$(codesign -dv "$path" 2>&1 | sed -n 's/.*flags=0x[0-9a-f]*(\(.*\)).*/\1/p' | head -1)
    keys=$(codesign -d --entitlements - --xml "$path" 2>/dev/null | plutil -convert xml1 -o - - 2>/dev/null |
        sed -n 's/.*<key>com\.apple\.security\.\(.*\)<\/key>.*/\1/p' | tr '\n' ' ')
    printf '  %-90s %s; %s\n' "${path#./}" "$flags" "${keys:-no entitlements}"
done < "$work/all"

if [ "$runtime" = yes ]; then
    for need in ./Contents/MacOS/plugin-container.app .; do
        if ! codesign -d --entitlements - --xml "$need" 2>/dev/null | grep -q 'cs.allow-jit'; then
            echo "error: $need lacks allow-jit" >&2
            status=1
        fi
    done
fi

[ $status -eq 0 ] && echo && echo "Signed ad hoc and verified: $app"
exit $status
