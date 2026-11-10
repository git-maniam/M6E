#!/bin/bash
# Builds confine_tests and sandbox_probe (arm64, Homebrew clang) into $1.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
out="$1"
lt="${LT_PREFIX:-/Users/rsubramaniam/Projects/M6E/artifacts/p8/engine-libtorrent/install-lt-noossl}"
cxx="${CXX:-/opt/homebrew/opt/llvm/bin/clang++}"

flags="$(PKG_CONFIG_PATH="$lt/lib/pkgconfig" pkg-config --cflags --libs libtorrent-rasterbar)"
src="$here/../../src"

mkdir -p "$out"
# shellcheck disable=SC2086
"$cxx" -arch arm64 -std=c++20 -O1 -Wall -Wextra -Werror $flags \
  -I"$src" "$src/safety.cpp" "$src/confine.cpp" \
  "$here/confine_tests.cpp" -o "$out/confine_tests"
# shellcheck disable=SC2086
"$cxx" -arch arm64 -std=c++20 -O1 -Wall -Wextra -Werror $flags \
  -framework IOKit -framework SystemConfiguration \
  -I"$src" "$src/safety.cpp" "$src/confine.cpp" \
  "$here/sandbox_probe.cpp" -o "$out/sandbox_probe"
