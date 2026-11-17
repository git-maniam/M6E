#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Sign a packaged M6E.app with the "Developer ID Application" certificate in
# the login keychain, with the hardened runtime and a secure timestamp, which
# notarization requires. It runs sign-adhoc.sh with M6E_SIGN_IDENTITY set.
#
#   tools/m6e/package/sign-developer-id.sh <path to M6E.app>
#
# M6E_SIGN_IDENTITY may already name the identity (a name or a SHA-1 hash);
# otherwise the one Developer ID Application identity in the keychain is used.
# The first run may show a keychain dialog asking whether codesign may use the
# private key: choose "Always Allow".

set -eu

here=$(cd "$(dirname "$0")" && pwd -P)
print_only=no
if [ "${1:-}" = --print-identity ]; then
    print_only=yes
    set -- unused
fi
[ $# -eq 1 ] || { echo "usage: $0 <path to M6E.app>" >&2; exit 2; }

if [ -z "${M6E_SIGN_IDENTITY:-}" ]; then
    ids=$(security find-identity -v -p codesigning | sed -n 's/^ *[0-9]*) \([0-9A-F]\{40\}\) "Developer ID Application:.*/\1/p')
    count=$(printf '%s\n' "$ids" | grep -c . || true)
    if [ "$count" -ne 1 ]; then
        echo "error: expected one Developer ID Application identity in the keychain, found $count." >&2
        echo "Set M6E_SIGN_IDENTITY to the hash from: security find-identity -v -p codesigning" >&2
        exit 1
    fi
    M6E_SIGN_IDENTITY=$ids
fi
export M6E_SIGN_IDENTITY

if [ "$print_only" = yes ]; then
    echo "$M6E_SIGN_IDENTITY"
    exit 0
fi

exec "$here/sign-adhoc.sh" "$@"
