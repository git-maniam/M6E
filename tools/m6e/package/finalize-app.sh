#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Remove from the Info.plist files of a packaged M6E.app what must not ship:
#
#   MozillaDeveloperObjPath, MozillaDeveloperRepoPath
#       absolute paths into the developer's checkout. ./mach package already
#       drops them (toolkit/mozapps/installer/packager.py); this is a safety net.
#   SMPrivilegedExecutables
#       Mozilla's updater and DMG-install helper identities with Mozilla's Team
#       ID. M6E builds neither helper, and ad-hoc code can never satisfy them.
#
#   tools/m6e/package/finalize-app.sh <path to M6E.app>
#
# Idempotent: a plist without these keys is not rewritten. sign-adhoc.sh runs
# this before it signs; changing a plist after signing breaks the signature.

set -eu

[ $# -eq 1 ] || { echo "usage: $0 <path to M6E.app>" >&2; exit 2; }
[ -d "$1" ] || { echo "error: $1 is not a directory" >&2; exit 1; }

app=$(cd "$1" && pwd -P)
case $app in
    *.app) ;;
    *) echo "error: $app is not an .app bundle" >&2; exit 1 ;;
esac
[ -f "$app/Contents/Info.plist" ] || { echo "error: $app has no Contents/Info.plist" >&2; exit 1; }

if [ -n "$(find "$app" -type l -lname '/*' -print -quit)" ]; then
    echo "error: $app links to files outside itself (a build tree, not a package)." >&2
    echo "Finalize the app that ./mach package produces, never obj-m6e/dist/M6E.app." >&2
    exit 1
fi

keys="MozillaDeveloperObjPath MozillaDeveloperRepoPath SMPrivilegedExecutables"

plists=$(mktemp "${TMPDIR:-/tmp}/m6e-finalize.XXXXXX")
trap 'rm -f "$plists"' EXIT
find "$app" -name Info.plist > "$plists"

removed=0
while IFS= read -r plist; do
    if ! /usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$plist" > /dev/null 2>&1; then
        echo "error: cannot read CFBundleIdentifier from ${plist#"$app"/}" >&2
        exit 1
    fi
    for key in $keys; do
        if /usr/libexec/PlistBuddy -c "Print :$key" "$plist" > /dev/null 2>&1; then
            /usr/libexec/PlistBuddy -c "Delete :$key" "$plist"
            echo "Removed $key from ${plist#"$app"/}"
            removed=$((removed + 1))
        fi
    done
    plutil -lint -s "$plist" || { echo "error: $plist is not a valid property list" >&2; exit 1; }
    for key in $keys; do
        if /usr/libexec/PlistBuddy -c "Print :$key" "$plist" > /dev/null 2>&1; then
            echo "error: $key is still in $plist" >&2
            exit 1
        fi
    done
done < "$plists"

echo "Finalized $app ($removed keys removed)"
