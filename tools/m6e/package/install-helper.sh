#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Put the m6e-torrent helper (spec 8.4, D39) into a packaged M6E.app, at
# Contents/MacOS/m6e-torrent: the folder that holds the m6e executable, where
# M6ETorrentService.sys.mjs looks for it (the parent of XREExeF).
#
#   tools/m6e/package/install-helper.sh <path to M6E.app>
#
# The helper is not built by ./mach build. Source of the binary, first match:
#   1. M6E_TORRENT_HELPER, a prebuilt binary, as an absolute path (sign-adhoc.sh
#      changes directory before it gets here); copied over any helper in the app
#   2. a helper already in the app (kept, so re-signing an app is safe)
#   3. tools/m6e/torrent/build-helper.sh, which builds libtorrent once into its
#      cache and the helper against it (see tools/m6e/torrent/README.md)
# The copy is checked (arm64 only, macOS 11.0, system libraries only) but not
# signed: sign-adhoc.sh signs it, with the rest of the bundle, afterwards.

set -eu

[ $# -eq 1 ] || { echo "usage: $0 <path to M6E.app>" >&2; exit 2; }
[ -d "$1/Contents/MacOS" ] || { echo "error: $1 is not an app bundle" >&2; exit 1; }

here=$(cd "$(dirname "$0")" && pwd -P)
app=$(cd "$1" && pwd -P)
dest=$app/Contents/MacOS/m6e-torrent

if [ -n "${M6E_TORRENT_HELPER:-}" ]; then
    [ -f "$M6E_TORRENT_HELPER" ] || { echo "error: M6E_TORRENT_HELPER=$M6E_TORRENT_HELPER is not a file" >&2; exit 1; }
    src=$M6E_TORRENT_HELPER
elif [ -f "$dest" ]; then
    src=
else
    "$here/../torrent/build-helper.sh"
    src=$here/../torrent/build/m6e-torrent
fi

if [ -n "$src" ]; then
    rm -f "$dest"
    cp "$src" "$dest"
    chmod 755 "$dest"
    xattr -c "$dest" 2> /dev/null || true
    echo "Installed $src into $app/Contents/MacOS"
fi

archs=$(lipo -archs "$dest")
minos=$(otool -l "$dest" | awk '/LC_BUILD_VERSION/ { f = 1 } f && $1 == "minos" { print $2; exit }')
[ "$archs" = arm64 ] || { echo "error: $dest is built for $archs, not arm64" >&2; exit 1; }
[ "$minos" = 11.0 ] || { echo "error: $dest needs macOS $minos, not 11.0" >&2; exit 1; }
foreign=$(otool -L "$dest" | tail -n +2 | awk '{ print $1 }' | grep -v -e '^/usr/lib/' -e '^/System/Library/' || true)
[ -z "$foreign" ] || { echo "error: $dest links outside the system: $foreign" >&2; exit 1; }
echo "Helper in place: ${dest#"$app"/} ($archs, macOS $minos+)"
