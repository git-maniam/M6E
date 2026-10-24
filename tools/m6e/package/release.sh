#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Make the release DMG from a finished build: package, sign, build the DMG
# from the signed app, then verify the DMG and run the post-signing checks.
#
#   tools/m6e/package/release.sh [output directory]      (default artifacts/release)
#
# Run it after ./mach build (or make -C obj-m6e/faster for front-end changes).
# With MOZ_PGO in the mozconfig, any C++ change needs ./mach clobber first.
#
# Nothing is mounted at /Volumes/M6E before it starts, and the machine should
# not be running another M6E: the checks launch the signed app. Work happens
# in a temporary directory, and the signed app is never placed in /tmp for
# manual testing: a copy under /tmp showed intermittent page-load hangs in
# tests that the same copy does not show elsewhere (see the plan, P5.2).

set -eu

top=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$top/artifacts/release}
work=$(mktemp -d "${TMPDIR:-/tmp}/m6e-release.XXXXXX")
trap 'rm -rf "$work"' EXIT

cd "$top"

if [ -d /Volumes/M6E ]; then
    echo "A volume named M6E is mounted; eject it first." >&2
    exit 1
fi

./mach package

staged=obj-m6e/dist/m6e/M6E.app
ditto "$staged" "$work/M6E.app"

tools/m6e/package/sign-adhoc.sh "$work/M6E.app"

version=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$work/M6E.app/Contents/Info.plist")
mkdir -p "$out"
dmg="$out/M6E-$version-macos-arm64.dmg"
rm -f "$dmg"

python3 tools/m6e/branding/make-dmg-layout.py build "$work/M6E.app" "$dmg"
python3 tools/m6e/branding/make-dmg-layout.py verify --require-signed "$dmg"

./mach python tools/m6e/chrome_eval.py \
    --binary "$work/M6E.app/Contents/MacOS/m6e" \
    --script tools/m6e/package/check-signed.js --headless

echo "Release DMG: $dmg"
echo "Next: ./mach python tools/m6e/acceptance.py --app <a copy of M6E.app outside /tmp> --out artifacts/P5.2-acceptance"
