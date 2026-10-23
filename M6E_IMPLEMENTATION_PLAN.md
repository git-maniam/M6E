# M6E Implementation Plan

How the spec (`m6e_browser_agent_instructions.md`) is built: the working method, what each finished step changed, and the plan for the remaining steps. Decision numbers (D1 to D20) refer to the spec's decision log.

## 1. Working method

### Step loop
1. Implement the step.
2. Build:
   - **Front-end changes (JS, CSS, FTL, XHTML, prefs):** `./mach build faster`.
   - **C++ or Rust changes:** `./mach build`.
   - **Configure changes (mozconfig, `moz.configure`):** these rebuild the whole tree (40 minutes to 2 hours), so batch them.
3. Check the result in the running browser (see Tools).
4. Commit as `M6E P<phase>.<step>: <summary>`, then push to `origin/main`.

Don't start the next step until the current one builds and checks out. Split a step that grows (`P1.6a`, `P1.6b`) rather than committing a broken tree. Write long command output to `artifacts/` (git-ignored) and read the file.

### Tools (`tools/m6e/`)

| Tool | Use |
|---|---|
| `chrome_eval.py` | `./mach python tools/m6e/chrome_eval.py --binary obj-m6e/dist/M6E.app/Contents/MacOS/m6e --script check.js --headless` runs a JS snippet with chrome privileges on a fresh profile and prints the JSON it returns |
| `measure.py` | Startup timings, memory, and every host a fresh profile contacts in 5 idle minutes (plain launch, not Marionette) |
| `push/push_check.py` | Web Push end to end: subscribes, sends real pushes, and checks notifications with the page open, with its tab closed, after a restart, and with no windows (`--rfp` repeats it with fingerprinting resistance) |
| `branding/make-icons.sh` | Regenerates every branding image from `M6E-Icon.png` |
| `branding/replace-fox-art.sh` | Replaces every image listed in `branding/fox-art.txt` (Firefox's fox mascot Kit, fox illustrations and Firefox logos) with the M6E icon, keeping each file's name and size |
| `branding/scan-strings.py` | Lists user-visible en-US strings that still name Firefox or Mozilla; the expected result is 0 |

**Marionette pitfalls:** its test profile disables New Tab feeds, plugin updates, Safe Browsing and update traffic. Pass `browser.newtabpage.activity-stream.testing.shouldInitializeFeeds=true` at launch when checking New Tab, and select a tab before measuring its layout. `Services.search` doesn't exist in this tree; import `moz-src:///toolkit/components/search/SearchService.sys.mjs`.

### Environment
- **Toolchain (D17):** the toolchain comes from Homebrew and is pinned: rust, cbindgen, nasm, llvm, lld, wasi-libc and wasi-runtimes. Xcode 27 provides clang and the SDK.
  - The mozconfig sets `--disable-bootstrap`, with Homebrew's LLVM and WASI sysroot for the WebAssembly library sandbox.
  - `mach bootstrap` fails here because of Cloudflare Gateway TLS inspection.
- **Machine:** arm64, 14 cores, 48 GB RAM. The current full build takes about 40 minutes.
- **Upstream base (D9):** commit `034cd8a8` (2026-10-21). The initial import had dropped every hidden root file; the build-relevant ones (`.cargo/`, `.ycm_extra_conf.py`, `.gitignore`, formatter and linter configs) were restored from that commit.

## 2. Progress log

| Step | Result | Commit |
|---|---|---|
| P0.1–P0.2 | Ignore build output; mozconfig | `ca967e3f05`, `682404dc3e` |
| — | Restore the root dotfiles missing from the import | `780db83fc3` |
| P0.4 | Measurement tool and baseline | `5fb6ab02a2` |
| P1.1 | Android and iOS removed | `30fbdced8a` |
| P1.2 | Windows removed | `757c45ba83` |
| P1.3 | Linux and GTK removed | `85e00c90be` |
| P1.4 | Platform-only source subdirectories removed | `eb217d8cac` |
| P1.5 | English-only interface, en-GB dictionary, `m6e.js` created | `6ac4b3580e` |
| P1.6a | Browser UI forced light | `69cde78f67` |
| P1.6b | Default theme is the only theme | `4ab3ef85f9` |
| P1.7 | Firefox View removed | `751fd3e391` |
| P1.8 | Accounts and Sync removed from the UI | `8308869d0d` |
| P1.9 | VPN switched off | `f49f2658bb` |
| P1.10 | Telemetry and data collection off | `f910e2cd03` |
| P1.11–P1.12 | Crash reporter, updater and Normandy off; Mozilla services off; Remote Settings allowlist | `a4906630a8` |
| P1.13 | Sponsored content and AI off | `bcaace91d2` |
| P1.14 | Remaining background traffic removed | `7657900de6` |
| P2.1 | Branding images generated | `abb9739dc8` |
| P2.2 | M6E branding and app identity | `651df97357` |
| P2.3 | Firefox/Mozilla removed from user-visible strings | `b455477dbb` |
| P2.4 | Licence attribution | `575089f9fd` |
| P2.5 | Native macOS look | `06ecd100a0` |
| — | About dialog description text | `2095eac911` |
| P3.1 | Search limited to Google and Bing; adding engines blocked | `faed4d1811` |
| P1.11b | In-app restart works without the updater | `29b3f07e31` |
| P1.4b | Lint configs repaired; file modes fixed | `c2f28c435a` |
| P2.3b | fluent-lint accepts de-branded words | `5998fb7ec1` |
| P3.2 | Bookmarks toolbar setting | `e4970f95a9` |
| P3.3 | Reading Mode | `c1a620c1de` |
| P3.4 | Privacy hardening defaults (D18) | `1d0df295aa` |
| P3.5 | Privacy Shield | `1bce2fc615` |
| P3.5b | Privacy Shield off means no blocking (D20; reworked in P3.7) | `48b7b26941` |
| P3.6 | Push notifications verified; push check tool | `4b25cc5ac6` |
| P2.6 | Fox artwork replaced with the M6E icon | `f6b2bfd7eb` |
| P3.7 | Tracker Blocker button with a two-way, session-only per-site switch | `94d2a79bd4` |
| P3.2b | No AI buttons in the default toolbar layout | `46b2ba9b0b` |
| P2.7 | Home page splash image above the search box | `a37d26869c` |
| P2.8 | "No Internet Connection" picture on offline error pages | `bd0f0d48fe` |
| P4.1 | Phase 3 build measured: 681 ms first paint, 449 MB, 8 hosts; `measure.py` defaults to 10 runs | this commit |

## 3. Results by phase (as built)

### Phase 0: Setup and baseline
- **mozconfig:** `--enable-application=browser`, `--target=aarch64-apple-darwin`, `--enable-optimize`, `--disable-debug`, objdir `obj-m6e`, and `--disable-bootstrap` with the Homebrew WebAssembly toolchain. Later steps added `--disable-crashreporter`, `--disable-updater`, `--with-branding=browser/branding/m6e`, `--with-distribution-id=org.m6e` and `MOZ_APP_BASENAME=M6E`. Never use artifact builds: they download Mozilla binaries.
- **Baseline (stock Nightly, `artifacts/baseline-P0`):**
  - **Startup:** first paint at 785 ms.
  - **Memory:** 698 MB idle across 8 processes.
  - **Network:** 21 hosts received requests, plus 40 hosts that were only looked up over DNS.

### Phase 1: Debloating
- **Platform removal (P1.1–P1.4):** about 18,000 files are gone, including `mobile/`, `widget/{android,uikit,windows,gtk,x11}`, the Windows and Linux sandboxes, installers, system integration and platform-only source subdirectories. Rules for any future removal (each one caught a real breakage):
  - **Cargo:** remove workspace members and `path =` dependencies, even `cfg`-targeted or optional ones, then prune `Cargo.lock` offline: copy `.cargo/config.toml.in` to `.cargo/config.toml`, run `cargo metadata --offline --format-version 1 > /dev/null`, and delete `.cargo/config.toml`. A build with no Rust changes never runs Cargo, so breakage can hide until later.
  - **Glean:** if remaining code records a metric from a removed `metrics.yaml`, restore just that file at its original path (as with the GeckoView, WMF and push-notification-helper metrics); otherwise drop the index entry.
  - **Elsewhere:** also check test-manifest `include` lines, `LICENSED_UNDER` paths, `mach` command mappings, and unconditional `LOCAL_INCLUDES`. `./mach build-backend` finds most of these in seconds.
  - **Keep:** vendored crates (their checksums are verified), `build/rust/windows` (a Cargo patch target) and `widget/headless`.
- **Languages (P1.5):** the multilingual prefs are locked off, which hides the language pickers, and `all-locales`/`shipped-locales` are en-US. The SCOWL `en_GB-ise` dictionary sits in `extensions/spellcheck/locales/en-GB`.
- **Light UI (P1.6a):**
  - `PreferenceSheet::ColorSchemeSettingForChrome()` always returns Light.
  - `Document::PreferredColorScheme()` returns Light for `ChromeRulesEnabled()` documents (`chrome:`, `resource:` and privileged `about:` pages).
  - So no chrome CSS was edited. Native menus and dialogs inherit the window appearance, and web content still follows macOS.
- **Themes (P1.6b):** `XPIDatabase.isUsableAddon()` refuses every theme except the default one. The built-in Light, Dark and Alpenglow themes are removed, and the theme UI is hidden in about:addons, Settings, Customize mode and the profile picker.
- **Firefox View (P1.7):** the page, `about:firefoxview`, the button and the menu item are gone. Widgets the sidebar shares are kept, and `FirefoxViewHandler` remains as a stub for SessionStore.
- **Accounts, VPN, telemetry, crash reporter, updater, Normandy (P1.8–P1.12):** switched off with the prefs the matching enterprise policies use, plus configure flags. The code stays dormant (D13), and `MOZ_DATA_REPORTING` is off, which removes the data-collection settings.
- **Remote Settings (D2):** `Utils.isAllowedCollection()` (`services/settings/Utils.sys.mjs`) is enforced in `RemoteSettingsClient.sync()` and `maybeSync()` and in the attachment downloader. `pullStartupBundle()` never runs.
- **DRM (D10):** a blank `media.gmp-manager.url` makes the plugin installer fall back to `toolkit/content/gmp-sources/` (Google and Cisco).
- **DNS over HTTPS (D11):** `network.trr.mode` defaults to 2 (not locked), and `DoHController` no longer starts.
- **Sponsored content and AI (P1.13–P1.14):** New Tab sponsored content, stories, wallpapers and server-backed widgets are off. AI is blocked through `browser.ai.control.*`. DNS prefetching, link prefetching and speculative connections are off by default.
- **Result (`artifacts/phase1-P1.14b`):**
  - **Network:** a fresh profile left idle contacts only the 8 allowed hosts: Remote Settings ×3, push, DoH, and the plugin downloads ×3.
  - **Startup:** first paint at 777 ms, which is within noise of the baseline.
  - **Memory:** blank-tab idle memory is 448 MB, down from 698 MB, so 36% less.

### Phase 2: Rebranding and UI
- **Branding images (P2.1):** `tools/m6e/branding/make-icons.sh` uses Swift/CoreGraphics, `sips`, `iconutil` and `actool`.
  - It finds the white tile in `M6E-Icon.png`, drops the grey background, shadow and watermark, and fits the tile onto Apple's 1024 px grid.
  - **Output in `browser/branding/m6e`:**
    - `firefox.icns`, `document.icns`, `disk.icns` and `document.ico`;
    - `Assets.car` (`AppIcon`);
    - window and About images, and a DMG background;
    - an embedded-PNG `about-logo.svg` and `document_pdf.svg`;
    - system-font M6E wordmarks.
  - File names are stable, so rerunning the script with new artwork updates everything.
- **Identity (P2.2, D6, D14):**
  - **Branding settings:** `browser/branding/m6e` (`configure.sh`: `MOZ_APP_DISPLAYNAME=M6E`, `MOZ_MACBUNDLE_ID=browser`; every brand term M6E; a light About style).
  - **Configure:** `browser/confvars.sh` and the mozconfig select the M6E branding, and `MOZ_APP_VENDOR` is M6E.
  - **Removed:** the Nightly, Aurora, official and unofficial branding, and the Windows-only desktop launcher.
  - **Checked:** the bundle ID, the executable, the profile folder, and that the app ID is kept.
- **Strings (P2.3, D7, D16):**
  - **Strings:** the scanner found 76 strings and all were rewritten.
  - **Removed:** the default bookmarks are empty, and `about:mozilla` is gone.
  - **Hidden:** Help-menu links to Mozilla and the web-compatibility reporter, support links (`moz-support-link` hides itself when there is no support URL), and More from Mozilla.
  - **Renamed and replaced:** the Settings landing pane is now "General"; the Kit mascot and Firefox glyph are replaced; the New Tab defaults are Wikipedia, YouTube and Reddit.
- **Attribution (P2.4):** the About dialog shows the description text from spec §1 and the MPL attribution, with Licensing Information as its only link. `about:license` opens with the attribution. `about:rights` is removed.
- **Fox artwork (P2.6, requested 2026-10-23):** 123 images of Firefox's fox mascot (Kit), fox illustrations and Firefox logos now show the M6E icon. They appear in error pages, feature callouts, the Trust panel, profiles, sidebar, New Tab, onboarding and about:debugging.
  - **Finding them:** 90 by file name (`fox`, `kit`, `firefox`), and 33 more by reviewing contact sheets of every illustration-sized image (artifacts `sheet-*.png`).
  - **Tool:** `tools/m6e/branding/replace-fox-art.sh` with the list `fox-art.txt`. An SVG embeds the 256 px icon centred in its original viewBox; a PNG or GIF is redrawn at its pixel size (`icon-tool.swift fit`). File names are unchanged.
  - **Left out:** the Windows-only `browser/components/shell/icons/kit.ico`, which macOS never shows.
- **Home page splash (P2.7):** New Tab's `Logo` component (`browser/extensions/newtab/content-src/components/Logo/`) shows `data/content/assets/m6e-home-splash.jpg`, which is `home_page_splash.png` as JPEG (760 KB instead of 2.7 MB), in place of the logo and wordmark.
  - **Sizing:** `.m6e-home-splash` in `_Logo.scss` sizes it as `min(50vw, 50vh)` wide with a 3:2 aspect ratio and rounded corners.
  - **Build:** `./mach build faster` regenerates the New Tab bundles (it needs Node). Checked at 1280 and 900 px wide.
- **No Internet page (P2.8):** `NET_ERROR_ILLUSTRATIONS.noInternet` is `chrome://global/skin/illustrations/no-internet.jpg`, made from `no_internet.png` as JPEG (670 KB).
  - **When it shows:** `net-error-card.mjs` uses it for `netOffline` and `NS_ERROR_OFFLINE`, and for errors that normally show the no-connection illustration while `navigator.onLine` is false. A mistyped address while online keeps the usual illustration.
  - **Layout:** `aboutNetError.css` stacks the picture above the text (class `no-internet` on the card) at `min(36em, 80vw, 60vh * 1.5)`.
  - **Checked:** with `Services.io.offline` (or `m6e -offline <url>`) and on an online DNS error.
- **macOS look (P2.5, D15):** `toolkit/themes/shared/m6e-tokens.css` is imported from the generated `tokens-shared.css`, and the token generator's header helper keeps that import.
  - **Accent:** follows `AccentColor`.
  - **Toolbar:** a neutral grey tab strip and toolbar with no Nova gradient, a white selected tab with a neutral edge, and a white page canvas.

### Phase 3: Features and privacy
- **Search (P3.1, D4, D5):**
  - **Configuration:** `tools/m6e/search/prune-search-config.py` rewrites `search-config-v2.json` and `-v3.json` to just `google` and `bing`.
    - Each engine has a single variant for all regions and locales. Google is the global and private default, with no region defaults, engine orders or listed locales.
    - Removed: `partnerCode`, the `pc`, `form`, `channel` and `ep` parameters, and any `{partnerCode}` value.
    - Kept: Google's `client=firefox` on suggestions and trending, because it selects the JSON format the browser parses.
    - The script also cuts the icons to Google's and Bing's bundled images (and updates `moz.build`) and empties the default-override allowlist. Rerun it after every upstream merge.
  - **No added engines:** the locked pref `browser.search.bundledEnginesOnly` drives three `M6E:` changes:
    - `SearchService.addUserEngine()` and `addOpenSearchEngine()` throw `not-allowed`;
    - `#addEngineToStore()` skips every engine that isn't a `ConfigSearchEngine` (add-on, policy, and engines saved in the profile);
    - `Services.policies.isAllowed("installSearchEngine")` returns false, which hides the Settings Add button, the context-menu and urlbar offers, and page OpenSearch offers.
  - **Tests:** `./mach test toolkit/components/search/tests/xpcshell` gives 107 passed and 3 searchconfigs passed.
    - `head_search.js` unlocks the gate and the region URL, because upstream tests use OpenSearch, add-on and region fixtures. `test_m6e_bundled_engines_only.js` checks the shipped behaviour.
    - Removed: `test_searchconfig.js`, `test_searchconfig_google_additional.js`, `test_distributions.js` and `test_appProvided_icons_updates.js`.
  - **Test-harness fixes found on the way:**
    - mozinfo reports `appname` `firefox` for M6E, otherwise `firefox-appdir` is ignored and xpcshell loads no browser files;
    - `testing/profiles/base/user.js` sets `network.trr.mode` 0;
    - manifestparser runs on Python 3.15.
  - **Checked in the browser:** only Google and Bing, default Google; plain `https://www.google.com/search?q=…` and Bing URLs; user and OpenSearch adds refused; Settings lists the two engines with no Add button; a urlbar search opens a plain Google URL.
  - **Network (`artifacts/P3.1-measure`):** the same 8 hosts. Widevine came from `dl.google.com` this time instead of `edgedl.me.gvt1.com`.
- **Restart (P1.11b):**
  - **Cause:** macOS restarts used to go through the updater binary as a relauncher (bug 2036593). P1.11 removed the updater, so a restart quit M6E for good.
  - **Fix:** without `MOZ_UPDATER`, `SetupMacCommandLine()` (`toolkit/xre/nsCommandLineServiceMac.mm`) has `/bin/sh` wait until the old process has exited or become a zombie, then exec the executable with the inherited environment.
  - **Checked:** the overlap test in `test_relaunch_dock_overlap.py` passes, and `measure.py` completes again (`artifacts/P3.1b-measure`: 447 MB blank-tab memory and the same 8 hosts; that startup run overlapped with lint, so it isn't a valid timing).
- **Lint (P1.4b, P2.3b):**
  - **Configs:** the Android linters are removed, along with stale include and exclude paths and the `mobile/` entries in `eslint-subdirs.config.mjs`.
  - **Linter changes:** lintpref skips locked prefs. fluent-lint reads the M6E brand file and has a `generic-names` list for de-branded words.
  - **File modes:** the import had made every file executable, so 463,326 files are now 644. Only `#!` scripts and binaries keep the executable bit.
  - **Status:** `./mach lint` works, except for clang-format, pnpm-audit and cargo-audit, which need `mach bootstrap` (D17). For C++, use Homebrew's `/opt/homebrew/opt/llvm/bin/clang-format`.
- **Bookmarks toolbar (P3.2):**
  - **Setting:** Settings, Appearance has a "Bookmarks toolbar" group with a "Show bookmarks toolbar" switch (`showBookmarksToolbar` in `config/appearance.mjs`). On means `always` and off means `never`, applied through `CustomizableUI.setToolbarVisibility()` so every window updates.
  - **Default:** `never` (`m6e.js`), because the switch can't represent Firefox's `newtab` default. CustomizableUI reads the area's default from that pref, so Restore Defaults matches.
  - **Checked:** the switch works both ways and follows View-menu changes. Import from Safari, Chrome and Chromium is available.
  - **Tests:** `browser_appearance_pane.js` and the new-tab toolbar tests pass. The new-tab tests pin `newtab`.
  - **Known failures, from earlier phases:**
    - (Fixed in P3.2b.) The default placements no longer list the AI buttons `smartwindow-group-tabs-button` and `ai-window-toggle`, which P1.13 leaves unplaced, so Customize starts in its default state. `browser_bookmarks_toolbar_collapsed_restore_default.js` cycles through the visibility states that differ from M6E's default, and passes.
    - Customize tests still fail where they rely on the accounts button or Synced Tabs, which P1.8 removed: `browser_923857_…`, `browser_remote_tabs_button.js` and `browser_synced_tabs_menu.js`. They also leave dirty state that can fail the tests run after them.
    - `browser_bookmarks_toolbar_telemetry.js` fails because telemetry is off.
- **Reading Mode (P3.3):** Reader View is modified in place, and `readability/` is untouched.
  - **Trigger:** the urlbar button still appears on detected articles. The View menu item and the shortcut work on any http(s) page (`AboutReaderParent.updateReaderButton`).
  - **Clutter:**
    - `ReaderMode._readerParse()` removes ad slots (`AD_SLOT_SELECTOR`: ad ids and classes, `data-ad*`, advertisement labels) from a clone before Readability runs. Readability rewrites those containers and drops their ids and classes.
    - `AboutReader._removeClutter()` then removes iframes, embeds, video, audio, `nav`, `aside`, forms, "Advertisement" labels, figures without media, and inline colours and backgrounds.
  - **Typography:** `_TEXT_SCALE` 1.25 multiplies every font-size step, so the default is 25 px instead of 20 px.
  - **Colours:** `reader.color_scheme` is locked to `light`, with `light` its only value. The light foreground is `#000000`, and the colours menu is `hidden` when there is a single scheme. High contrast mode still applies.
  - **Images:** images under 48 px in both dimensions are dropped; article images are kept.
  - **Checked:** a clutter test page, plus Wikipedia (Auto rickshaw), the Rust 1.0 blog post and a Gutenberg book, all black on white at 25 px with the images kept.
  - **Tests:** `browser_readerMode_m6e.js` is new, and the colour-scheme tests are removed. The reader suite gives 149 passed. The one failure, `browser_localfile_readerMode.js`, happens only locally, because test files under `obj-m6e/_tests` are symlinks and the address bar shows the resolved source path.
- **Privacy hardening (P3.4, D18):** all of these are defaults, not locks, so the Privacy Shield (P3.5) can switch them.
  - **Strict ETP:** new profiles start in the strict category. `browser.contentblocking.initialCategory` is `strict` in `m6e.js`, and `ContentBlockingPrefs.init()` applies it when the profile has no category yet, as if the user had chosen it. Choosing Standard or Custom persists.
    - Strict includes Fingerprinting Protection (`privacy.fingerprintingProtection`). `privacy.resistFingerprinting` stays off.
    - Setting strict values as pref defaults doesn't work: "Standard" means "every pref at its default", so the UI would show Standard and choosing it would change nothing.
  - **Other prefs:**
    - GPC;
    - HTTPS-Only;
    - `XOriginTrimmingPolicy` 2;
    - WebRTC `default_address_only` and `proxy_only_if_behind_proxy`;
    - no TLS 0-RTT and safe renegotiation required;
    - expert bad-cert pages;
    - punycode IDNs;
    - no PDF scripting;
    - no cross-origin subresource HTTP auth;
    - no window move or resize.
  - **Rejected:** strict certificate pinning (`security.cert_pinning.enforcement_level` 2) breaks pinned sites behind TLS inspection, such as YouTube via Cloudflare Gateway on this network.
  - **Checked:**
    - Notification, Push and service workers are intact, and the time zone isn't spoofed.
    - Google, Wikipedia, YouTube, Reddit and BBC load.
    - An HTTP-only site gets the HTTPS-Only page.
  - **Tests:**
    - `testing/profiles/base/user.js` sets `initialCategory` empty and the P3.4 prefs to Firefox's values. HTTPS-Only otherwise blocks the harness on `http://mochi.test`.
    - The `etp` Settings tests pass, `browser_m6e_initial_category.js` is new, and the TCP section test expects a hidden support link (D7).
  - **Maintenance:** after merges, check `ContentBlockingPrefs` still honours `initialCategory`.
  - **Network (`artifacts/P3.4-measure`):** 8 hosts. The Widevine component came from `www.google.com/dl/…` this time. Google rotates that download between `edgedl.me.gvt1.com`, `dl.google.com` and `www.google.com`, all D10.
- **Privacy Shield (P3.5):**
  - **Module:** `browser/components/protections/M6EPrivacyShield.sys.mjs`. `apply({enabled, maximum})` does the switching:
    - **Off:** strict ETP becomes Standard, and each hardening pref from P3.4 still at M6E's value gets Firefox's value (`COMPATIBILITY_VALUES`).
    - **On:** the reverse.
    - **Maximum:** while the Shield is on, maximum mode adds `privacy.resistFingerprinting` (D18).
    - Prefs changed by hand and a Custom category are left alone.
  - **Settings:** the `privacyShield` group is first in the Privacy pane. It has a "Privacy Shield" switch (`m6e.privacyShield.enabled`, default on) and a nested "Maximum protection" checkbox (`m6e.privacyShield.maximum`), which is disabled while the Shield is off. Changing either one offers to reload tabs.
  - **Tests:** `etp/browser_m6e_privacy_shield.js`, which is in `browser-srd.toml` because the Shield exists only with the settings redesign. The `etp` suite gives 875 and 923 passed.
  - **Off means no blocking (P3.5b, D20):** with a strict or standard category, off switches to `custom` with every blocking feature off (`NO_BLOCKING_VALUES`): tracking content, social trackers, fingerprinters, cryptominers, email trackers, the level-2 list, query stripping and Fingerprinting Protection, in normal and private windows. `cookieBehavior` stays 5 (Total Cookie Protection). On returns to strict when the category is standard or blocking is still all off. A custom setup the user made is kept.
  - **Not a bug:** the security warning card on the Privacy pane appears only in Marionette and test profiles. They turn off password saving and the popup blocker.
- **Tracker Blocker button (P3.7, D19, D20):**
  - **Trust panel:** this Nightly's Trust panel (`browser-trustPanel.js`, `#trust-icon-container`) already provides the button, the count pill and the panel, so P3.7 builds on it.
    - `m6e.js` turns on `browser.urlbar.trackerCount.featureGate`, so the pill reads "N trackers blocked" and then N.
    - With protection off for the site, the shield has an X and shows no number.
    - The panel lists blocked trackers, cookies, social trackers, fingerprinters and cryptominers with their domains.
    - The toggle is labelled "Tracker Blocker". Breach alerts are locked off, because they offered Mozilla Monitor scans.
  - **Two-way, session-only override (D20):** `M6EPrivacyShield.isSiteProtected()` and `setSiteProtected()` drive the panel toggle.
    - **Shield on:** off for a site adds the ETP allow-list permission and the HTTPS-Only session exemption. `ContentBlockingAllowList` now always uses `EXPIRE_SESSION`.
    - **Shield off:** on for a site adds a session `m6e-protect` permission for its http and https origins.
    - **C++ hooks:** with the Shield off, every site is exempt unless it has `m6e-protect`:
      - `ContentBlockingAllowList::Check()` covers tracking protection, cookie restrictions and FPP;
      - `nsContentUtils::ETPSaysShouldNotResistFingerprinting()` makes the allow list exempt RFP too;
      - `nsHTTPSOnlyUtils::TestIfPrincipalIsExempt()` covers HTTPS-Only.
    - The blocking prefs stay strict either way, so P3.5b's pref flipping was reverted. The Shield only flips the global-only hardening prefs.
  - **Checked on BBC News (`tools/m6e/trackers/tracker_count_check.py`):**
    - Shield on: 7 blocked; site off: inactive, 0 blocked, session-only permissions; site back on: blocking resumes.
    - Shield off: inactive, 0 blocked; site on: 9 blocked with the count showing.
    - HTTPS-Only on `http.badssl.com`: enforced with the Shield on, not enforced with it off, and enforced again with the Shield off and the site turned on.
  - **Tests:**
    - `etp/browser_m6e_privacy_shield.js` covers the Shield and both override directions through the page's `cookieJarSettings.isOnContentBlockingAllowList`, plus the session expiry.
    - The Trust panel suite gives 498 passed. Its `head.js` unlocks breach alerts for the upstream tests.
    - The `browser-protectionsUI` suite gives 734 checks with 0 unexpected. It must run without `--headless`, and alone, because window focus matters.
  - **Marionette:** profiles have no tracker lists (see the build-environment notes); the check tool restores them.
- **Push notifications (P3.6, D12):** no code change was needed, because M6E's defaults already give reliable push.
  - **Defaults:** `dom.push.enabled`, `dom.push.connection.enabled`, `wss://push.services.mozilla.com/`, and `alerts.useSystemBackend` (macOS Notification Center).
  - **`tools/m6e/push/push_check.py`:** serves a service-worker page on localhost, subscribes through Mozilla's push service, sends real pushes to the endpoint with curl, and checks delivery and the shown notification in four cases:
    - with the page open;
    - with its tab closed;
    - after an in-app restart;
    - that the app keeps running with no windows.

    All pass, with and without fingerprinting resistance (`--rfp`). Marionette turns `dom.push.connection.enabled` off, so the tool turns it back on.
  - **Manual check:** the native banner and the macOS permission prompt on first use (System Settings, Notifications, M6E), because headless runs can't show them.
  - **Not done:** the optional "open at login, hidden" login item. It's native code and needs the user's agreement first.

## 4. Next step: Phase 4, performance (P4.2 build flags)

**Resuming:** the user starts a session with "Resume from Phase 4". Before continuing:
1. Read `CLAUDE.md`, the spec (`m6e_browser_agent_instructions.md`) and this plan, and run `git log --oneline -15`.
2. Run `git status` and `git fetch`. If `origin/main` is ahead (the user sometimes commits from the IDE), pull first.
3. Build with `./mach build`. The objdir `obj-m6e` normally survives between sessions, so this is incremental; a clean objdir or a mozconfig change means a full build.
4. Commit and push work in progress often, and keep the checkpoint note below current (user rule, connection drops).
5. Measure on a quiet machine: ask the user to quit their own M6E first.

**Checkpoint:** P4.1 is done. P4.2a (Homebrew clang 23 and cross-language ThinLTO) is in `mozconfig` and committed as work in progress; next is the full rebuild, then a check that LTO is really on (`MOZ_LTO` in `obj-m6e/config/autoconf.mk`) and measuring. If the ld64 link fails, try `ac_add_options --enable-linker=lld` (Homebrew `lld` is pinned).

**P4.1 results (Phase 3 build, Apple clang 21, no LTO):**

| Run | First paint | Blank-tab memory | Idle hosts |
|---|---|---|---|
| Stock Nightly baseline (P0.4, 3 runs) | 785 ms | 698 MB (8 processes) | 21 hosts with HTTP requests, plus 40 hosts only looked up over DNS |
| After debloating (P1.14b, 3 runs) | 777 ms | 448 MB | 8 allowed hosts |
| **P4.1, 10 runs** | **681 ms** (range 664–712; session restored 714 ms) | **449 MB** (8 processes); 1497 MB with five sites | **8 allowed hosts**, none only looked up over DNS |

- **Hosts:** Remote Settings ×3 (`firefox.settings.services.mozilla.com`, `content-signature-2.cdn.mozilla.net`, `firefox-settings-attachments.cdn.mozilla.net`), `push.services.mozilla.com`, `mozilla.cloudflare-dns.com` (DoH), `update.googleapis.com`, `ciscobinary.openh264.org` and `www.google.com` (the Widevine download in `gmp-sources/widevinecdm.json`).
- **Noise:** a 3-run measurement of the same build gave 865 ms (752, 875, 865), so `measure.py` now defaults to 10 startup runs. Compare flags with 10-run medians only.
- **A/B baseline:** `artifacts/baseline-P4.1/M6E.app` is a standalone copy of this build (`cp -RL`; the copy errors on dangling links to files Phase 1 removed are harmless). Measure it next to each flag build in the same session.

**Already in place:** C/C++ already builds at `-O3` (`MOZ_OPTIMIZE_FLAGS`). Rust builds at opt-level 2 (the `RUSTC_OPT_LEVEL` default).

**P4.2 order** (each is a full rebuild; measure with 10 runs after each and keep only what helps):
- **P4.2a:** Homebrew clang 23 for C/C++ (`CC`/`CXX`), `--enable-lto=cross` (ThinLTO for C++ and Rust), linked by Xcode 27's ld64 with `MOZ_LD64_KNOWN_GOOD=1`.
  - Rust's LLVM is 23.1.3 and Homebrew clang is 23.1.4, so the bitcode is compatible. The user chose this route on 2026-10-23.
  - The clang driver passes Homebrew's `libLTO.dylib` to ld64. Mozilla links its LTO/PGO mac builds with ld64 because lld regressed performance (`build/macosx/mozconfig.common`).
- **P4.2b:** CPU tuning (`-mcpu=apple-m1` and Rust `-C target-cpu=apple-m1`) plus `RUSTC_OPT_LEVEL=3`.
- **P4.2c:** PGO (`MOZ_PGO=1`). This needs an instrumented build, a profiling run and a final build.

## 5. Remaining steps

### Phase 4: Performance
- **P4.1 Measure:** done (section 4).
- **P4.2 Build flags:** add one flag at a time and measure after each:
  - **LTO:** `--enable-lto=cross` needs a clang whose LLVM matches Rust's. Check Homebrew's `llvm` against `rustc --version --verbose`.
  - **PGO:** `MOZ_PGO=1`.
  - **CPU tuning:** `-mcpu=apple-m1` (C/C++) and `-C target-cpu=apple-m1` (Rust).
  - **Optimisation level:** C/C++ is already `-O3`; Rust `RUSTC_OPT_LEVEL=3`.

  Each flag is a configure change and a full rebuild. Keep a flag only if it builds reliably and helps.
- **P4.3 Profile:** take startup and interaction profiles, then defer or remove front-end work.
- **P4.4 Rust rewrites:** only for a hotspot P4.3 proves, with the user's approval. Prefer native Rust behind XPCOM over WebAssembly.

### Phase 5: Packaging and acceptance
- **P5.1 Package:** run `./mach package` for `M6E.app` and the `.dmg`.
  - **Signing:** sign ad hoc (D8) with `codesign --force --deep -s -`.
  - **DMG layout:** regenerate the DMG `dsstore` layout file, which was copied unchanged from Nightly's branding.
  - **Check:** the DMG shows the M6E artwork and the app launches from `/Applications`.
- **P5.2 Acceptance, on a fresh profile of the packaged app:**
  - **Network:** only the 8 allowed hosts while idle.
  - **Branding:** `scan-strings.py` reports 0, and the attribution is present.
  - **Theme:** Dark Mode leaves the browser UI light, while web pages still follow it.
  - **Search:** only Google and Bing.
  - **Removed features:** absent.
  - **Phase 3 features:** the bookmarks toggle, Reader View rules, the Privacy Shield, the Tracker Blocker button and push all work.
  - **Performance:** startup and memory at least as good as Phase 0.
- **P5.3 Maintenance:** with updates disabled, security fixes need manual upstream merges (D9). After each merge:
  - rerun `prune-search-config.py`, `scan-strings.py` and `replace-fox-art.sh`, and look for new fox artwork to add to `fox-art.txt`;
  - review the Remote Settings allowlist (D2) against upstream's collections;
  - refresh `gmp-sources` (D10);
  - check that the `m6e-tokens.css` import survives in `tokens-shared.css`.

## 6. Risks

| Risk | Mitigation |
|---|---|
| Frozen Nightly base (D9) misses security fixes | Merge upstream periodically (P5.3) |
| Remote Settings allowlist drifts as upstream adds or moves collections (D2) | Review on every merge |
| Mozilla changes or restricts its push service (D12) | Self-host autopush if needed |
| Bundled plugin download lists go stale (D10) | Refresh `gmp-sources` on merges |
| Arkenfox-style prefs break sites | The Privacy Shield master toggle (P3.5) |
| Xcode 27's libc++ warns that the macOS 10.15 deployment target is unsupported | Raise the minimum macOS version if it becomes an error |
| A Homebrew toolchain upgrade changes compilers mid-project | The packages are pinned (`brew pin`) |
| LTO and PGO lengthen builds | Add flags one at a time (P4.2) |

## 7. Session notes
- **Fresh sessions:** start each phase in a fresh session, with the spec, this plan, `CLAUDE.md` and the memory notes as the handoff. "Resume from Phase N" means: follow the resuming checklist in section 4, then continue with that phase.
- **Model choice:** Sonnet suits high-volume, build-driven steps. Opus suits design and profiling (Phases 3 and 4). Fable suits stubborn build-system or security-review problems.
