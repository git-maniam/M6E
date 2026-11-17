#!/bin/sh
# Runs the privacy and settings tests (spec 8.4, 8.5) against a helper.
# Usage: run.sh [helper binary] [privacy_tests.py options]
# Synthetic fixtures on 127.0.0.1 and ::1 only; no real torrent, tracker or
# DHT node is contacted (spec 8.9, D38).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
helper=${1:-$here/../../build/m6e-torrent}
[ $# -gt 0 ] && shift
exec python3 -I "$here/privacy_tests.py" --helper "$helper" "$@"
