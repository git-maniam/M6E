# M6E Implementation Plan

How the spec (`m6e_browser_agent_instructions.md`) is built: the working method, what each finished step changed, and the plan for the remaining steps. Decision numbers (D1 to D19) refer to the spec's decision log.

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
| `branding/make-icons.sh` | Regenerates every branding image from `M6E-Icon.png` |
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
| P3.5 | Privacy Shield | see `git log --grep P3.5` |

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
    - `browser_bookmarks_toolbar_collapsed_restore_default.js` fails because M6E is never in the default customization state. `TabsToolbar` defaults still list the AI buttons `smartwindow-group-tabs-button` and `ai-window-toggle`, which P1.13 leaves unplaced, so Restore Defaults is always enabled.
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
  - **Not a bug:** the security warning card on the Privacy pane appears only in Marionette and test profiles. They turn off password saving and the popup blocker.

## 4. Next step: P3.6 Reliable push notifications

See P3.6 in section 5. After it comes P3.7, the Tracker Blocker button (D19).

**Follow-up for a later step:** drop the AI buttons from CustomizableUI's default placements (see P3.2).

## 5. Remaining steps

### P3.6 Reliable push notifications (D12)
- Notifications go through macOS Notification Center (`alerts.useSystemBackend`), and the macOS permission request appears on first use.
- M6E keeps running after its last window closes, the macOS default; confirm that.
- Optional and off by default: an "Open M6E at login, hidden" login item using `SMAppService`. It's native code, so agree its scope with the user first.
- **Check:** subscribe on a Web Push test page, close every window, send a push, and see a native notification. Repeat with the Privacy Shield on and off, and after a restart.

### P3.7 Tracker Blocker button (D19)
Build on Firefox's tracking protection icon (`#tracking-protection-icon-container` in the urlbar's identity box) and the protections panel (`browser/base/content/browser-siteProtections.js`, `#protections-popup`). Don't add a second widget.
- **Count badge:** add a number to the icon showing the items blocked on the current tab. Use the content-blocking log the panel already reads (`gBrowser.selectedBrowser.getContentBlockingLog()`, or the `onContentBlockingEvent` notifications), counting every blocked tracker, ad, social tracker, fingerprinter, cryptominer and cross-site cookie. Update it on each blocking event and on tab switches, and reset it on navigation.
- **States:**
  - **Enabled:** the active Tracker Blocker icon with the count.
  - **Disabled:** a distinct inactive icon with no count, when the site has a tracking protection exception or blocking is off.
  - Draw new SVG icons in `browser/themes/shared/icons/` in the M6E style.
- **Panel:**
  - List each category and the blocked domains (the panel's subviews already hold the per-category lists; show them inline or one click away).
  - Keep the toggle. It is Firefox's per-site ETP switch, relabelled "Tracker Blocker".
  - Drop the panel's Mozilla links, which have no support site (D7).
- **Strings:** "Tracker Blocker", and an accessible label that includes the count.
- **Check:** on a page with known trackers, the badge counts up and matches the panel's lists. Turning the toggle off shows the inactive icon with no number and reloads the page unblocked. Turning it on restores the count. Also check tab switches, private windows, and with the Privacy Shield off. Run `browser/base/content/test/protectionsUI`.

### Phase 4: Performance
- **P4.1 Measure:** repeat `measure.py` on the Phase 3 build and compare with the baselines.
- **P4.2 Build flags:** add one flag at a time and measure after each:
  - **LTO:** `--enable-lto=cross` needs a clang whose LLVM matches Rust's. Check Homebrew's `llvm` against `rustc --version --verbose`.
  - **PGO:** `MOZ_PGO=1`.
  - **CPU tuning:** `-mcpu=apple-m1` (C/C++) and `-C target-cpu=apple-m1` (Rust).
  - **Optimisation level:** an explicit `-O3`.

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
  - rerun `prune-search-config.py` and `scan-strings.py`;
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
- **Fresh sessions:** start each phase in a fresh session, with the spec, this plan, `CLAUDE.md` and the memory notes as the handoff.
- **Model choice:** Sonnet suits high-volume, build-driven steps. Opus suits design and profiling (Phases 3 and 4). Fable suits stubborn build-system or security-review problems.
