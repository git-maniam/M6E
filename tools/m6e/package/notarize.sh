#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Submit a Developer ID signed DMG to Apple for notarization, wait for the
# verdict, staple the ticket and check the result.
#
#   tools/m6e/package/notarize.sh <path to M6E-<version>-macos-arm64.dmg>
#
# One-time setup (the password is typed into the prompt, never into a file):
#   xcrun notarytool store-credentials m6e-notary \
#       --apple-id <your Apple ID> --team-id <TEAM ID>
# The stored profile name can be changed with M6E_NOTARY_PROFILE.
#
# The DMG must already be signed with the Developer ID certificate (release.sh
# does this when M6E_SIGN_IDENTITY is set). Apple scans the app inside it.
# If the submission is rejected, the log with the reasons is printed.

set -eu

profile=${M6E_NOTARY_PROFILE:-m6e-notary}
[ $# -eq 1 ] || { echo "usage: $0 <dmg>" >&2; exit 2; }
dmg=$1
[ -f "$dmg" ] || { echo "error: $dmg not found" >&2; exit 1; }

if ! codesign -dvv "$dmg" 2>&1 | grep -q 'Authority=Developer ID Application'; then
    echo "error: $dmg is not signed with a Developer ID Application certificate." >&2
    exit 1
fi
if ! xcrun notarytool history --keychain-profile "$profile" > /dev/null 2>&1; then
    echo "error: no stored notary login named $profile. Run:" >&2
    echo "  xcrun notarytool store-credentials $profile --apple-id <Apple ID> --team-id <TEAM ID>" >&2
    exit 1
fi

result=$(mktemp "${TMPDIR:-/tmp}/m6e-notary.XXXXXX")
trap 'rm -f "$result"' EXIT

echo "Submitting $dmg (this takes a few minutes)"
xcrun notarytool submit "$dmg" --keychain-profile "$profile" --wait --output-format json > "$result" || true
cat "$result"
echo

status=$(sed -n 's/.*"status" *: *"\([^"]*\)".*/\1/p' "$result" | head -1)
id=$(sed -n 's/.*"id" *: *"\([^"]*\)".*/\1/p' "$result" | head -1)
if [ "$status" != Accepted ]; then
    echo "error: notarization status is '${status:-unknown}'." >&2
    [ -z "$id" ] || xcrun notarytool log "$id" --keychain-profile "$profile" >&2 || true
    exit 1
fi

xcrun stapler staple "$dmg"
xcrun stapler validate "$dmg"
spctl --assess --type open --context context:primary-signature --verbose=2 "$dmg"
echo "Notarized and stapled: $dmg"
