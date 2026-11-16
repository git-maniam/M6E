#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Builds sandbox_query, runs sandbox_tests.py, then the conformance suite
# against the helper under --sandbox (m6e-torrent-sandboxed); with --privacy
# also the privacy suite under --sandbox (about 4 more minutes).
# Usage: run.sh [helper binary] [--privacy]
# Synthetic fixtures on 127.0.0.1 and ::1 only (spec 8.9, D38).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
helper=$here/../../build/m6e-torrent
privacy=no
for arg in "$@"; do
    case $arg in
        --privacy) privacy=yes ;;
        *) helper=$arg ;;
    esac
done
helper=$(cd "$(dirname "$helper")" && pwd)/$(basename "$helper")
[ -x "$helper" ] || { echo "Bail out! no helper at $helper"; exit 2; }

out=$(mktemp -d "${TMPDIR:-/tmp}/m6e-sandbox-tests.XXXXXX")
trap 'rm -rf "$out"' EXIT
xcrun --sdk macosx clang -arch arm64 -O1 -Wall -Wextra -Werror \
    "$here/sandbox_query.c" -o "$out/sandbox_query"

status=0
echo "== sandbox_tests.py"
python3 -I "$here/sandbox_tests.py" --helper "$helper" --query "$out/sandbox_query" || status=1
echo "== run_tests.py, helper under --sandbox"
M6E_TORRENT_HELPER=$helper python3 -I "$here/../run_tests.py" \
    --helper "$here/m6e-torrent-sandboxed" || status=1
if [ "$privacy" = yes ]; then
    echo "== privacy_tests.py, helper under --sandbox"
    M6E_TORRENT_HELPER=$helper M6E_SANDBOX_LAYOUT=privacy python3 -I \
        "$here/../privacy/privacy_tests.py" --helper "$here/m6e-torrent-sandboxed" || status=1
fi
exit $status
