<p align="center"><img src="M6E-Icon.png" alt="M6E icon: a Bangalore autorickshaw" width="160"></p>

# M6E

M6E (said "'Am Sexy") is a fast, private and deliberately simple web browser for Macs with Apple silicon. It's built from the Firefox source code, with everything that phones home, tracks you or adds clutter removed. Like the Bangalore autorickshaw on its icon, it's meant to be nimble.

> M6E is an experimental release. It's built for speed and privacy, but you may run into bugs or websites that don't work.

- [What's different from Firefox](#whats-different-from-firefox)
- [Install](#install)
- [Using M6E](#using-m6e)
- [Status](#status)
- [Building from source](#building-from-source)
- [Documentation](#documentation)
- [Licence](#licence)

## What's different from Firefox

- **Debloated:** no Pocket, Firefox View, sponsored content, add-on recommendations or Normandy studies, and Phase 6 deleted the Mozilla VPN, Translations, generative AI and machine learning, Sync, Mozilla accounts, the application updater, the crash reporter and Nimbus experiments (Nimbus leaves a small stub). Telemetry and data collection are switched off and locked. What remains of the deleted features' code is checked to be unreachable (`tools/m6e/p6-reports/unreachability.md`). Only macOS on Apple silicon is supported, and the interface is in English only.
- **Quiet on the network:** on a fresh profile left idle for 5 minutes, the release build contacted five hosts and nothing else: `content-signature-2.cdn.mozilla.net`, `firefox-settings-attachments.cdn.mozilla.net` and `firefox.settings.services.mozilla.com` (Remote Settings, for security and tracking-protection data only), `push.services.mozilla.com` (Web Push) and `security.cloudflare-dns.com` (DNS over HTTPS). Mozilla is contacted for those two things only. Widevine and OpenH264 still download from Google and Cisco when needed.
- **Privacy by default:**
  - strict tracking protection, which includes Fingerprinting Protection;
  - HTTPS-Only mode, Global Privacy Control and WebRTC IP-leak protection;
  - Arkenfox-style hardening;
  - DNS over HTTPS through Cloudflare for Families (security), which also refuses to resolve known malware and phishing domains.
- **Privacy Shield:** one switch in Settings, Privacy that sets how every site is treated, plus an optional Maximum protection mode with full fingerprinting resistance.
- **Tracker Blocker:** the shield at the left of the address bar counts what was blocked on the current page. Its panel lists the blocked trackers and has a per-site switch that overrides the Privacy Shield until M6E quits, in either direction.
- **Telemetry button:** a gauge button beside the address bar that shows page timings and size, measured on your Mac and never sent anywhere.
- **Search:** Google by default, with Bing as the only alternative. There are no partner codes, and no way to add other engines.
- **Reading Mode:** strictly black text on white, 25% larger, with sidebars, ads, embeds and tracking pixels stripped. It can be opened on any page from the View menu.
- **Look:** a light, Safari-like interface that follows the macOS accent colour, with the M6E icon in place of every fox. The browser interface stays light even when macOS is in Dark Mode, while web pages still follow it. The home page shows the M6E splash illustration above the search box, and there's a custom "No Internet Connection" page.
- **Also:** a bookmarks toolbar on/off switch in Settings, and push notifications delivered through macOS Notification Center.

## Install

Requirements: a Mac with Apple silicon (M1 or later). There is no Intel or universal build. The app's `Info.plist` sets a minimum of macOS 10.15, which every Apple silicon Mac exceeds (they shipped with macOS 11 or later).
<!-- CONFIRM: the macOS versions M6E has actually been tested on (the plan only records the developer's machine); say which is the lowest tested -->

### Homebrew

<!-- TODO: confirm once the tap is published -->
```sh
brew install --cask git-maniam/m6e/m6e
```

This installs the cask `m6e` from the tap `git-maniam/m6e`.

### DMG

1. Download `M6E-<version>-macos-arm64.dmg` (currently `M6E-161.0a1-macos-arm64.dmg`) from the [GitHub Releases page](https://github.com/git-maniam/M6E/releases).
<!-- CONFIRM: a release with this asset is published at that URL -->
2. Open the DMG and drag M6E onto the Applications link.

### First launch

M6E is signed ad hoc, not with an Apple Developer ID, and it is not notarized (decision D8 in the [specification](m6e_browser_agent_instructions.md)). Apple's Gatekeeper therefore treats a downloaded copy as coming from an unidentified developer the first time you open it. You do this once:

1. Open M6E from Applications. macOS says it cannot verify the app ("M6E Not Opened"). Click **Done**, not Move to Trash.
2. Open **System Settings, Privacy & Security**, scroll to **Security**, and click **Open Anyway** next to the message about M6E. Enter your password or use Touch ID, then click **Open**. The button is available for about an hour after step 1.
3. After that, M6E opens normally.

Alternatives:

- In Terminal, remove the quarantine flag: `xattr -dr com.apple.quarantine /Applications/M6E.app`.
- With Homebrew, skip the flag at install time: `brew install --cask --no-quarantine git-maniam/m6e/m6e`.
<!-- CONFIRM: --no-quarantine is still accepted by the Homebrew version users will have -->
- Control-click (right-click) and Open no longer bypasses the check on macOS 15 and later. On macOS 14 and earlier it is the usual way to open an app from an unidentified developer; the project has not tested this.

If macOS says "M6E is damaged and can't be opened", the download is incomplete or the app changed after signing: download it again.
<!-- CONFIRM: the Gatekeeper dialog and the Open Anyway steps are from the plan (P5.1); the plan says they are still untested with a real download on a second Mac -->

Notarization would remove all of this. It needs an Apple Developer ID (US$99 a year) and has not been decided.

## Using M6E

**First run:** M6E starts on the home page with the splash illustration above the Google search box. There is no welcome page or sign-in. To bring bookmarks in from another browser, use the bookmark manager's import (Safari, Chrome and Chromium are available).

**Settings:** open the M6E menu, then Settings.

- **Privacy Shield** (Settings, Privacy): the first group in the pane.
  - The **Privacy Shield** switch is on by default. On, it blocks trackers, ads and fingerprinting, connects to sites securely and applies M6E's privacy hardening. Off, nothing is blocked on any site.
  - **Maximum protection** adds full fingerprinting resistance while the Shield is on. Sites see your time zone as UTC and windows get grey margins, so more sites may not work.
  - Changing either one offers to reload your tabs.
- **Search:** Google and Bing only. Settings lists the two, and there is no Add button.
- **Bookmarks toolbar** (Settings, Appearance): the "Show bookmarks toolbar" switch. It is off by default.
- **DNS over HTTPS:** on by default through Cloudflare for Families (security). You can pick another resolver or turn it off in Settings. A site the default resolver blocks shows the "can't find this site" page, and DoH falls back to your Mac's ordinary DNS (unfiltered) if the resolver itself can't be reached.

**Tracker Blocker:** the shield at the left of the address bar shows how many trackers and other privacy invasions were blocked on the current page. Click it to see them by category, with their domains, and to use the **Tracker Blocker** switch for the current site.

- With the Privacy Shield on, switching a site off lets an important site that breaks load without blocking (and without HTTPS-Only for it).
- With the Privacy Shield off, switching a site on protects a high-risk site.
- The choice lasts until you quit M6E.
- A site with protection off shows a shield with an X and no number.
- Some hardening applies to the whole browser rather than per site (TLS options, punycode display, WebRTC IP handling, PDF scripting, referrer trimming, Global Privacy Control, and window move and resize), so the per-site switch doesn't change it.

**Telemetry button:** the gauge button beside the address bar (tooltip "Telemetry") opens a panel for the page in the current tab: time to first byte, first contentful paint, time to interactive, page size (with the number of requests and how many came from the cache), script parse time, script execution time and TLS/SSL handshake time. The figures are measured on your Mac and are never sent anywhere. M6E has no data collection.

**Reading Mode:** on any page, choose Enter Reader View from the View menu, or press Command-Option-R. The address bar button also appears on pages detected as articles. The text is black on white, 25% larger than usual, with ads, navigation, embeds, forms and small images removed.

**Push notifications:** sites can send notifications through Mozilla's push service, and they appear in macOS Notification Center. Allow M6E to show notifications when macOS asks, or change it later in System Settings, Notifications.
<!-- CONFIRM: the native banner and the macOS permission prompt were checked by hand (the plan lists this as a manual check for the first use) -->

**Updates:** M6E has no auto-updater, so it never checks for or installs a new version by itself. Security fixes arrive only when the project merges upstream Firefox and publishes a new build. To update, install the new DMG over the old app in Applications, or run:

```sh
brew upgrade --cask m6e
```

<!-- TODO: confirm once the tap is published -->
Add-on updates are also off, and only signed add-ons can be installed. Themes can't be installed.

**Your data:** the profile is at `~/Library/Application Support/M6E` (the folder name is the app's name, `M6E`), and the cache is under `~/Library/Caches/M6E`. M6E never shares a profile with an installed Firefox.
<!-- CONFIRM: the Caches folder name and whether macOS also leaves ~/Library/Preferences/org.m6e.browser.plist and ~/Library/Saved Application State/org.m6e.browser.savedState; these follow from the Firefox code and the bundle ID, but were not checked on a running install -->

**Uninstall:** quit M6E, drag M6E from Applications to the Trash, and delete `~/Library/Application Support/M6E` and `~/Library/Caches/M6E` to remove your profile and cache. With Homebrew:

```sh
brew uninstall --zap --cask m6e
```

<!-- TODO: confirm once the tap is published (the cask's zap list decides what --zap removes) -->

**Limitations:**

- Apple silicon Macs only; the interface is in English only (en-US, with a UK English spell-check dictionary).
- No Mozilla accounts, Sync or sign-in: bookmarks and settings stay on the Mac.
- Search is Google and Bing only, and you can't add search engines.
- Sites that need DRM work through Widevine, which downloads from Google when needed. Platform passkeys (Touch ID, iCloud Keychain) are unavailable.
- Because the app is ad hoc signed, macOS may ask again for camera, microphone and location permission after each update.
- The Privacy Shield and strict tracking protection can break some sites. Turn protection off for the site with the Tracker Blocker, or switch the Privacy Shield off.
- Malware and phishing protection works per domain through the DNS resolver, doesn't check downloads, and stops when DoH is unavailable.

## Status

Phases 0 to 6 are done. The current release is 161.0a1, built with LTO and PGO. The plan has the dates, commits and test results of every step.

| Phase | Scope | Status |
|---|---|---|
| 0 | Toolchain and baseline | Done |
| 1 | Debloating | Done |
| 2 | Rebranding and macOS UI | Done |
| 3 | Features and privacy | Done |
| 4 | Performance, platform removal, security hardening | Done (2026-10-24) |
| 5 | `.dmg` packaging, signing and acceptance | Done (2026-10-24) |
| 6 | Deleting the dormant features (VPN, Translations, generative AI, machine learning, Sync, Mozilla accounts, updater, crash reporter, Nimbus) | Done (2026-10-30) |

Measured on the packaged Phase 6 build (details in [`M6E_IMPLEMENTATION_PLAN.md`](M6E_IMPLEMENTATION_PLAN.md), P6.3):

| | Phase 0 (stock Nightly) | Phase 6 (M6E 161.0a1) |
|---|---|---|
| Acceptance (`tools/m6e/acceptance.py`) | | 15 of 15 checks |
| First paint (median of 10 runs) | 785 ms | 643 ms (Phase 5: 579 ms) |
| Blank-tab memory | 698 MB | 430 MB (8 processes) |
| Speedometer 3 | | 45.46 (Phase 5: 44.7) |
| Idle network (300 s, fresh profile) | 21 hosts, plus 40 looked up only over DNS | 5 hosts |
| Release DMG | | 106.5 MiB |

The 643 ms first paint was measured right after the computer restarted, so it may overstate the difference to Phase 5; the plan lists a re-measure on a quiet machine. The Speedometer 3 score was not measured on the stock Phase 0 build.

## Building from source

M6E builds on macOS with Apple silicon, using the Mozilla build system (`./mach`). The toolchain comes from Homebrew rather than `mach bootstrap`, which fails behind TLS inspection (decision D17). The reference machine is arm64 with 14 cores and 48 GB of RAM.

1. Install Xcode (the plan builds with Xcode 27), then the Homebrew toolchain:
   ```sh
   brew install rust cbindgen nasm llvm lld wasi-libc wasi-runtimes node
   ```
   Node is needed for the New Tab bundles. The mozconfig points at Homebrew's LLVM (`/opt/homebrew/opt/llvm`) and WASI sysroot (`/opt/homebrew/share/wasi-sysroot`).
2. The repository's `mozconfig` sets up an optimised arm64 build in `obj-m6e/` with the M6E branding, cross-language LTO and PGO. Build it:
   ```sh
   ./mach build
   ```
   A clean build takes about 70 minutes, because PGO builds three times. Never use artifact builds: they download Mozilla's prebuilt binaries.
3. Run it with `./mach run`, or open `obj-m6e/dist/M6E.app`. The executable is `Contents/MacOS/m6e`.

With PGO, `./mach build faster` refuses to run, and any C++ change needs `./mach clobber` before the next `./mach build`. For front-end changes (JS, CSS, FTL, prefs) run:

```sh
make -C obj-m6e/faster
```

Configure changes (`mozconfig`, `moz.configure`) rebuild everything, so batch them.

### Checks

- **Lint:** `./mach lint <paths>`. The clang-format, pnpm-audit and cargo-audit linters need `mach bootstrap`; for C++ use `/opt/homebrew/opt/llvm/bin/clang-format`.
- **Tests:** `./mach test --headless <directory>` for one area. `tools/m6e/run_tests.sh <log directory>` runs M6E's own suites and the areas it changes, writing one log per suite. The window-based suites (`browser/components/pagetelemetry`, the Trust panel) refuse `--headless` and need an unlocked display.
- **Acceptance:** run it on a signed, packaged app copied outside `/tmp`, on a quiet machine, with no other builds or tests running:
  ```sh
  ./mach python tools/m6e/acceptance.py --app <path to M6E.app> --out <dir>
  ./mach python tools/m6e/unreachability_check.py --app <path to M6E.app> --out <dir>
  ```
  `acceptance.py --list` names the 15 checks. `unreachability_check.py` observes the dormant features that stay in the tree (7 checks).
- **Measurements:** `./mach python tools/m6e/measure.py --binary <app>/Contents/MacOS/m6e --out <dir>` (startup, memory, idle network) and `./mach python tools/m6e/speedometer.py --binary <exe> --out <file>` (Speedometer 3, offline). Timings need a quiet machine.
- **Scripted checks of a running build:** `tools/m6e/chrome_eval.py`; see the plan for the Marionette pitfalls.

### Packaging

```sh
tools/m6e/package/release.sh [output directory]
```

Run it after a build. It packages the app, signs it ad hoc (`sign-adhoc.sh`), builds `M6E-<version>-macos-arm64.dmg` from the signed app (default `artifacts/release`), verifies the DMG and runs the post-signing checks. Copy an app out of the DMG with `ditto --noextattr --norsrc`, never `cp -R`: `cp -R` carries `com.apple.FinderInfo` attributes, which make `codesign --verify --strict` fail.

### Where M6E's changes live

- behaviour prefs: `browser/app/profile/m6e.js`;
- visual overrides: `toolkit/themes/shared/m6e-tokens.css`;
- branding: `browser/branding/m6e`, regenerated from `M6E-Icon.png` by `tools/m6e/branding/make-icons.sh`;
- the Privacy Shield and Tracker Blocker: `browser/components/protections/M6EPrivacyShield.sys.mjs`;
- the Telemetry button: `browser/components/pagetelemetry`;
- other source edits are marked `M6E:`.

Helper tools are in `tools/m6e/`:

- `measure.py`, `speedometer.py`, `gecko_profile.py`: measurements and profiling;
- `chrome_eval.py`: scripted checks;
- `acceptance.py`, `unreachability_check.py`, `run_tests.sh`: verification;
- `branding/`: icon generation, the user-visible string scan (`scan-strings.py`), the fox-artwork replacement and the DMG layout;
- `search/`: the search configuration filter;
- `push/` and `trackers/`: end-to-end checks;
- `package/`: signing and the release DMG;
- `merge/`: upstream merge tooling;
- `platform/`: the scripts that stripped Windows, Linux and Android code.

### Working rules

- Keep source edits small and mark them `M6E:`, so upstream Firefox fixes can still be merged.
- Put prefs in `m6e.js` and visual overrides in `m6e-tokens.css` instead of editing upstream files.
- Never blindly replace "Firefox" or "Mozilla". They must stay in licences, MPL headers, the attribution text, internal identifiers and upstream URLs. `tools/m6e/branding/scan-strings.py` lists user-visible strings that still name them; the expected result is 0.
- Commit one change per plan step as `M6E P<phase>.<step>: <summary>`, after it builds and checks out.

### Merging upstream Firefox

M6E has no auto-updater, so security fixes come from manual upstream merges (decision D9). The last merged upstream commit is in `tools/m6e/merge/upstream-base.txt`. The procedure is in the plan under "P5.3 Maintenance":

1. Fetch upstream and diff from the recorded base.
2. Drop the changes to files M6E deleted with `tools/m6e/merge/filter-upstream-diff.py`.
3. Apply with `git apply --3way` and resolve the conflicts, keeping M6E's deletions.
4. Run `tools/m6e/merge/post-merge-checks.sh --upstream-new <sha>` (`--list` names the checks).
5. Record the merged commit in `upstream-base.txt`, then `./mach clobber` and `./mach build`.

Each tool's docstring has the details.

## Documentation

- [`m6e_browser_agent_instructions.md`](m6e_browser_agent_instructions.md): the specification, covering what M6E is and why, the current status, and the numbered decision log.
- [`M6E_IMPLEMENTATION_PLAN.md`](M6E_IMPLEMENTATION_PLAN.md): how it's built, with a progress log of every step and its commit, and the as-built notes and results of each phase.
- [`tools/m6e/p6-reports/unreachability.md`](tools/m6e/p6-reports/unreachability.md): the proof that the dormant features that remain can't be reached.
- [`CLAUDE.md`](CLAUDE.md) and [`AGENTS.md`](AGENTS.md): working rules for development with Claude Code.

## Licence

M6E is based on the Firefox source code from the [Mozilla project](https://www.mozilla.org/) and is distributed under the [Mozilla Public License 2.0](LICENSE). The About dialog and `about:license` carry this attribution. Firefox and Mozilla are trademarks of the Mozilla Foundation. M6E is not affiliated with or endorsed by Mozilla. The [`AUTHORS`](AUTHORS) file points to the credits of the Mozilla contributors whose work M6E builds on.

M6E was created by Ravi Subramaniam using Claude Code.
