#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Builds sandbox_query and net_probe, then runs security_tests.py.
# Usage: run.sh [helper binary] [security_tests.py options]
# Synthetic fixtures on 127.0.0.1 only (spec 8.9, D38).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
helper=$here/../../build/m6e-torrent
case ${1:-} in
    ""|-*) ;;
    *) helper=$1; shift ;;
esac
[ -x "$helper" ] || { echo "Bail out! no helper at $helper"; exit 2; }

out=$(mktemp -d "${TMPDIR:-/tmp}/m6e-security-tests.XXXXXX")
trap 'rm -rf "$out"' EXIT
xcrun --sdk macosx clang -arch arm64 -O1 -Wall -Wextra -Werror \
    "$here/../sandbox/sandbox_query.c" -o "$out/sandbox_query"
xcrun --sdk macosx clang -arch arm64 -O1 -Wall -Wextra -Werror \
    "$here/net_probe.c" -o "$out/net_probe"
python3 -I "$here/security_tests.py" --helper "$helper" --query "$out/sandbox_query" \
    --probe "$out/net_probe" "$@"
