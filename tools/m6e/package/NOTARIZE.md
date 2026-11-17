# Signing and notarizing M6E

M6E is signed with the Developer ID Application certificate
`Developer ID Application: Ravi Subramaniam (XU8XWLY8Q9)` and notarized by
Apple, so that macOS opens it without the "unidentified developer" warning.

## One-time setup

1. **Certificate.** Already done: `security find-identity -v -p codesigning`
   shows the `Developer ID Application` line. Back up the certificate and its
   private key: Keychain Access, My Certificates, right-click the certificate,
   Export, `.p12` with a password. Without the private key the certificate is
   useless on another Mac.
2. **App-specific password.** Sign in at https://account.apple.com, open
   Sign-In and Security, App-Specific Passwords, create one named `m6e-notary`
   and copy it. Use the Apple ID of the developer account.
3. **Store the login in the keychain** (the password is typed at the prompt and
   never written to a file):

       xcrun notarytool store-credentials m6e-notary \
           --apple-id <your Apple ID> --team-id XU8XWLY8Q9

   It checks the login with Apple. If it fails with a TLS or network error, the
   office network is probably inspecting traffic to Apple: try another network.

## Every release

One command builds, signs with Developer ID, builds the DMG, signs the DMG,
notarizes and staples:

    export M6E_LIBTORRENT_PREFIX=$PWD/artifacts/p8/engine-libtorrent/install-lt-noossl
    M6E_DEVELOPER_ID=1 M6E_NOTARY_PROFILE=m6e-notary \
        tools/m6e/package/release.sh artifacts/release-<date>

Or in steps, for a DMG that `release.sh` already built with `M6E_DEVELOPER_ID=1`:

    tools/m6e/package/notarize.sh artifacts/release-<date>/M6E-<version>-macos-arm64.dmg

`notarize.sh` submits the DMG, waits for Apple's verdict (usually 2 to 15
minutes), staples the ticket and checks it. On a rejection it prints Apple's log
with the reasons.

## Check the result

    xcrun stapler validate <dmg>
    spctl --assess --type open --context context:primary-signature --verbose=2 <dmg>

The second should say `accepted` and `source=Notarized Developer ID`. Before
notarization it says `rejected` and `source=Unnotarized Developer ID`, which is
expected.

Final test, on a Mac that did not build M6E: download the DMG through a browser
(this sets the quarantine flag), open it, drag M6E to Applications and start it.
There should be no warning, only the usual "downloaded from the internet" dialog.

## If Apple rejects it

`xcrun notarytool log <submission id> --keychain-profile m6e-notary` lists each
problem. The usual ones: a nested binary that is not signed or lacks the
hardened runtime or a secure timestamp (the scripts sign everything with
`--options runtime --timestamp`), the `get-task-allow` entitlement, or an
unsigned file left in the bundle. Fix, rebuild and submit again.

## What the scripts do

- `sign-developer-id.sh`: finds the one Developer ID Application identity (or
  uses `M6E_SIGN_IDENTITY`) and runs `sign-adhoc.sh` with it: nested code first,
  the bundle last, hardened runtime, secure timestamp, the entitlements listed
  in `sign-adhoc.sh`; the torrent helper has the hardened runtime and none.
- `release.sh`: with `M6E_DEVELOPER_ID=1` it also signs the DMG, and with
  `M6E_NOTARY_PROFILE` set it runs `notarize.sh`.
- Without those variables everything stays ad hoc, as before.
