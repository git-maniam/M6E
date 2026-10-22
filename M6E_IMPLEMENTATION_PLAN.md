# M6E Implementation Plan

How the spec (`m6e_browser_agent_instructions.md`) is built: the working method, what each finished step changed, and the plan for the remaining steps. Decision numbers (D1 to D17) refer to the spec's decision log.

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

## 4. Next step: P3.1 search engines (D4, D5)

**Goal:** the only engines are Google (default everywhere) and Bing. Their URLs carry no Mozilla partner codes, and users can't add engines from any entry point.

**Facts checked:**
- **Bundled configuration:** `services/settings/dumps/main/search-config-v2.json` has 155 `engine` records, plus one record each for `defaultEngines` (global default `google`, with 10 region and locale overrides), `engineOrders` (2 orders) and `availableLocales`.
- **Config v3:** `search-config-v3.json` has the same shape and is used when `browser.search.configV3.featureGate` is on.
- **Partner codes:** Google's partner codes sit in its 11 `variants`. Bing has `partnerCode: MOZI` and `pc`/`form` search parameters.
- **Sync:** both configurations already stay on the bundled copy, because D2 blocks syncing them.

**Steps:**
1. **Prune script:** write `tools/m6e/search/prune-search-config.py`, a deterministic filter that can be rerun after every upstream merge. For both config dumps it:
   - keeps only the `google` and `bing` engine records;
   - sets `defaultEngines` to `google` globally, with no region overrides;
   - reduces `engineOrders` to Google then Bing;
   - removes Mozilla partner and attribution parameters (`client`, `channel`, `pc`, `form`, `partnerCode`) from every Google variant and from Bing;
   - prunes `search-config-icons.json` (and its attachment files) to the two engines' icons, and empties `search-default-override-allowlist.json`.
2. **Block user-added engines (D5):**
   - **Central refusal:** in `SearchService`, refuse to add any engine that doesn't come from the app configuration, whether from OpenSearch, a user, an extension or a policy. Mark the change `M6E:`. This is the same central-enforcement approach used for themes.
   - **UI:** hide the "Add search engine" entry points: the urlbar's "add engine" offers, the Settings search pane's Add button and its "find more search engines" link, and the search bar's context menu.
3. **Tests:** remove the region and engine tests in `toolkit/components/search/tests/xpcshell/searchconfigs/` that assert removed engines, and update the remaining search tests to the two-engine configuration.
4. **Build:** `./mach build faster` (data and JS only).
5. **Check in the running browser (`chrome_eval.py`):**
   - `SearchService.getVisibleEngines()` returns exactly `Google`, `Bing`, and the default is Google;
   - `getSubmission("test")` for each engine has no partner parameters;
   - adding an OpenSearch engine and a user engine is refused;
   - the Settings search pane lists two engines and shows no Add button;
   - a search from the urlbar opens a plain `www.google.com/search?q=…` URL.
6. **Network:** run `measure.py` and confirm no new hosts.
7. **Commit and push:** `M6E P3.1: Limit search to Google and Bing`.

**Risks:**
- Search tests are numerous. Removing tests for removed engines is expected; keep the remaining ones passing with `./mach test toolkit/components/search`.
- If `SearchService` validates the dump against a schema, keep the record shapes intact and only drop records.

## 5. Remaining steps

### P3.2 Bookmarks toolbar setting
- Keep the bookmark manager and browser import (`MOZ_PROFILE_MIGRATOR`).
- Add a "Show bookmarks toolbar" setting to the Settings General or Appearance pane, bound to `browser.toolbars.bookmarks.visibility` (`always`/`never`), in the redesigned config format (`browser/components/preferences/config/*.mjs`).
- **Check:** the setting toggles the toolbar both ways, and import from Safari and Chrome works.

### P3.3 Reader View
All changes go in `toolkit/components/reader` and `toolkit/themes/shared/aboutReader.css`. Don't edit the vendored `readability/` library; post-process its output in `AboutReader.sys.mjs`.
- **Trigger:** keep the reader button for detected articles, and add a View-menu item that forces Reader View on any page.
- **Clutter:** after Readability runs, remove every remaining `iframe`, `nav`, `aside`, form and ad-like element.
- **Typography:** base text 1.25 times the stock base, derived from how `reader.font_size` maps to pixels. The user can still resize.
- **Colours:** `#000000` on `#FFFFFF`. Remove the other colour schemes from `reader.color_scheme.values`, along with their controls, background images and page colours.
- **Images:** keep inline article `img` and `picture` elements. Drop tiny or tracking images (under about 48 px) and video embeds.
- **Check:** `./mach test toolkit/components/reader` passes, plus three real articles look right.

### P3.4 Privacy hardening defaults
- Add a curated Arkenfox subset to `m6e.js`, each pref with its reason:
  - Strict Enhanced Tracking Protection;
  - `privacy.resistFingerprinting`;
  - HTTPS-Only mode;
  - referrer trimming;
  - WebRTC IP-leak protection.
- Prefetching and speculative connections are already off (P1.14). These are defaults, not locks: P3.5 manages them.
- Don't disable Web Push, notifications or DRM (D10, D12). Check that fingerprinting resistance doesn't block or spoof the Notification and Push APIs; exempt them if it does.
- **Check:** the expected values on a fresh profile, a fingerprinting test page, and a five-site compatibility sample, with any breakage recorded.

### P3.5 Privacy Shield
- Add a "Privacy Shield" group at the top of the Settings Privacy pane (the config format), with a master toggle backed by `m6e.privacyShield.enabled`.
- A small module, for example `browser/modules/M6EPrivacyShield.sys.mjs`, applies the P3.4 strict set when the toggle is on. When it is off, it restores compatibility values (Standard tracking protection, fingerprinting resistance off). It respects prefs the user changed by hand.
- **Check:** the toggle flips the prefs both ways, the state survives a restart, and the Privacy-pane tests pass.

### P3.6 Reliable push notifications (D12)
- Notifications go through macOS Notification Center (`alerts.useSystemBackend`), and the macOS permission request appears on first use.
- M6E keeps running after its last window closes, the macOS default; confirm that.
- Optional and off by default: an "Open M6E at login, hidden" login item using `SMAppService`. It's native code, so agree its scope with the user first.
- **Check:** subscribe on a Web Push test page, close every window, send a push, and see a native notification. Repeat with the Privacy Shield on and off, and after a restart.

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
  - **Phase 3 features:** the bookmarks toggle, Reader View rules, the Privacy Shield and push all work.
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
