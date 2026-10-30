#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Print the Homebrew cask (Casks/m6e.rb of the git-maniam/homebrew-m6e tap) for
# a release DMG. The version comes from the file name and the sha256 from the
# file, so each release regenerates the cask.
#
#   tools/m6e/package/homebrew-cask.sh <M6E-<version>-macos-arm64.dmg> [output file]
#
# Without an output file the cask goes to standard output. Run it after
# release.sh and before publishing the DMG as the GitHub release v<version>.

set -eu

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    echo "usage: $0 <M6E-<version>-macos-arm64.dmg> [output file]" >&2
    exit 2
fi

dmg=$1
out=${2:-}

if [ ! -f "$dmg" ]; then
    echo "No such file: $dmg" >&2
    exit 1
fi

name=$(basename "$dmg")
case "$name" in
    M6E-*-macos-arm64.dmg) ;;
    *)
        echo "Unexpected file name: $name (want M6E-<version>-macos-arm64.dmg)" >&2
        exit 1
        ;;
esac

version=${name#M6E-}
version=${version%-macos-arm64.dmg}
case "$version" in
    '' | *[!0-9A-Za-z.]*)
        echo "Cannot read a version from: $name" >&2
        exit 1
        ;;
esac

sha256=$(shasum -a 256 "$dmg" | cut -d ' ' -f 1)

cask() {
    sed -e "s/@VERSION@/$version/" -e "s/@SHA256@/$sha256/" <<'EOF'
cask "m6e" do
  version "@VERSION@"
  sha256 "@SHA256@"

  url "https://github.com/git-maniam/M6E/releases/download/v#{version}/M6E-#{version}-macos-arm64.dmg"
  name "M6E"
  desc "Debloated, privacy-first web browser for Apple silicon"
  homepage "https://github.com/git-maniam/M6E"

  livecheck do
    url :url
    regex(/^v?(\d+(?:\.\d+)+(?:[ab]\d+)?)$/i)
    strategy :github_releases do |json, regex|
      json.filter_map do |release|
        next if release["draft"]

        release["tag_name"]&.[](regex, 1)
      end
    end
  end

  depends_on arch: :arm64
  depends_on macos: :big_sur

  app "M6E.app"

  zap trash: [
    "~/Library/Application Support/M6E",
    "~/Library/Caches/M6E",
    "~/Library/Preferences/org.m6e.browser.plist",
    "~/Library/Saved Application State/org.m6e.browser.savedState",
  ]

  caveats <<~EOS
    M6E is signed ad hoc and is not notarized by Apple, so macOS Gatekeeper
    blocks the first launch of a downloaded copy. Do one of the following:

      - Open the app from Finder with right-click > Open, then confirm; or
      - open System Settings > Privacy & Security and click "Open Anyway"
        after the first blocked launch; or
      - remove the quarantine flag yourself:
          xattr -dr com.apple.quarantine /Applications/M6E.app
        or install with:
          brew install --cask --no-quarantine git-maniam/m6e/m6e

    M6E does not update itself. Update it with:
      brew upgrade --cask m6e
  EOS
end
EOF
}

if [ -n "$out" ]; then
    cask > "$out"
    echo "Wrote $out ($name, sha256 $sha256)" >&2
else
    cask
fi
