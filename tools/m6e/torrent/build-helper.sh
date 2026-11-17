#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Build the m6e-torrent helper (spec 8.4, D39): libtorrent-rasterbar from the
# pinned release tarball into a cache, then the helper against it, with the
# Homebrew clang, for arm64 and macOS 11.0. Never uses the network: the
# libtorrent build runs under a sandbox that denies it.
#
#   tools/m6e/torrent/build-helper.sh [--clean] [--install-into <M6E.app>]
#
# Output: tools/m6e/torrent/build/m6e-torrent. --install-into copies it to
# <M6E.app>/Contents/MacOS/ (sign the app again afterwards). --clean removes
# the helper's object files first (not the libtorrent cache).
#
# Environment:
#   M6E_TORRENT_CACHE      cache folder (default: <main checkout>/artifacts/p8/cache)
#   M6E_LIBTORRENT_PREFIX  an existing libtorrent install to use instead
#   M6E_LIBTORRENT_TARBALL the release tarball (default: looked up in the cache,
#                          then in artifacts/p8/engine-libtorrent/dl-2.1.2)
#   M6E_TORRENT_JOBS       parallel jobs (default: the number of CPUs)

set -eu

LT_VERSION=2.1.2
LT_SHA256=3362546d9cd71b9e49ee6cac7d3f1f914ce9cdb217c86b63d5b22cbed0334dbc
LT_TARBALL_NAME=libtorrent-rasterbar-$LT_VERSION.tar.gz
MINOS=11.0
ARCH=arm64

clean=no
app=
while [ $# -gt 0 ]; do
    case $1 in
        --clean) clean=yes; shift ;;
        --install-into) app=${2:?--install-into needs a path}; shift 2 ;;
        *) echo "usage: $0 [--clean] [--install-into <M6E.app>]" >&2; exit 2 ;;
    esac
done

here=$(cd "$(dirname "$0")" && pwd)
common=$(git -C "$here" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)
if [ -n "$common" ]; then
    main=$(dirname "$common")
else
    main=$(cd "$here/../../.." && pwd)
fi
cache=${M6E_TORRENT_CACHE:-$main/artifacts/p8/cache}
jobs=${M6E_TORRENT_JOBS:-$(sysctl -n hw.ncpu)}

llvm=/opt/homebrew/opt/llvm
boost=/opt/homebrew/opt/boost
cc=$llvm/bin/clang
cxx=$llvm/bin/clang++
for tool in "$cc" "$cxx" /opt/homebrew/bin/cmake /opt/homebrew/bin/pkgconf; do
    [ -x "$tool" ] || { echo "error: $tool is missing (Homebrew toolchain, D17)" >&2; exit 1; }
done
[ -f "$boost/include/boost/version.hpp" ] || { echo "error: Boost headers are missing at $boost" >&2; exit 1; }
sdk=$(xcrun --sdk macosx --show-sdk-path)
sdk_version=$(xcrun --sdk macosx --show-sdk-version)
boost_version=$(sed -n 's/^#define BOOST_LIB_VERSION "\(.*\)"/\1/p' "$boost/include/boost/version.hpp")
compiler_version=$("$cxx" --version | head -n 1)

target="-arch $ARCH -mmacosx-version-min=$MINOS -isysroot $sdk -mcpu=apple-m1"
libcxx="-nostdinc++ -isystem $sdk/usr/include/c++/v1"
harden="-ftrivial-auto-var-init=zero -fstack-protector-strong"

nonet() {
    if [ "${M6E_TORRENT_SANDBOX:-1}" = 1 ] && command -v sandbox-exec >/dev/null 2>&1; then
        sandbox-exec -p '(version 1)(allow default)(deny network*)' "$@"
    else
        "$@"
    fi
}

find_tarball() {
    for candidate in "${M6E_LIBTORRENT_TARBALL:-}" "$cache/$LT_TARBALL_NAME" \
        "$main/artifacts/p8/engine-libtorrent/dl-$LT_VERSION/$LT_TARBALL_NAME"; do
        if [ -n "$candidate" ] && [ -f "$candidate" ]; then
            echo "$candidate"
            return
        fi
    done
    echo "error: $LT_TARBALL_NAME not found. Put the official release tarball" >&2
    echo "(sha256 $LT_SHA256) at $cache/$LT_TARBALL_NAME; this script never downloads." >&2
    exit 1
}

build_libtorrent() {
    work=$1
    prefix=$work/install
    tarball=$(find_tarball)
    actual=$(shasum -a 256 "$tarball" | cut -d ' ' -f 1)
    if [ "$actual" != "$LT_SHA256" ]; then
        echo "error: $tarball has sha256 $actual, expected $LT_SHA256" >&2
        exit 1
    fi
    echo "Building libtorrent $LT_VERSION into $work (about 3 minutes)"
    rm -rf "$work"
    mkdir -p "$work/src"
    tar -xzf "$tarball" -C "$work/src"
    src=$work/src/libtorrent-rasterbar-$LT_VERSION
    [ -f "$src/CMakeLists.txt" ] || { echo "error: unexpected tarball layout" >&2; exit 1; }
    nonet /opt/homebrew/bin/cmake -S "$src" -B "$work/build" -G "Unix Makefiles" -Wno-dev \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" \
        -DCMAKE_C_FLAGS="-mcpu=apple-m1 $harden" \
        -DCMAKE_CXX_FLAGS="$libcxx -mcpu=apple-m1 $harden" \
        -DCMAKE_OSX_SYSROOT="$sdk" -DCMAKE_OSX_ARCHITECTURES=$ARCH \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=$MINOS -DCMAKE_CXX_STANDARD=17 \
        -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_PREFIX_PATH="$boost" \
        -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=TRUE \
        -DCMAKE_DISABLE_FIND_PACKAGE_GnuTLS=TRUE \
        -DCMAKE_DISABLE_FIND_PACKAGE_LibGcrypt=TRUE \
        -DBUILD_SHARED_LIBS=OFF -Dwebtorrent=OFF -Di2p=OFF -Ddeprecated-functions=OFF \
        -Dbuild_tests=OFF -Dbuild_examples=OFF -Dbuild_tools=OFF -Dpython-bindings=OFF \
        > "$work/configure.log" 2>&1 || { tail -n 30 "$work/configure.log" >&2; exit 1; }
    nonet /opt/homebrew/bin/cmake --build "$work/build" -j "$jobs" > "$work/build.log" 2>&1 \
        || { tail -n 30 "$work/build.log" >&2; exit 1; }
    nonet /opt/homebrew/bin/cmake --install "$work/build" > "$work/install.log" 2>&1 \
        || { tail -n 30 "$work/install.log" >&2; exit 1; }
    rm -rf "$work/src" "$work/build"
    touch "$work/complete"
}

if [ -n "${M6E_LIBTORRENT_PREFIX:-}" ]; then
    prefix=$M6E_LIBTORRENT_PREFIX
else
    key=$(printf '%s\n' "$LT_SHA256" "$compiler_version" "$sdk_version" "$boost_version" \
        "$target" "$libcxx" "$harden" webtorrent=OFF i2p=OFF deprecated=OFF tls=none \
        | shasum -a 256 | cut -c 1-12)
    work=$cache/libtorrent-$LT_VERSION-$key
    [ -f "$work/complete" ] || build_libtorrent "$work"
    prefix=$work/install
fi
pc=$prefix/lib/pkgconfig
[ -f "$pc/libtorrent-rasterbar.pc" ] && [ -f "$prefix/lib/libtorrent-rasterbar.a" ] \
    || { echo "error: no static libtorrent install at $prefix" >&2; exit 1; }
pkg() {
    PKG_CONFIG_PATH=$pc PKG_CONFIG_LIBDIR=$pc /opt/homebrew/bin/pkgconf \
        --define-variable=prefix="$prefix" "$@" libtorrent-rasterbar
}
[ "$(pkg --modversion)" = "$LT_VERSION" ] || { echo "error: $prefix is not libtorrent $LT_VERSION" >&2; exit 1; }
lt_cflags=$(pkg --cflags)
lt_libs=$(pkg --libs)
for flag in $lt_cflags; do
    case $flag in
        -I*) [ -d "${flag#-I}" ] || { echo "error: $flag from libtorrent's pkgconf file does not exist" >&2; exit 1; } ;;
    esac
done

out=$here/build
obj=$out/obj
[ "$clean" = yes ] && rm -rf "$obj"
mkdir -p "$obj"
cxxflags="$target $libcxx -std=c++17 -O2 -fexceptions -Wall -Wextra -Wno-unused-parameter $harden $lt_cflags"
stamp=$obj/flags
if [ ! -f "$stamp" ] || [ "$(cat "$stamp")" != "$cxx $cxxflags" ]; then
    rm -f "$obj"/*.o
    printf '%s' "$cxx $cxxflags" > "$stamp"
fi

objects=
for source in "$here"/src/*.cpp; do
    name=$(basename "$source" .cpp)
    object=$obj/$name.o
    rebuild=no
    if [ ! -f "$object" ] || [ "$source" -nt "$object" ]; then
        rebuild=yes
    else
        for header in "$here"/src/*.hpp; do
            [ "$header" -nt "$object" ] && rebuild=yes
        done
    fi
    if [ "$rebuild" = yes ]; then
        echo "  CXX $name.cpp"
        # shellcheck disable=SC2086
        "$cxx" $cxxflags -c "$source" -o "$object"
    fi
    objects="$objects $object"
done

echo "  LINK m6e-torrent"
# shellcheck disable=SC2086
"$cxx" $target $objects $lt_libs -framework IOKit -framework CoreFoundation \
    -Wl,-dead_strip -o "$out/m6e-torrent"
strip -x "$out/m6e-torrent"

archs=$(lipo -archs "$out/m6e-torrent")
minos=$(otool -l "$out/m6e-torrent" | awk '/LC_BUILD_VERSION/ { f = 1 } f && $1 == "minos" { print $2; exit }')
[ "$archs" = "$ARCH" ] || { echo "error: built for $archs, not $ARCH" >&2; exit 1; }
[ "$minos" = "$MINOS" ] || { echo "error: minimum macOS is $minos, not $MINOS" >&2; exit 1; }
foreign=$(otool -L "$out/m6e-torrent" | tail -n +2 | awk '{ print $1 }' \
    | grep -v -e '^/usr/lib/' -e '^/System/Library/' || true)
[ -z "$foreign" ] || { echo "error: links outside the system: $foreign" >&2; exit 1; }
echo "Built $out/m6e-torrent ($archs, macOS $minos+, $(stat -f %z "$out/m6e-torrent") bytes, libtorrent from $prefix)"

if [ -n "$app" ]; then
    [ -d "$app/Contents/MacOS" ] || { echo "error: $app is not an app bundle" >&2; exit 1; }
    cp "$out/m6e-torrent" "$app/Contents/MacOS/m6e-torrent"
    echo "Copied into $app/Contents/MacOS; sign the app again (tools/m6e/package/sign-adhoc.sh)."
fi
