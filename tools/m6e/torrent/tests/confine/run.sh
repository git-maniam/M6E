#!/bin/bash
# Builds and runs the confinement tests: unit tests on hostile paths, hostile
# synthetic .torrent files, and the sandbox proof. arm64, Homebrew clang.
# Usage: run.sh [output directory]   (default: a new temporary folder)
# Synthetic data and 127.0.0.1 only; no real torrent is read or contacted.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
if [ $# -ge 1 ]; then
  out="$1"
  mkdir -p "$out"
else
  out="$(mktemp -d "${TMPDIR:-/tmp}/m6e-confine-build.XXXXXX")"
fi
out="$(cd "$out" && pwd -P)"

"$here/build.sh" "$out"
python3 -I "$here/gen_hostile.py" "$out/torrents"
"$out/confine_tests" "$out/torrents"
"$here/sandbox_proof.sh" "$out"
