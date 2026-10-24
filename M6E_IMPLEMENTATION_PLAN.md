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
| `speedometer.py` | Runs the vendored Speedometer 3 offline against one or more builds, alternating them by round, and writes the scores as JSON |
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
| P4.1 | Phase 3 build measured: 681 ms first paint, 449 MB, 8 hosts; `measure.py` defaults to 10 runs | `bc97a8ada7` |
| P4.2a | Homebrew clang 23, ld64 and cross-language ThinLTO: Speedometer +12%, first paint 647 ms | `3e02d45126` |
| P4.2b | Apple Silicon CPU tuning and Rust opt-level 3: within noise, kept as the spec asks | `e3c330b3d2` |
| P4.2c | PGO: Speedometer +15% (median +18%), first paint 597 ms; DMG packaging works | `a169c87934` |
| P4.5 | Upstream merged to `cfa0508a3b` (1,301 files); allowlist tests | `cfd42808e0` |
| P4.6a | 141 platform-only directories deleted (2,195 files) | `3dcdd875c7` |
| P4.6b | 702 platform conditions removed from 286 `moz.build` files; backend proven unchanged | `44250d4d06` |
| P4.6c | `#ifdef`s stripped from 1,403 first-party files (about 62,000 lines); binaries' symbols unchanged | `12a394ab1c` |
| P4.6d | Front-end `#ifdef`s stripped from 19 preprocessed files; outputs unchanged | `14020b7897` |
| P4.7a | 20 platform-only orphan subtrees deleted (193 files); push still works | `596062f662` |
| P4.7b | Normandy and about:studies removed | `8a3c3fac23` |
| P3.8 | Page telemetry button: TTFB, FCP, TTI, page size, script parse/execution, TLS | `7e1542f61b` |
| P4.8 | Security hardening: experimental features off, auto-var-init, sandbox locks, add-on and download prefs, static analysis, dormant pages unregistered | `45bf9d9e22` and earlier |
| P4.9, P4.3 | Final PGO build, full test pass and profiling; Phase 4 complete | `ff0b6ab100` |
| P5.0 to P5.3 | D25 (DoH blocking) verified; app signed, release DMG built and verified; acceptance 15 of 15; merge tooling | `07b3761cbd` |
| P4.8d | Review of M6E's changes: 1 medium, 3 low and 1 info finding fixed, with tests | `cfd42808e0` |

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

## 4. Next step: Phase 6, deleting the dormant features

**Resuming:** the user starts a session with "Resume from Phase 6". Before continuing:
1. Read `CLAUDE.md`, the spec (`m6e_browser_agent_instructions.md`) and this plan, and run `git log --oneline -15`.
2. Run `git status` and `git fetch`. If `origin/main` is ahead (the user sometimes commits from the IDE), pull first.
3. Build with `./mach build`. The objdir `obj-m6e` normally survives between sessions, so this is incremental; a clean objdir or a mozconfig change means a full build.
4. Commit and push work in progress often, and keep the checkpoint note below current (user rule, connection drops).
5. Measure on a quiet machine: ask the user to quit their own M6E first.

**Checkpoint (2026-10-24, Phase 6 in progress):** P6.0 is under way. The mozconfig builds without LTO and PGO for this phase. A clean build of the unchanged tree is running (`artifacts/p6/dev-build-0.log`).
- **Parallel stage running:** the workflow `m6e-phase6-analyse-and-prepare` runs per feature: analyst (`artifacts/p6/analysis/<key>.md`), independent critic (appends an addendum), preparer in a git worktree (branch `m6e-p6/<name>`, notes in `artifacts/p6/prep/<key>.md`), then up to three reviewer/fixer rounds. No builds in this stage. Branches are local only.
- **Next:** when the baseline build finishes, run the baseline tests (`tools/m6e/run_tests.sh`, results in `artifacts/p6/baseline`). Then integrate one branch at a time in the order of the table below (P6.1a to P6.1i): cherry-pick, build, run the feature's suites, compare with the baseline, commit `M6E P6.1x: …`, push.
- **Phases 0 to 5 are complete.** Release artifact: `artifacts/release/M6E-161.0a1-macos-arm64.dmg`, rebuilt by `tools/m6e/package/release.sh`. After Phase 6: upstream merges (P5.3 tooling).
- **Checked on 2026-10-24 with the screen unlocked:** the DMG in Finder, the app installed from the DMG into `/Applications` and launched through LaunchServices, and the two panel suites (10 + 498 passes, 0 failures). See "Phase 5 results".
- **Needs the user:** decide on a Developer ID and notarization (US$99 a year); skim `tools/m6e/merge/nightly-prefs-reviewed.txt`; test a real download on a second Mac; look at real macOS Dark Mode (menus, dialogs, PDF). Details in "Phase 5 results" and "Decisions taken while the user was away".
- **Build notes:** with PGO, any C++ change needs `./mach clobber` first; front-end-only changes use `make -C obj-m6e/faster`, then `release.sh`. Don't test app copies under `/tmp`.

**P4.2c results (PGO):** `MOZ_PGO=1` gives an instrumented build in `obj-m6e/instrumented`, a profiling run (120 profiles merged into a 100 MB `merged.profdata`) and the final build, about 1.5 hours in all.
- **Speedometer 3:** 42.09 ± 8.28 (median 46.41) against P4.2b's 36.55 ± 6.29 (median 39.32), measured in the same session: **+15% mean, +18% median.**
- **First paint:** 597 ms against 612 ms for P4.2b, measured back to back on a quiet machine. A run made during git activity gave 816 ms, so never measure while git or builds are running.
- **Memory and hosts:** 449 MB, 8 hosts.
- **Build fixes needed:**
  - `LLVM_PROFDATA` set by hand in `mozconfig`.
  - `xpcom/tests` and `uriloader/exthandler/tests` skip installing `SIMPLE_PROGRAMS` in the instrumented pass. `rules.mk` doesn't build them there, but the harness install still depended on them.
  - `STRIP=/usr/bin/strip`: LLVM 23's `llvm-strip` rejects the `LC_DYLD_ENVIRONMENT` load command that `browser/app/moz.build` adds for Safari's staged frameworks.
  - `MOZ_PKG_FORMAT=DMG`: developer builds default to a TAR made with GNU-only `tar --mode`.
- **Build cost:** any `mozconfig` change recompiles the whole tree.

**P4.2b results:** `-mcpu=apple-m1` (C/C++), `-C target-cpu=apple-m1` (Rust) and `RUSTC_OPT_LEVEL=3`. Speedometer 3: 37.17 ± 4.60 (median 38.78) against P4.2a's 36.52 ± 4.60 (median 38.51), measured in the same session. First paint 670 ms (range 639–688), 449 MB, 8 hosts. That is within noise. The flags are kept anyway: the spec asks for them, they build reliably, and they don't cost anything. A same-session rerun of P4.2a gave 36.5 against the 39.0 measured earlier, so only compare scores taken in the same session.

**P4.2a results** (the baseline copy and the new build, measured in the same session):

| Build | Speedometer 3 (20 iterations) | First paint (10 runs) | Blank-tab memory | Idle hosts |
|---|---|---|---|---|
| P4.1 baseline (Apple clang 21, lld, no LTO) | 34.79 ± 3.87 | 681 ms (P4.1 run) | 449 MB | 8 |
| **P4.2a** (Homebrew clang 23, ld64, cross-language ThinLTO) | **39.01 ± 4.65 (+12%)** | **647 ms** | 449 MB | 8 |

- **Binary size:** `XUL` shrank from 302.6 MB to 277.7 MB.
- **Checks:** Rust objects in `libgkrust.a` are LLVM bitcode, which confirms cross-language LTO. ld reports "LTO support using: LLVM version 23.1.4". All five sites in the memory run loaded.
- **Noise:** the baseline copy's first-paint run in this session (median 882 ms, worst run 1383 ms) ran while the machine was still busy, so it isn't used.
- **Widevine host:** the one allowed Google host for Widevine varies by run: `www.google.com`, its redirect target `dl.google.com`, or the other mirror listed in `gmp-sources/widevinecdm.json`, `edgedl.me.gvt1.com`.
- **Same-session comparison:** `tools/m6e/speedometer.py` runs the vendored Speedometer 3 offline, alternating builds by round. Startup time hardly reflects compiler flags, so use Speedometer to judge them.

**P4.2a toolchain findings:**
- **libc++ headers:** Homebrew clang uses its own libc++ 23 headers, which crash bindgen (`Not an item: ItemId(…)` in the `gecko-profiler` build script) and don't match the system `libc++.dylib`. `CXX` therefore adds `-nostdinc++ -isystem <SDK>/usr/include/c++/v1`, which configure also passes to bindgen.
- **Linker:** with Homebrew clang, configure picks lld, which can't resolve clang 23's `objc_msgSendClass$…` stubs (in `ChannelPrefs`). `--enable-linker=ld64` uses Xcode 27's ld, and the clang driver hands it Homebrew's `libLTO.dylib`.
- **Error messages:** mach's agent mode filters the terminal output. The full log is `obj-m6e/.mozbuild/logs/build/build_log_<time>.log`.

**P4.1 results (Phase 3 build, Apple clang 21, no LTO):**

| Run | First paint | Blank-tab memory | Idle hosts |
|---|---|---|---|
| Stock Nightly baseline (P0.4, 3 runs) | 785 ms | 698 MB (8 processes) | 21 hosts with HTTP requests, plus 40 hosts only looked up over DNS |
| After debloating (P1.14b, 3 runs) | 777 ms | 448 MB | 8 allowed hosts |
| **P4.1, 10 runs** | **681 ms** (range 664–712; session restored 714 ms) | **449 MB** (8 processes); 1497 MB with five sites | **8 allowed hosts**, none only looked up over DNS |

- **Hosts:** Remote Settings ×3 (`firefox.settings.services.mozilla.com`, `content-signature-2.cdn.mozilla.net`, `firefox-settings-attachments.cdn.mozilla.net`), `push.services.mozilla.com`, `security.cloudflare-dns.com` (DoH, D25; `mozilla.cloudflare-dns.com` before 2026-10-24), `update.googleapis.com`, `ciscobinary.openh264.org` and `www.google.com` (the Widevine download in `gmp-sources/widevinecdm.json`).
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

**P4.9 results (final build: PGO and `-ftrivial-auto-var-init=zero`, 2026-10-24):**

| | P4.2b copy (LTO and CPU tuning, no PGO) | Final build |
|---|---|---|
| Speedometer 3 (20 iterations, same session) | 37.98 ± 4.65 (median 39.5) | **44.70 ± 2.97 (median 45.7), +17.7%** |
| First paint (10 runs) | 631 ms (583–671) | 649 ms (620–686) |
| Memory: blank tab / five sites | 472 / 1,796 MB | 486 / 1,644 MB |
| Idle hosts | | 8, the allowed list |

- **Zero-initialisation cost:** PGO alone gave +15% (P4.2c), so zero-initialisation costs nothing measurable.
- **First paint** is within noise; the ranges overlap.
- **Against the Phase 3 build:** first paint 681 ms → 649 ms, and Speedometer 34.8 → 44.7 (about +28%, measured in different sessions).
- **Blank-tab memory** rose from 449 MB (P4.1) to 486 MB. Check this in Phase 5.
- **Tests on the final build:**
  - **Passing:**
    - Privacy Shield, ETP and HTTPS-First: 939 pass.
    - Page telemetry: 10/10.
    - Trust panel: 498/0, run alone.
    - Nimbus: 33/33, after its test head unlocked the prefs M6E locks.
    - Anti-tracking: 17/17.
    - Remote Settings: 18/18 plus one skip. `head_settings.js` lets upstream tests see every collection as allowed, `test_m6e_allowed_collections.js` restores the real check, and the startup-bundle test is skipped because P1.12 removed that download.
    - HTTPS-Only: 8 pass. Reader View: 166 pass.
    - `push_check.py`: all four cases pass.
    - `scan-strings.py`: 0. Lint: clean.
  - **Environmental failures:** this network's DNS resolves nonexistent hosts (`browser_console_logging.js`, `websocket-unexpected`), and the symlinked `_tests` path breaks `browser_localfile_readerMode.js`.
  - **By design:**
    - Toolkit modules: fixed GMP URLs (D10) and region lookup off.
    - Enterprise policies: no updater, no added search engines (D5), and DoH (D11).
    - One race between parallel runs (`test_Services.js`), which passes 3/3 alone.
- **Found and fixed in P4.9:**
  - **P4.8f's locks:**
    - Locked `extensions.autoDisableScopes` and `enabledScopes` kept the test profiles from enabling the harness add-ons. PGO's profiling run waited about 8 hours for its quitter add-on.
    - Locked `xpinstall.signatures.required` made every mochitest abort with `ERROR_SIGNEDSTATE_REQUIRED`.
    - All three are now unlocked defaults.
  - **P4.8a's WebCodecs line:** turned off WebCodecs, which desktop Release ships. Removed.
  - **`tools/m6e/profile.py`:** shadowed Python's `profile` module for every tool in that directory. Renamed to `gecko_profile.py`.
  - **Downloads:** P4.8f's prefs added to `testing/profiles/base/user.js`.
  - **Lint configs:** entries for files deleted in P4.6 and P4.7 removed.
- **Running tests in parallel:** mochitest suites must run one at a time (they share port 8888), and the panel suites need the machine to themselves. Even a headless M6E launched alongside made `browser_trust_panel_pages.js` time out. xpcshell suites can run alongside.

**P4.3 results (profiles from `tools/m6e/gecko_profile.py`, final build):**
- **Startup:** the parent main thread was busy for 1.28 s of the first 10 s of a warm start (87% idle). JS is 13% of the busy time, about 170 ms. The largest native costs are system calls (`mach_timebase_info_trap`, the profiler's own timestamps, and `mach_vm_protect`, JIT memory protection). No front-end module exceeds about 7%: `BrowserUtils` category startup calls, the `<browser>` element, extension startup (`ExtensionParent`, `AddonManager`, `XPIProvider`) and `browser-init`.
- **Interaction** (two sites, the address bar, the page telemetry panel): 650 ms busy in 10 s, with no hot spot. `Tabbrowser` is 6.5%; the rest is the Marionette harness and the address bar at about 2% each.
- **Decision:** nothing is worth deferring now. Two of the six built-in extensions that start every time belong to features marked for deletion: `ipp-activator` (the VPN) and `addons-search-detection` (telemetry only). They are added to P6.1, which removes their startup cost too.
- **P4.4 (Rust rewrites):** skipped. Profiling found no JS hotspot that would justify one.

### P3.8 Page telemetry button (D29; added 2026-10-23)
- **C++:**
  - SpiderMonkey `JSTimers` gains `parseTime`, accumulated on main-thread compiles.
  - `ScriptLoader` adds off-thread compile durations.
  - A `[ChromeOnly]` `Document` getter returns the page's script parse and execution times and Gecko's time-to-first-interactive.
- **Front end:**
  - A JSWindowActor reads Navigation and Paint Timing plus that getter.
  - A parent-process network observer counts transfer sizes per top-level page.
  - A `#m6e-page-telemetry-button` in the address bar opens a panel with the seven figures.
- **Tests:** a browser test for the panel and its values.

- **P3.8 results:** the gauge button (`#m6e-page-telemetry-button`, tooltip "Telemetry") sits after the Tracker Blocker. Its panel shows the seven figures and refreshes every second while open.
  - **On Wikipedia:** TTFB 1,161 ms, FCP 1,443 ms, TTI 2,041 ms, 84.2 kB in 7 requests, script parse 1 ms, execution 4 ms, TLS 324 ms.
  - **How it was built:**
    - The button reuses `.identity-box-button` for its box styles.
    - The actor needs `safeForUntrustedWebProcess`: the parent only displays the numbers, through Fluent, and counts sizes itself.
    - The parent sees `decodedBodySize` as 0, because decompression happens in content. So the panel shows bytes on the wire and how many requests came from the cache.
    - A page served from cache has no TTFB ("Not available").
  - **Tests:** `browser/components/pagetelemetry/tests/browser` passes (10/10; it needs a fresh URL per load to avoid the cache), and the trust panel suite still passes (498).
  - **Values with Maximum protection:** the Navigation and Paint Timing figures (TTFB, FCP, TLS) go through the web APIs, so they are rounded there. The other figures come from Gecko directly.

### Phase 4 additions (user request 2026-10-23; spec D21 to D24)
Order: P4.2c, then P4.5 to P4.9, then P4.3 and P4.4 (profile the final code). PGO is off from P4.5 to P4.8 (D24).
- **P4.5 Upstream merge:** apply upstream `034cd8a8..main` (364 commits on 2026-10-23) before deleting anything, while it still applies cleanly. Preparation (2026-10-23):
  - **Source:** the shallow upstream clone is `../m6e-upstream.git` (base `034cd8a8`, upstream `main` at `cfa0508a3b`). The diff is `../m6e-upstream-diff/upstream.diff`: 1,576 files, +43,780/−32,916 lines.
  - **Dry run** (`git apply --check`):
    - 274 changes are to files M6E deleted. Exclude them and keep the deletions.
    - Only 3 conflicts, all in `browser/components/preferences/tests/etp/`. Upstream deletes `browser-srd.toml` and `browser_contentblocking_standard_tcp_section.js`, both of which P3.5 modified. The repo has no upstream history (snapshot import), so it comes in as a diff. Resolve conflicts with M6E's deletions by keeping the deletion. Then rerun the P5.3 maintenance scripts.
- **P4.5 results:**
  - **Merge:** upstream `034cd8a8..cfa0508a3b` was applied as a three-way diff (work-in-progress commit `53d399f2c4`): 1,301 files, of which 61 were added, 88 deleted, 1,130 modified and 11 renamed.
    - Left out: changes to the 274 files M6E deleted, and 12 new files in directories M6E removed. A new file is left out only if its nearest ancestor that existed upstream is gone here.
    - The one conflict, `preferences/tests/etp/browser.toml`, now has upstream's list plus M6E's two tests. Upstream removed `browser-srd.toml`, because the settings redesign is now the default.
  - **Checks:**
    - The merge bumped `CLOBBER` (libwebrtc), so a clean build: 16.5 minutes with LTO.
    - New pref `network.http.akamai_waf_telemetry.enabled` is locked off.
    - The new onboarding images on Mozilla's CDN are only used by ASRouter's test provider. The new Horoscopes widget was already locked off.
    - `scan-strings.py` finds 0 strings, and the search configuration is unchanged.
  - **Network:** only the 8 allowed hosts while idle, with `security.cloudflare-dns.com` as the DoH resolver (D25).
  - **Tests** (2,325 + 498 trust-panel passes): ETP settings, HTTPS-First, HTTPS-Only, Reader View, Remote Settings and the trust panel.
    - Two Remote Settings tests synced collections outside the D2 allowlist. They now stub `Utils.isAllowedCollection`. The new `test_m6e_allowed_collections.js` checks what is allowed and what is blocked, and that blocked collections never fetch.
    - Three failures are environmental:
      - the network's DNS resolves nonexistent hosts (`https-only/browser_console_logging.js` and `websocket-unexpected` in `test_resource_upgrade.html`);
      - the symlinked `_tests` path in `browser_localfile_readerMode.js`. Reader View does open the local file.
- **P4.6a results:** 141 platform-only directories deleted (2,195 files, about 445,000 lines), and the build passes.
  - **First pass:** it also deleted `gfx/skia/skia/src/utils/win`, which Skia compiles everywhere and empties with its own `#if`s. The used-file list had missed it: dependency files record sources pulled in by unified builds as paths relative to the object directory, so the list must resolve those too (fixed; 24,315 C files). The directory is restored.
  - **Validation:** the build backend checks that every source in an evaluated condition exists, so a wrong deletion fails at `config.status`.
- **P4.6b results:** `tools/m6e/platform/strip_mozbuild.py` removed 702 Windows, Linux and Android conditions from 286 first-party `moz.build` files, and `ruff-format` tidied them.
  - **Proof:** `tools/m6e/platform/compare_backends.py` compares the build backend generated before and after (a `git worktree` of the previous commit plus `./mach configure`). Result: 5,856 backend files, 0 differences. So the macOS build compiles exactly the same sources with the same defines and flags.
  - **Bug caught on the way:** the first version resolved a dead `elif` on its own and pulled its `else` body out of a chain that started with an undecided condition (the build caught this in `tools/profiler`). A decided `elif` is now dropped from its chain or becomes the chain's `else`.
  - **Not changed:** 6 files whose rewrite wouldn't parse, and vendored directories (D21).
- **P4.6c results:** `strip_ifdefs.py` ran `unifdef` over 1,403 first-party C, C++ and Objective-C files and removed about 62,000 lines of `XP_WIN`, `MOZ_WIDGET_GTK`, `ANDROID`, `XP_LINUX`, BSD and other branches. 73 files it can't parse are unchanged.
  - **Proof:** the symbol tables of all 22 shipped binaries (`nm -j`) are identical before and after, including XUL's 472,819 symbols.
  - **wasm sandbox:** 15 files are also compiled for the RLBox wasm sandbox (`wasm32-wasi`), where `XP_DARWIN`, `XP_UNIX` and `__APPLE__` aren't defined. They are stripped with `--undefine-only`; `mozalloc.cpp` had lost its non-Darwin fallback. The wasm targets' dependency files are the `*.wasm*.pp` files in `obj-m6e`.
  - **Cargo:** cargo needs every target's crate manifests to resolve the workspace, so `toolkit/crashreporter/rust_minidump_writer_linux` (deleted in P4.6a) is restored. P4.6a's build had passed only because Rust wasn't rebuilt.
- **P4.6d results:** `strip_ifdefs.py --frontend` stripped the platform branches from the 19 preprocessed front-end files (`firefox.js`, `AppConstants.sys.mjs`, 14 XHTML files, 2 manifests and `.lldbinit.in`).
  - **No longer preprocessed:** 5 XHTML files had no directives left, and Mozilla's preprocessor rejects those, so their `jar.mn` `*` markers are removed. Two of them had `#` licence comments that only the preprocessor removed; they are now XML comments.
  - **Proof:**
    - The preprocessed outputs in `dist/` are identical apart from `//@line` markers.
    - The other three now-unprocessed files match their old output byte for byte.
    - All six changed dialogs and pages open in the browser exactly as on the P4.1 baseline.
    - `AppConstants.platform` is still `macosx`.
- **P4.6 summary and limits:** Windows, Linux and Android code is gone from the first-party C, C++, Objective-C and preprocessed front-end code, from `moz.build`, and wherever whole directories could go. What remains:
  - vendored code (D21);
  - Rust `cfg(target_os = ...)` code;
  - runtime checks such as `AppConstants.platform == "win"` in JavaScript;
  - test manifests' `skip-if` conditions;
  - CI configuration (`taskcluster/`, `testing/mozharness`);
  - the 73 C files and 6 `moz.build` files the tools can't parse.

  None of these is compiled or run on macOS.
- **P4.6 Platform removal (D21):**
  - **a:** delete platform-only files and directories (first-party and vendored), checking for references first.
  - **b:** remove WINNT, Linux, GTK and Android branches from `moz.build`, `moz.configure`, makefiles and mozconfigs.
  - **c:** `unifdef` first-party C, C++ and Objective-C for `XP_WIN`, `XP_LINUX`, `MOZ_WIDGET_GTK`, `MOZ_WIDGET_ANDROID`, `ANDROID`, `MOZ_WAYLAND`, `MOZ_X11`, `XP_IOS` and the BSDs and Solaris. Keep `XP_UNIX`, `XP_DARWIN` and `XP_MACOSX`.
  - **d:** the same for preprocessed front-end files (`#ifdef` in JS, CSS and XHTML).
  - Build and test after each part.
- **P4.7a results:** 20 platform-only subtrees (193 files) are deleted. After P4.6b no build file reaches them, and the macOS build reads none of their files:
  - Windows COM, MSAA and IA2 accessibility, the maintenance service, Windows media-foundation EME, the about:third-party page, installer prefs, the installation-dir layout, the private-browsing proxy and the Windows push command-line handler;
  - Android's linker, MediaDrm EME, HLS and themes;
  - Linux's interposers, glxtest, speech-dispatcher and no-PIE launcher;
  - the Linux crash injector.

  Cross-platform HLS telemetry in `DecoderTraits.cpp` goes with them (`MOZ_ANDROID_HLS_SUPPORT` is now on the strip list). `push_check.py` passes. Crash-reporter crates, packaging scripts, fuzzing targets and PHC stay (crates are needed by cargo; the rest is tooling).
- **P4.7b results (Normandy):** `toolkit/components/normandy` (65 files) and the unregistered `about:studies` (`browser/components/nimbus`) are deleted.
  - Nimbus and `ClientEnvironment` use `Services.uuid` instead of `NormandyUtils`. The legacy Heartbeat Nimbus feature, Normandy's startup hooks and prefs, its ESLint config and the policy test that imported it are gone.
  - **Glean:** Nimbus still records enrolment under Glean's `normandy` category, so those metric definitions moved to `toolkit/components/nimbus/metrics-normandy.yaml`. Deleting them made Nimbus throw a `TypeError` when recording an enrolment, which the Nimbus tests caught.
  - **Tests:** 12 Nimbus xpcshell tests fail, and they fail identically on the tree before P4.7b (M6E's Nimbus lockdown). P4.9 adapts them.
- **P4.7 Dormant features (D23):** for Sync, Glean telemetry, Normandy, AI/ML, Translations, VPN (IP protection), the crash reporter, the updater, the default browser agent and others: try deleting each, keeping a deletion only if the build and tests pass. Show that whatever stays is unreachable.
- **P4.8 Security hardening (D22):**
  - **a:** attack-surface reduction prefs and build options, including Nightly-only experimental features.
  - **b:** compiler and runtime hardening (stack protector, `_FORTIFY_SOURCE`, libc++ hardening, automatic variable initialisation, allocator hardening).
  - **c:** sandbox and process isolation checks (content, GPU, RDD, socket and utility processes, Fission, and environment-variable overrides).
  - **d:** review every M6E commit.
  - **e:** static analysis (clang-tidy and analyzer security checks) of first-party code that parses untrusted input or handles IPC, with a manual review of what it finds.
  - **f:** extensions, protocol handlers and local-network access.
  - Fix what is found. Record every finding and fix here.
- **P4.8d results** (an independent review of every M6E change; all fixes built and tested):
  1. **Medium:** a Shield switch could leave per-site overrides from the other state behind, so the Tracker Blocker showed "protected" while the engine still exempted the site.
     - **Fix:** `setSiteProtected` clears all four override types first, and `isSiteProtected` follows the C++ order.
     - **Test:** `test_site_override_after_shield_switch`.
  2. **Low:** with the Shield off, HTTPS-Only exempted every site, so even Firefox's HTTPS-First was lost.
     - **Fix:** the Shield-off value of `dom.security.https_only_mode` is now false, giving HTTPS-First everywhere. `PotentiallyDowngradeHttpsFirstRequest` refuses the plain http fallback for `m6e-protect` sites (after the in-progress flag is set). The old exemption in `TestIfPrincipalIsExempt` is gone.
     - **Test:** `dom/security/test/https-first/browser_m6e_shield_off_no_downgrade.js`.
     - **Limit:** for protected sites with the Shield off, subresources get HTTPS-First's mixed-content upgrading, not HTTPS-Only's.
  3. **Low:** the console paste guard was off (`devtools.selfxss.count` 5, copied from Nightly branding). It is now 0.
  4. **Low:** content relevancy's Rust Remote Settings client bypasses the D2 allowlist. `toolkit.contentRelevancy.enabled` is now locked off; P4.7 removes the feature.
  5. **Info:** Reader View cloned the page inside the live document, which re-ran page custom elements and image loads. Ad slots are now stripped from a copy imported into an inert document.
  6. **For the user to decide (P4.8a):**
     - Safe Browsing is off, so there is no malware, phishing or download protection.
     - Add-on updates are off, so installed add-ons never get security fixes.
     - Turning a site off with the Shield on also allows plain http for it (D20).
     - The RUSTSEC ignores in `.cargo/audit.toml` need re-checking.
  - **a results:** `m6e.js` gives Firefox Release's values to the Nightly-only static prefs and to the Nightly blocks in `all.js` and `firefox.js`.
    - **Correction (P4.9):** the first version also turned off WebCodecs (`dom.media.webcodecs.enabled`), which is Nightly-only on Android but on by default in desktop Release, and two prefs that are already off on macOS (`dom.webshare.enabled`, `network.proxy.detect_system_proxy_changes`). It had read the first `value:` of entries with `#if` blocks inside them. Those three lines are gone, and the list was rechecked against the YAML with the platform conditionals resolved for macOS.
    - **Tests:** manifests for the features now off set their Nightly values again: `dom/tests/mochitest/general`, `dom/workers/test`, `dom/serviceworkers/test`, `dom/base/test/browser.toml` (Speculation Rules), `widget/tests/chrome.toml` (clipboard), and three WPT Speculation Rules metadata files.
    - **Now off:** Speculation Rules, streaming fetch uploads, the worker remote debugger, Typed OM, compression dictionaries, Reporting API crash reports, experimental RegExp and WebCrypto features, the new clipboard formats, the experimental BiDi features, and Nightly's parallel second SDP parser.
    - **Nightly values kept:** opaque response blocking, COEP credentialless, schemeful SameSite, DOM-clobbering protection, unused-permission expiry, content-script resource isolation, no `Components` shim, no inline handlers in SVG `<use>`, the MathML and `dragexit` removals, and visible control characters.
    - **Locked:** the SharedArrayBuffer COOP/COEP bypass stays off (as in Release), and the devtools remote debugging server is locked off.
    - **New:** Local Network Access also blocks trackers (`network.lna.block_trackers`).
    - **Not locked:** the experimental features stay unlocked, so a user can opt in through about:config.
    - **Checked in the running build.** Mochitests for these features that run only on Nightly need their prefs set again (P4.9).
    - **Compatibility:** Google, Wikipedia, GitHub, BBC and YouTube load with no navigation errors, and idle traffic is still the 8 allowed hosts.
  - **b results:**
    - **Added:** `-ftrivial-auto-var-init=zero` for C and C++ (`mozconfig` `CFLAGS`/`CXXFLAGS`), so uninitialised locals read as zero instead of leaking memory. Mozilla only uses this in debug builds; Chrome ships it. It takes effect with P4.9's final build, which measures its cost.
    - **Already in place:** `_FORTIFY_SOURCE=2`, `-fstack-protector-strong`, `-fstrict-flex-arrays=1`, libc++ *extensive* hardening, and PHC (`MOZ_PHC=1`).
    - **Not used:**
      - `_FORTIFY_SOURCE=3`: the macOS SDK caps the level at 2 (`secure/_common.h`).
      - `-fstack-clash-protection`: configure excludes macOS.
      - Branch protection (PAC/BTI): it only protects arm64e processes, which third-party apps can't ship.
  - **c results:**
    - **State:** content sandbox level 3 (the macOS maximum), Fission on, networking in the sandboxed socket process, media in RDD, and utility processes.
    - **Locked:** `security.sandbox.content.level` (3) and `fission.autostart` (true), so `about:config` or a `user.js` can't lower them.
    - **Left alone:** the `MOZ_DISABLE_*_SANDBOX` environment variables. Setting them already requires code running on the Mac, so they add no remote risk.
  - **g:** prove the features marked for deletion (D30) unreachable: locked prefs, no listening sockets, no network traffic, and no entry points from web content or IPC.
  - **e results:** Homebrew LLVM 23's `clang-tidy`, the same LLVM as the compiler, run with `tools/m6e/security-tidy.sh` on 39 first-party files that handle untrusted input or IPC.
    - **Checks:** the Clang Static Analyzer's core, C++, security and unix checks, plus bug-prone use-after-move, dangling handles, `sizeof` misuse, string construction, `memset` and memory-manipulation misuse, signed `char` misuse and widening casts.
    - **Files:**
      - M6E's own C++ changes;
      - HTTP response, header, chunked and authentication parsing, Http2Session and HttpChannelParent;
      - URL parsing, MIME parameters, and the multipart and compression converters;
      - the CSP parser;
      - the BMP, ICO, GIF, PNG, JPEG, WebP and AVIF decoders;
      - the WAV, FLAC, Ogg, WebM, MP3 and ADTS demuxers;
      - font parsing (`gfxFontUtils`);
      - ContentParent, BrowserParent and MessageChannel;
      - the JS frontend and the script loader.
    - **Result:** no findings in any of the 39 files themselves. 34 distinct findings in included first-party headers were triaged by hand, and none is exploitable:
      - moved-from `UniquePtr`s that are only logged;
      - ICO size products bounded by the 256×256 format limit;
      - a deliberate byte view of a PNG row buffer;
      - ASCII-only signedness comparisons in EXIF and the text converter;
      - `rand()` only in Ogg serial numbers and the test-only chaos mode;
      - deliberate perfect forwarding and `sizeof` patterns.
    - Mozilla runs these analyzers in its own CI, so few findings were expected.
  - **f results:**
    - **Add-ons:** signatures are required. Add-ons load only from the profile and the application (`extensions.enabledScopes` 5), so other software can't side-load them. None of these is locked: test profiles must install the unsigned harness add-ons (a lock made every mochitest abort with `ERROR_SIGNEDSTATE_REQUIRED`), and changing them needs local write access, which could change the app itself anyway.
    - **Correction (P4.9):** the scope prefs are no longer locked. With `autoDisableScopes` locked, the test profiles' value of 0 had no effect, so the harness add-ons stayed disabled. That hung PGO's profiling run for hours (its quitter add-on never ran) and would have broken mochitests too.
    - **Downloads:** M6E asks before handling a new file type, and downloads stay out of macOS's Recent Items.
    - **Already in place:** link prefetch, DNS prefetch and speculative connections off (P3.4), a warning before external protocol handlers, and Local Network Access with tracker blocking (P4.8a).
  - **g results:**
    - **Prefs:** the VPN, Translations, Mozilla accounts, ML/AI, telemetry, studies and content relevancy are off and locked, and so are Nimbus rollouts now.
    - **Not built:** the updater service and the crash reporter.
    - **Pages:** web pages could link to eight dormant-feature pages: `about:translations`, `about:aichatcontent`, `about:smartpage`, `about:smartwindowtasks`, `about:smartformfillreview`, the ASRouter tools `about:asrouter` and `about:messagepreview`, and `about:referrals`, which redirected to firefox.com. All eight are unregistered (`AboutRedirector.cpp`, `nsAboutRedirector.cpp` and the two `components.conf` files).
    - **Sockets:** a normal run on a fresh profile listens on no TCP port. Its three UDP sockets are HTTP/3 client connections; with HTTP/3 off there are none.
    - **Network:** idle traffic is the 8 allowed hosts (P4.8a).
- **P4.9 Full test pass:** build, xpcshell and mochitest suites for the changed areas, `push_check.py`, `measure.py`, `speedometer.py`, `scan-strings.py` and the Phase 3 feature checks. Then the final PGO build, measure, and push.

### Phase 5: Packaging and acceptance (done 2026-10-24; results above)
- **P5.1 Package:** run `./mach package` for `M6E.app` and the `.dmg`.
  - **Signing:** sign ad hoc (D8) with `codesign --force --deep -s -`.
  - **DMG layout:** regenerate the DMG `dsstore` layout file, which was copied unchanged from Nightly's branding.
  - **Check:** the DMG shows the M6E artwork and the app launches from `/Applications`.
- **P5.2 Acceptance, on a fresh profile of the packaged app:**
  - **Network:** only the 8 allowed hosts while idle, with `security.cloudflare-dns.com` as the DoH resolver (D25).
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

### Phase 5 results (2026-10-24)

**P5.0 Malware and phishing protection by DNS (D25).**
- **Change:** `m6e.js` sets `network.trr.default_provider_uri` to `https://security.cloudflare-dns.com/dns-query`, and locks `network.trr.hard_fail_on_extended_error` (true) and `network.trr.allow-rfc1918` (false). `doh-providers.json` names the default "Cloudflare for Families (security)".
- **Two C++ changes were needed in `DNSPacket.cpp`:** `hardFail()` lists the extended errors 15 and 16; and `DecodeInternal` skips an all-zero A or AAAA answer. My first version had only the first one, and it never ran: `DOHresp::Add` rejects `0.0.0.0`, and the decoder returned that error before it read the extended error. A review agent proved this by running the real resolver against the build.
- **Results on the final binary:** the blocked test domains fail with `NS_ERROR_UNKNOWN_HOST` and no addresses, with no native lookup (acceptance check 8: 0 native lookups, 4 DoH lookups each). The previous build, pointed at the same resolver, resolved both through native DNS (`162.159.36.12`). The new xpcshell test `netwerk/test/unit/test_trr_blocked_ede.js` and 13 existing TRR tests pass.
- **Limits:** blocking works per domain, not per URL; downloads aren't checked; mode 2 falls back to unfiltered DNS if DoH itself is unreachable.

**P5.1 Package, sign, DMG.** One command, `tools/m6e/package/release.sh`, does it all: `./mach package`, copy the app out, `sign-adhoc.sh`, `make-dmg-layout.py build`, `verify --require-signed`, then `check-signed.js`. The output is `artifacts/release/M6E-161.0a1-macos-arm64.dmg` (116.6 MB). It is git-ignored; upload it to a GitHub release.
- **Signing is required after packaging.** Packaging strips and moves files, so the build's own signatures no longer match (`codesign --verify`: "code has no resources but signature indicates they must be present"), and Apple Silicon refuses to run an invalidly signed binary.
- **How it signs (D8, ad hoc):**
  - nested code first, the bundle last, no `--deep`, hardened runtime on (A4);
  - Mozilla's release entitlements minus the keys that need an Apple provisioning profile (application-identifier, web-browser.public-key-credential, application-groups);
  - `disable-library-validation` on every helper, because ad-hoc code has no Team ID and macOS would refuse to load the app's own libraries.
- **Verified:** `codesign --verify --deep --strict` is valid. `check-signed.js` passes 13 of 13 with a real window: JIT in the parent and in a content process, native WebAssembly, content sandbox level 3 (a content process can't read the home folder or `/etc`, write `/tmp` or spawn a process), and the socket, RDD and GPU processes all start. Without `allow-jit` the app dies at startup (exit 139), and one changed byte in a helper makes macOS kill it (exit 137). The app installed from the DMG verifies and launches.
- **Info.plist (A9):** `finalize-app.sh` removes `SMPrivilegedExecutables` (Mozilla's helper identities and Team ID) and `MozillaDeveloperRepoPath`/`MozillaDeveloperObjPath` (absolute paths into the checkout, which revealed the username) from every Info.plist in the packaged app before signing. All 6 plists in the release DMG's app are clean. Do not remove the keys from the `Info.plist.in` templates: the developer build needs them (see A9).
- **DMG:** the layout (680x460, "Install M6E", an arrow, the app on the left and the Applications link on the right) is written by `tools/m6e/branding/make-dmg-layout.py`, without Finder. 25 checks pass, including the signature of the app on the volume.
- **Seen in Finder (2026-10-24):** the window opens at 680x460 with the "Install M6E" title and subtitle, the green arrow, the M6E icon on the left, the Applications link on the right and the path bar. No Firefox or Nightly text. The icons are about 80 px rather than the intended 128 px: cosmetic, left as is (the A/B steps in `make-dmg-layout.py`'s docstring cover it).
- **Installed and launched (2026-10-24):** the app copied from the mounted DMG into the real `/Applications` verifies non-strict (valid on disk, satisfies its Designated Requirement) and opens through LaunchServices with the M6E menu bar and the New Tab page. `spctl --assess` rejects it, as expected for an ad-hoc identity (D8); the strict check fails only on the `FinderInfo` attribute below. The Gatekeeper dialog itself still needs a second Mac with a real download.
- **Known limits:**
  - `hdiutil` puts a `com.apple.FinderInfo` attribute on every file (Mozilla's DMGs too), so `codesign --verify --strict` fails on the mounted app or a plain copy. The non-strict check passes and macOS launches the app. Strict passes after `xattr -cr`.
  - The ad-hoc identity is the code hash, so macOS may ask again for camera, microphone and location after each update.
  - Platform passkeys (Touch ID, iCloud Keychain) need a provisioning profile and are unavailable.

**Installing M6E (text for the release notes).** M6E is signed ad hoc, not with an Apple Developer ID, and is not notarized, so macOS treats a download as coming from an unidentified developer the first time you open it. You do this once:
1. Open `M6E.dmg` and drag M6E onto Applications.
2. Open M6E from Applications. macOS says it cannot verify the app ("M6E Not Opened"). Click **Done**, not Move to Trash.
3. Open **System Settings, Privacy & Security**, scroll to **Security**, and click **Open Anyway** next to the message about M6E. Enter your password or use Touch ID, then **Open**. The button is available for about an hour after step 2.
4. After that M6E opens normally. In Terminal, `xattr -dr com.apple.quarantine /Applications/M6E.app` does the same.
- Control-click, Open no longer bypasses the check on macOS 15 and later.
- "M6E is damaged and can't be opened" means the app changed after signing or the download is incomplete: download it again.
- **Notarization** would remove all of this. It needs the Apple Developer Program (US$99 a year), a Developer ID Application certificate, the hardened runtime and entitlements (already in place), `notarytool` and stapling. The user decides; nothing here blocks it. I could not test `notarytool` (no account; this network's TLS inspection may also break its upload).

**P5.2 Acceptance (`tools/m6e/acceptance.py`) on the signed app: 15 of 15 checks pass,** run from `/Users/Shared` on a quiet machine (load 2.9):

| Check | Result |
|---|---|
| App bundle | identifier `org.m6e.browser`, 21 Mach-O files all arm64, codesign valid, no Firefox or Mozilla in user-visible plist strings |
| Branding | `scan-strings.py` 0; About dialog and `about:license` state the attribution |
| Network (300 s idle) | 8 allowed hosts; DoH goes to `security.cloudflare-dns.com`; no UDP sockets, nothing listens on a TCP port |
| Theme | browser window and five `about:` pages stay light with Dark Mode simulated; web content sees dark |
| Search | exactly Google (default) and Bing; adding engines blocked |
| Removed features | 29 items absent |
| Phase 3 | bookmarks toggle, Reader View rules, Privacy Shield, Tracker Blocker, Web Push and the Telemetry button (13 items) |
| D25 | both blocked domains show the "server not found" page with no native lookup; a normal site loads |
| Hardening | content sandbox level 3 locked and in effect; Fission on and locked |
| Performance | first paint **579 ms** (Phase 0: 785; Phase 4: 649); blank-tab memory **443 MB** (Phase 0: 698; Phase 4: 486). Five sites (informational, noise over 100 MB): 1,720 MB, no navigation errors |

- **A real defect the run found and I fixed:** the **PDF viewer followed macOS Dark Mode**, against Phase 1's "always light" (A11). pdf.js runs as web content, so the interface rule didn't reach it. `pdfjs.viewerCssTheme` is now 1 and locked.
- **Regression suites on the final build** (`tools/m6e/run_tests.sh`):
  - **Pass:** Privacy Shield, ETP and HTTPS-First mochitests 1,323 pass, 0 fail; Remote Settings and the D25 tests; Nimbus 33 of 33; HTTPS-Only 8 pass; Reader View 166 pass; the new D25 xpcshell test and 13 existing DoH tests.
  - **Environmental failures:** this network's DNS answers even nonexistent hosts (`browser_console_logging.js`, `websocket-unexpected`), and the symlinked `_tests` path (`browser_localfile_readerMode.js`).
  - **By design:** toolkit modules (fixed Widevine and OpenH264 URLs, D10; the region lookup that phoned a Mozilla service is gone, so `test_Region.js` and `test_Region_geocoding.js` fail with a null region) and enterprise policies (no updater, no added search engines, DoH on).
  - **Window-based suites (rerun 2026-10-24, screen unlocked):** `browser/components/pagetelemetry` and `browser/components/urlbar/tests/browser-trustPanel` timed out while the screen was locked. Rerun on the final build: 17 tests, 572 checks, 0 unexpected (10 and 498 passes, 47 expected todos). Log: `artifacts/p5-window-tests.log`.
- **Still manual:** real macOS Dark Mode (the script can only simulate it) for menus, dialogs, a context menu, the About dialog and a PDF; the Gatekeeper dialog on a second Mac with a real download.
- **The YouTube hang I chased (A14):** the first acceptance run, from `/tmp`, reported YouTube timing out in the five-site test. I found an intermittent page-load hang, **in every build**, for app copies under `/tmp`: the unsigned new build 0 to 3 of 5 passes, the previous build 3 of 5 in the same place, against 36 of 37 passes in the repository, the home folder, `/Users/Shared` and the DMG-installed copy. Signing, the hardened runtime, DoH, the content sandbox, HTTP/3 and the D25 change were each ruled out by experiment. With the document at `readyState: interactive` and no HTTP request pending, the hang is not DNS or TLS. The root cause is unknown; it needs a Gecko-level look. `acceptance.py` now warns when the app is under `/tmp`.

**P5.3 Maintenance (`tools/m6e/merge/`).** Upstream merges are now repeatable; each tool's docstring has the details. Steps:
1. Start from a clean committed tree.
2. Fetch (`git -C ../m6e-upstream.git fetch --depth 1 upstream main`; incremental, a few MB).
3. `git diff --binary --full-index --no-renames <base> <new>` into a directory per merge. It counts file diffs; git's default diffstat counts 1,576 files for P4.5 because 11 are renames, while the diff has 1,587.
4. `filter-upstream-diff.py` drops changes to files and directories M6E deleted (P4.5: 1,301 kept, 274 deleted by M6E, 12 new in removed directories).
5. `git apply --3way` (with `GIT_ALTERNATE_OBJECT_DIRECTORIES` for the upstream objects), then resolve the conflicts, keeping M6E's deletions.
6. `post-merge-checks.sh --upstream-new <sha>`: 17 read-only checks (conflicts, search config, strings, fox artwork, Remote Settings allowlist, `gmp-sources`, DoH default, tokens import, platform code that came back, lint paths, new Nightly-only prefs, new linkable `about:` pages, new external URLs, `CLOBBER`). A check that can't run reports "needs attention", never "ok".
7. After the merge, record the merged upstream commit in `tools/m6e/merge/upstream-base.txt`.
- **Build after a merge:** `./mach clobber`, then `./mach build` (PGO).
- **Known baselines:** `nightly-prefs-reviewed.txt` (41 Nightly values kept on purpose, each with a reason; skim it); 324 stale lint-config entries (cleanup can go into P6.1); 22 front-end files with non-macOS `#if` lines.

**Memory investigation.** The rise from 449 MB (P4.1) to 472 and 486 MB was **not reproduced**: the same P4.2b binary measured 449, 472, 424 and 434 MB in different sessions. A single `measure.py` launch is noisy by about 30 MB, mostly from the GPU process, so compare the "all but GPU" total, which varies by 2 to 3 MB. The real growth in the parent is 0 to 4 MB (about 1%) with no mechanism found. The final acceptance runs measured 425 to 443 MB. Optional saving (not applied, A8): `browser.newtab.preload=false` saves about 8 MB but makes the first New Tab slower. The `jSOracle` and RDD utility processes (18 MB each) stay. **Open:** in the memory test, the P4.9 build showed 17 or 18 processes with five sites in 4 of 9 runs, other builds 15; the cause is unknown (ad frames are a guess).

### Decisions taken while the user was away (2026-10-24)
Each is the recommended option. The user reviews these in the Summary of Completion; any can be reversed.

| # | Decision | Alternatives | Why this one |
|---|---|---|---|
| A1 | Proceed through Phase 5 without asking questions | Pause for each choice | The user asked for recommended decisions while away |
| A2 | Reset the build output with `./mach clobber` and rebuild all three PGO stages, after the first Phase 5 build failed at the final ThinLTO link (`ProfileSummary: IDs have conflicting values`) | Delete only the final-stage object files (blocked by the permission classifier as bulk deletion; also hackier); keep the old build and skip the fix | The profile run regenerates the profile data, and make doesn't track it as a dependency, so a partial rebuild mixes objects built against two profiles. A clean build is the only deterministic fix. It costs about 80 minutes of unattended time. |
| A3 | Apply the D25 DNS fix in `DNSPacket.cpp` as the review agent proposed (skip all-zero A and AAAA answers), instead of keeping my first version | Keep the first version | I verified in the source that the first version never ran: `DOHresp::Add` rejects `0.0.0.0`, and `DecodeInternal` returned that error before reading the extended DNS error. |
| A4 | Sign with the hardened runtime ON (`--options runtime`), `--no-runtime` kept as an option | Hardened runtime off | It matches how Mozilla signs; it blocks `DYLD_*` injection and debugger attachment. Both modes launch and run (tested by the signing agent). Cost: a new helper that needs JIT must get an entitlement in the script. |
| A5 | Lock `network.trr.hard_fail_on_extended_error` to true in `m6e.js` | Leave it unlocked | It is what makes D25's blocking hold; a policy or about:config tweak would silently turn the protection off. Users can still pick another resolver in Settings (D11). |
| A6 | Leave the `network.trr.default_provider_uri` default in `StaticPrefList.yaml` as Mozilla's endpoint; `m6e.js` overrides it at runtime | Edit the YAML | The YAML edit rebuilds a large part of the tree for no behavioural change; the live value was verified on the new build. |
| A7 | DMG background is a plain designed image (gradient, "Install M6E", arrow); no pictorial artwork | Add the rickshaw or splash artwork | The icon already shows as the app icon; the home-page splash is a busy cartoon with embedded text. Easy to change later with `dmg-background.swift`. |
| A8 | No memory-saving prefs applied (the 449 to 486 MB blank-tab rise was not reproduced; real parent growth 0 to 4 MB) | `browser.newtab.preload=false` saves about 8 MB but slows the first New Tab | The memory investigation found measurement noise of about 30 MB (GPU process); the build is far below the Phase 0 baseline of 698 MB. |
| A9 | Strip `MozillaDeveloperObjPath`, `MozillaDeveloperRepoPath` and `SMPrivilegedExecutables` from the packaged app's Info.plist in `finalize-app.sh`, before signing; leave Mozilla's `Info.plist.in` templates unchanged | Edit the templates | The first two are absolute paths into this checkout (they reveal the username); the third names Mozilla's helper identities and Team ID, and the updater and its helper are not built. Editing the templates **broke the developer build**: in a non-packaged build the first content process launch reads `MozillaDeveloperRepoPath` from Info.plist (`nsMacUtilsImpl::GetRepoDir`) and crashes without it, so the mochitest harness could not start. The packaged app contains `omni.ja`, which makes it a packaged build that never reads the keys. |
| A10 | Test the install-from-DMG launch from a stand-in `Applications` folder, not the real `/Applications` | Copy into `/Applications` | Installing into the user's `/Applications` while they are away is a side effect they didn't ask for. They can drag-install the DMG in a minute. |
| A11 | Force the PDF viewer light with `pdfjs.viewerCssTheme=1` (locked) | Leave it following Dark Mode | Phase 1's requirement says the PDF viewer is always light; the acceptance run showed it wasn't. |
| A12 | Also lock `network.trr.allow-rfc1918` to false | Leave it unlocked | The D25 fix only skips the block answer while it is false; with it on, a blocked site shows "unable to connect" instead. It is the default, so nothing changes for users. |
| A13 | The release DMG is built by `make-dmg-layout.py build` from the signed app, named `M6E-<version>-macos-arm64.dmg`, kept in `artifacts/release/` (git-ignored) | Use the DMG that `./mach package` makes | That DMG holds the unsigned app and a different layout; DMGs don't belong in git. |
| A14 | Treat the YouTube hang as an environment effect of running copies under `/tmp` (the previous build behaves identically there); no product change, a warning in `acceptance.py` | Hold the release for a root cause | Experiments excluded signing, the hardened runtime, DoH, HTTP/3, the sandbox and the D25 change; the shipped artifact passes in realistic locations (36 of 37). Left open. |
| A15 | Accept the Finder look of the DMG as unverified, and flag it for the user | Keep trying to wake the display or script Finder | The display stayed locked all session, and Finder scripting triggered a macOS permission prompt on the user's screen; I stopped after that. |
| A16 | Keep ad-hoc signing (D8) rather than buy a Developer ID | Buy the Developer Program | Spending money is the user's call. The Gatekeeper first-open steps are documented. |

### Build notes learned in Phase 5
- **PGO and incremental builds don't mix.** Any C++ change reruns all three stages, and the profile run regenerates `obj-m6e/instrumented/merged.profdata`. The final stage then compiles only the changed files against the new profile, and the ThinLTO link fails with `ProfileSummary: IDs have conflicting values`. Fix: `./mach clobber` before the build. For front-end-only changes use `make -C obj-m6e/faster`, which needs no compile.
- **D25 needed two C++ changes.** `hardFail()` has to list the extended errors 15 and 16, and `DecodeInternal` has to skip the all-zero address, or it fails before it reads the error (see the A3 row above).

- **A developer build reads the repo path from its Info.plist.** `MozillaDeveloperRepoPath` and `MozillaDeveloperObjPath` are read through a macro (`MAC_DEV_REPO_KEY`), so a text search for the key name finds no reader. Removing them crashes the first content process launch in `obj-m6e/dist/M6E.app` (`MOZ_CRASH("Failed to get path to repo dir")` in `ContentParent.cpp`) and every mochitest with it. The packaged app (it has `omni.ja`) doesn't read them.

### Phase 6: Delete the dormant features (D30; started 2026-10-24)
- **Goal:** remove the dormant Mozilla features under D23. A feature goes only if the build and tests pass without it. Anything that has to stay is proven unreachable: locked prefs, no listening sockets, no network traffic, and no entry points from web content or IPC.
- **Build configuration:** the phase builds without LTO and PGO (marked `M6E:` in `mozconfig`), so a C++ change costs an incremental build of minutes instead of a clean PGO build of about 80 minutes. P6.3 restores both and rebuilds from clean.
- **How the work is split:**
  1. **Analyse, in parallel (read-only):** one analyst per feature maps everything to delete and every reference from code that stays. An independent critic then hunts for missed references with different search angles. Reports: `artifacts/p6/analysis/<key>.md`.
  2. **Prepare, in parallel (git worktrees):** one agent per feature makes the removal in its own worktree on the branch `m6e-p6/<key>`, without building. An independent reviewer checks each branch for leftover references, and the preparing agent fixes what it finds.
  3. **Integrate, one at a time (the shared `obj-m6e`):** cherry-pick a branch, build, run the feature's consumer suites and `tools/m6e/run_tests.sh`, and compare with the P6.0 baseline. Then commit `M6E P6.1x: …` and push. Builds and mochitests can't run in parallel: there is one objdir, and mochitests share port 8888.
- **Steps:**

| Step | What | Main paths |
|---|---|---|
| P6.0 | Phase 6 build configuration; baseline test results on the unchanged tree | `mozconfig`, `artifacts/p6/baseline` |
| P6.1a | The VPN (IP protection) and the `addons-search-detection` built-in extension | `browser/components/ipprotection`, `toolkit/components/ipprotection`, `browser/extensions/ipp-activator`, `browser/extensions/search-detection` |
| P6.1b | Translations | `toolkit/components/translations`, `browser/components/translations` |
| P6.1c | The generative AI interface and the AI window | `browser/components/genai`, `browser/components/aiwindow` |
| P6.1d | The ML engine and its remaining consumers | `toolkit/components/ml` |
| P6.1e | Sync | `services/sync` |
| P6.1f | Mozilla accounts | `services/fxaccounts` |
| P6.1g | The updater | `toolkit/mozapps/update` |
| P6.1h | The crash reporter, crates included | `toolkit/crashreporter`, `toolkit/components/crashes` |
| P6.1i | Nimbus experiments | `toolkit/components/nimbus` |
| P6.2 | Stale lint-config entries (the 324 known and any new ones); the unreachability proof for whatever stays | lint configs |
| P6.3 | LTO and PGO back on, a clean build, the full regression run, acceptance, measurements and the release DMG | |

- **Order:** features that others depend on go last. The AI interface goes before the ML engine, the VPN and Sync before Mozilla accounts, and Nimbus last, because nearly every feature reads it. The analysis may change the order.
- **Done when:** every feature is deleted or proven unreachable, the release build passes, the regression suites match the baseline, acceptance passes 15 of 15, and startup, memory and app size are measured against Phase 5.

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
