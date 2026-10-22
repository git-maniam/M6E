# M6E Implementation Plan

This plan turns `m6e_browser_agent_instructions.md` (the spec) into ordered, verifiable steps. The spec stays the source of truth for *what* M6E is. This file covers *how* and *in what order*. If the two conflict, follow the spec and update this plan.

## How to execute this plan

Every step follows the same loop:

1. Implement the step.
2. Build: `./mach build`. Use `./mach build faster` when only front-end files (JS, CSS, FTL, XHTML) changed.
3. Run the step's **Verify** checks.
4. Commit as `M6E P<phase>.<step>: <summary>`, then push to `origin/main`.

Do not start the next step until the current one builds and verifies. If a step balloons, split it into sub-steps (`P1.2a`, `P1.2b`) rather than committing a broken tree.

Redirect long command output to `artifacts/` (for example `./mach build > artifacts/build-P1.2.log 2>&1`) and read the file, rather than piping through `tail` or `grep`.

## Baseline facts (checked against this checkout)

The spec was written before the tree was inspected. These findings adjust how some spec items are carried out:

| Spec item | What the tree actually has | Consequence for the plan |
|---|---|---|
| Base version | Firefox `160.0a1`, a **Nightly** snapshot of upstream `mozilla-firefox/firefox` commit `034cd8a8207e38b04afec5e6f75e5161f75a112f` (2026-10-21), identified in Phase 0. No upstream VCS history in this repository. | Nightly enables nightly-only features and code paths (`NIGHTLY_BUILD`). See decision D9. |
| Pocket | Already removed upstream (`browser/components/pocket` does not exist). | P1.14 only checks for leftovers. |
| Firefox VPN | Lives in `browser/components/ipprotection` ("IP Protection"). | P1.9 targets that component. |
| Language stripping | Only `en-US` strings are in-tree. The other 114 locales in `browser/locales/all-locales` come from a separate l10n repository. `en-GB` UI strings are **not** in the tree. | Language stripping means switching off locale plumbing and language-switching UI, not deleting files. `en-GB` needs decision D1. |
| "Strip Telemetry modules" | Telemetry is Glean (`toolkit/components/glean`) plus legacy `toolkit/components/telemetry`. Nearly every component declares Glean metrics (`metrics.yaml`), so deleting the modules would break the build. | Compile out reporting, lock prefs off, blank endpoints, and remove the UI (P1.10). |
| Crashpad | Firefox's crash reporting is `toolkit/crashreporter` with a configure switch. | `--disable-crashreporter` (P1.11). |
| Sync, Normandy, health report | Forced on by `imply_option(...)` in `browser/moz.configure`, so a mozconfig can't turn them off. | Edit `browser/moz.configure` (P1.8, P1.10, P1.12). |
| Telemetry upload | `MOZ_TELEMETRY_REPORTING` defaults to `MOZILLA_OFFICIAL`, so it is off for our unofficial build. `MOZ_DATA_REPORTING` is set whenever telemetry, health report, crash reporter or Normandy is on. | Turning those four off removes `MOZ_DATA_REPORTING` too. |
| Search engines | Engine definitions are bundled Remote Settings dumps: `services/settings/dumps/main/search-config-v2.json` (155 records), with `search-config-v3.json` behind a feature gate in `SearchUtils.sys.mjs`. Google URLs carry Mozilla partner parameters (`client`, `channel`). | Edit the dumps, not code (P3.1). See decision D4. |
| Dark theme | 40 `prefers-color-scheme: dark` blocks in 30 chrome CSS files. Built-in themes in `browser/themes/addons`: `dark`, `light`, `alpenglow`, `aiwindow`, `aiwindow-nova`, `privatewindow`. | P1.6 forces light at the root and removes dark blocks and themes. |
| User-visible "Firefox"/"Mozilla" | About 630 literal lines in about 300 `en-US` files (`browser`, `toolkit`, `dom`, `devtools` locales), on top of 670 brand-term references that the branding directory handles. | A dedicated string pass (P2.4). |
| App identity | `browser/moz.configure` sets `MOZ_APP_VENDOR = "Mozilla"` and Firefox's `MOZ_APP_ID`. | New identity in P2.3, so M6E never shares a profile with an installed Firefox. |
| AI features | `browser/components/aiwindow`, `browser/components/genai`, `toolkit/components/ml`, `browser/components/translations`. | Not in the spec. See decision D3. |
| Root dotfiles | The initial import dropped every hidden file in the repository root, including `.cargo/config.toml.in`, which the build requires. | Restored in Phase 0 from upstream `034cd8a8`, together with upstream's `.gitignore`. CI, Phabricator, GitHub workflow and AI/IDE configs were deliberately not restored. |
| Toolchain | Xcode 27, arm64, 14 cores, 48 GB RAM. Cloudflare Gateway inspects TLS on this network, and `./mach bootstrap` rejects its certificate. | Phase 0 uses Homebrew toolchains with `--disable-bootstrap` instead (see P0.2 and P0.3). |

## Decisions

All decisions were confirmed by the user on 2026-10-22. D2, D10, D11 and D12 replace the original defaults; the rest keep them.

- **D1 en-GB.** Default: ship the UI in `en-US` only, with no language switcher, and bundle an `en-GB` spell-check dictionary. Alternative: fetch `en-GB` strings from the l10n repository and ship both as built-in UI locales that follow the macOS language. Expect extra work every time strings change.
- **D2 Remote Settings. Confirmed by the user: keep security blocklist updates.** Remote Settings stays on behind an M6E allowlist of security and privacy-protection collections only:
  - the `security-state` bucket (OneCRL, intermediate certificates, CRLite certificate revocation);
  - the `blocklists` bucket (add-on and graphics-driver blocklists);
  - `main/tracking-protection-lists`, which Enhanced Tracking Protection's list updates use (`moz-sbrs:://antitracking`);
  - the small privacy collections, such as query-parameter stripping and URL-decoration lists.
  Every other collection uses only its bundled dump and never syncs. Above all, `search-config*` must not sync: the server would restore every engine M6E removes (P3.1). This is the second deliberate Mozilla exception (with D12); the hosts are `firefox.settings.services.mozilla.com`, `firefox-settings-attachments.cdn.mozilla.net` and `content-signature-2.cdn.mozilla.net`, which verifies the data's signatures.
- **D3 AI features.** Default: remove the AI window and the chatbot sidebar (`genai`), and disable Translations, which cannot fetch models once Remote Settings is off. Keep `toolkit/components/ml` only if something still needs it.
- **D4 Search partner codes.** Default: strip Mozilla partner and attribution parameters (`client`, `channel` and similar) from the Google and Bing URLs.
- **D5 User-added search engines.** Default: block them. Disable OpenSearch discovery and the "Add search engine" UI, so Google and Bing stay the only engines.
- **D6 App identity.** Default: `MOZ_APP_NAME=m6e` (binary and process name), `MOZ_APP_VENDOR=M6E`, bundle ID `org.m6e.browser`, a new `MOZ_APP_ID` GUID, and profiles in `~/Library/Application Support/M6E`.
- **D7 Support links.** Default: hide every "Learn more" link that points to `support.mozilla.org` and set `app.support.baseURL` to empty, so no Mozilla URL shows in the UI.
- **D8 Code signing.** Default: ad-hoc signing (`codesign -s -`) for local use. Notarized distribution needs an Apple Developer ID.
- **D9 Upstream base.** Default: keep the checked-in `160.0a1` Nightly snapshot. Since updates are disabled, security fixes arrive only when upstream changes are merged by hand, and this snapshot has no upstream history to merge from. Alternative: replace the base now with a Release or ESR source tree (more stable, smaller security merges later). Either way, record the exact upstream revision in this file.
- **D10 DRM and media plugins. Confirmed by the user: keep automatic downloads, so DRM sites work.** Widevine (from Google) and OpenH264 (from Cisco) install automatically on first run. Firefox normally asks `aus5.mozilla.org` which versions to fetch. M6E skips that request and uses the download lists bundled in `toolkit/content/gmp-sources/`, which point straight at Google and Cisco and carry SHA-512 hashes (P1.12). Those lists are fixed at the snapshot's versions, so refresh them whenever upstream changes are merged.
- **D11 DNS over HTTPS. Confirmed by the user: on by default, and the user can turn it off in Settings.** Default to Increased Protection (`network.trr.mode` 2: DoH first, falling back to system DNS if DoH fails) with Firefox's default provider, Cloudflare (`mozilla.cloudflare-dns.com`, under Mozilla's Trusted Recursive Resolver policy). Keep the existing DNS over HTTPS controls in the Privacy pane (Off, Increased, Max, and provider choice). Remove the region-based DoH rollout and its probes, since the default no longer depends on them. On managed or corporate networks, internal host names may not resolve over DoH; the fallback and the Off setting cover that.
- **D12 Web Push. Confirmed by the user: site push notifications must work, and work well.** Web Push is relayed through a push server, and Firefox's is Mozilla's autopush (`wss://push.services.mozilla.com/`). The only alternative is self-hosting autopush. So M6E keeps Mozilla's push service as a deliberate exception to "no Mozilla services" (the other is the Remote Settings security allowlist, D2). Message contents are end-to-end encrypted (RFC 8291), but Mozilla sees subscription metadata, timing and the connecting IP. Pushes only arrive while M6E is running, which P3.6 addresses.

## Phase 0: Setup and baseline

### P0.1 Ignore build output
- Add `obj-*/`, `artifacts/` and `*.pyc` to `.gitignore` (the last is already present).
- **Verify:** `git status` stays clean after P0.3's build.

### P0.2 Create the mozconfig
- Create `mozconfig` in the repository root, with options added in the step that introduces them:

```
ac_add_options --enable-application=browser
ac_add_options --target=aarch64-apple-darwin
ac_add_options --enable-optimize
ac_add_options --disable-debug
mk_add_options MOZ_OBJDIR=@TOPSRCDIR@/obj-m6e
ac_add_options --disable-bootstrap
ac_add_options --with-wasi-sysroot=/opt/homebrew/share/wasi-sysroot
export WASM_CC=/opt/homebrew/opt/llvm/bin/clang
export WASM_CXX=/opt/homebrew/opt/llvm/bin/clang++
# P1.11: ac_add_options --disable-crashreporter
# P1.12: ac_add_options --disable-updater
# P2.1:  ac_add_options --with-branding=browser/branding/m6e
# P4.2:  optimisation flags (LTO, PGO, CPU target)
```

- Never use artifact builds (`--enable-artifact-builds`). They download Mozilla-built binaries, which would bypass C++ changes and contact Mozilla.
- **Verify:** `./mach configure` succeeds.

### P0.3 Bootstrap and baseline build
- Install the toolchain with Homebrew: `brew install rust cbindgen nasm llvm lld wasi-libc wasi-runtimes` (Node and Xcode were already present). `./mach bootstrap` cannot be used on this network (see Baseline facts). Then run `./mach build`; the baseline build took 21 minutes.
- **Verify:** `./mach run` launches stock Firefox.

### P0.4 Record baselines
Record these in `artifacts/baseline.md`, which is not committed. Phase 4 and the final checks compare against them:
- **Startup:** time to first paint and to a usable window, with a Firefox Profiler startup profile (`MOZ_PROFILER_STARTUP=1`).
- **Memory:** idle memory with one blank tab and with five typical sites (from `about:memory`).
- **Network:** every outbound host contacted by a fresh profile left idle for 5 minutes. Capture with `MOZ_LOG="nsHttp:3,nsHostResolver:3" MOZ_LOG_FILE=artifacts/net.log`, then extract the host names.
- **Upstream:** the upstream revision this snapshot corresponds to (D9). Recorded: `034cd8a8207e38b04afec5e6f75e5161f75a112f`.
- **Tooling:** `./mach python tools/m6e/measure.py --binary <app binary> --out artifacts/<name>` records all of the above, writing `results.json` and `summary.md`.

Commit P0.1 and P0.2 only. The baselines are local notes.

## Phase 1: Debloating and stripping

### Rules for platform removal (P1.1 to P1.4)
- Delete each directory. Then search `moz.build`, `jar.mn`, `moz.configure` and `*.mozbuild` files outside it for references to its path. Fix only references that are *unconditional*; references inside `if CONFIG["MOZ_WIDGET_TOOLKIT"] == "windows"` (and similar) are inert on macOS and stay, per the spec.
- **Keep:**
  - Vendored crates in `third_party/rust/` (for example `winapi-*`). Cargo checks every vendored file against `.cargo-checksum.json`, so deleting them breaks the macOS build.
  - `widget/headless`, which macOS uses for headless tests.
  - Cross-platform directories that merely contain platform files, such as `browser/components/shell`.
  - `build/rust/windows`: the root `Cargo.toml` patches the `windows` crate to it, so Cargo needs it on every platform.
  - Vendored upstream code (for example the Chromium sandbox sources), even where parts are platform-specific.
- **Also check, for every removed directory** (each of these broke a build in P1.1 to P1.3):
  - **Cargo:** workspace `members` in the root `Cargo.toml`, and `path =` dependencies in any `Cargo.toml`, even `cfg(...)`-targeted or optional ones, because Cargo resolves every target. After editing, prune `Cargo.lock` offline: copy `.cargo/config.toml.in` to `.cargo/config.toml`, run `cargo metadata --offline --format-version 1 > /dev/null`, then delete `.cargo/config.toml`. Check that only the removed crates disappear from the lock. A build with no Rust changes never runs Cargo, so a broken workspace can hide until a later step.
  - **Glean:** entries in `toolkit/components/glean/metrics_index.py`. If any remaining code records a metric from a removed `metrics.yaml` (search for `glean::<category>::<metric>` and `Glean.<category>.<metric>`), restore just that `metrics.yaml` at its original path, so the generated header keeps its name. Otherwise drop the entry.
  - **Test manifests:** `include` lines in `testing/crashtest/crashtests.list` and `layout/reftests/reftest.list`.
  - **Licence metadata:** `LICENSED_UNDER[...].paths` entries (for example in `security/sandbox/moz.build`).
  - **mach:** command mappings in `python/mach/mach/command_util.py`.
- `./mach build-backend` reports most of these in seconds, so run it before the full build.

### P1.1 Android and iOS
- Remove `mobile/android` and `mobile/ios`, plus the root Gradle files (`build.gradle`, `settings.gradle`, `gradle.properties`, `gradlew`, `gradlew.bat`, `gradle/`).
- Remove `widget/android`, `widget/uikit` and `toolkit/system/androidproxy`.
- Remove `mobile/locales` and `mobile/shared` only if nothing outside `mobile/` references them.
- **Verify:** build passes.

### P1.2 Windows
- Remove the Windows-only directories:
  - **Widget and sandbox:** `widget/windows`, `security/sandbox/win`.
  - **Installer:** `browser/installer/windows`, `other-licenses/nsis`, `other-licenses/7zstub`.
  - **Toolkit:** `toolkit/mozapps/defaultagent`, `toolkit/xre/dllservices`, `toolkit/system/windowsproxy`, `toolkit/components/aboutwindowsmessages`.
  - **Media:** `dom/media/platforms/wmf`.
  - **Crash tooling:** `toolkit/crashreporter/google-breakpad/src/tools/windows`.
- Find further Windows-only directories by looking for directories reached only behind `OS_ARCH == "WINNT"` or `MOZ_WIDGET_TOOLKIT == "windows"` conditions.
- **Verify:** build passes.

### P1.3 Linux and GTK
- Remove `widget/gtk`, `security/sandbox/linux`, `toolkit/system/gnome`, `toolkit/system/unixproxy`, and other directories reached only behind `gtk` or `Linux` conditions.
- **Verify:** build passes.

### P1.4 Platform source subdirectories and test fixtures
- Remove per-platform source subdirectories without their own `moz.build` that only Windows, Linux, Android or iOS branches reference. Examples: `uriloader/exthandler/{win,android,uikit,unix}`, `hal/{android,linux,windows}`, `dom/gamepad/{android,linux,windows}`, `netwerk/wifi/win`, `dom/media/platforms/android` and `js/src/threading/windows`. Check each with the rules above. Leave Rust sources that `cfg(target_os)` selects.
- Remove test data that exists only for removed platforms, such as Windows `.exe` and `.dll` fixtures whose tests live in removed directories.
- Leave fixtures used by cross-platform tests.
- Linux crash-reporter pieces (`toolkit/crashreporter/rust_minidump_writer_linux`, `toolkit/crashreporter/breakpad-client/linux`) wait for P1.11, because the crash reporter's own crates depend on them.
- **Verify:** build passes.

### P1.5 Language and locale UI
- Set `browser/locales/all-locales` and `browser/locales/shipped-locales` to `en-US` (plus `en-GB` if D1 changes).
- Remove the Language section and the language-pack download UI from `about:preferences` (`browser/components/preferences/main.inc.xhtml` and `main.js`). Lock `intl.multilingual.enabled` and `intl.multilingual.downloadEnabled` to `false`.
- Bundle the `en-GB` spell-check dictionary (D1).
- **Keep** ICU data, hyphenation dictionaries and fonts. They serve web content in every language, not the UI language.
- **Verify:** `about:preferences` has no language switcher; spell-check offers en-GB.

### P1.6 Light-only browser chrome
Split into two commits.
- **P1.6a, forced-light core (done):**
  - `PreferenceSheet::ColorSchemeSettingForChrome()` always returns Light, which also makes `Services.appinfo.chromeColorSchemeIsDark` false (DevTools "auto", toast notifications).
  - `Document::PreferredColorScheme()` returns Light for documents with `ChromeRulesEnabled()` (`chrome:`, `resource:` and privileged `about:` pages). This makes `prefers-color-scheme`, `light-dark()` and system colours resolve light across all browser UI, including about: pages, the PDF viewer and the native window appearance that macOS menus inherit. So no chrome CSS needs editing.
  - `browser.theme.dark-private-windows` is locked off.
  - Web content still follows macOS.
  - Not done, deliberately: forcing the app-wide Aqua appearance in `widget/cocoa`. `SystemWantsDarkTheme()` reads it, so web pages would lose `prefers-color-scheme: dark`.
- **P1.6b, themes:**
  - Remove the built-in Dark, Light and Alpenglow themes (`browser/themes/BuiltInThemeConfig.sys.mjs` and `browser/themes/addons/`), and update their consumers: the about:welcome theme picker, profile themes, about:addons and the bundled theme localisation. The AI window themes go with D3 in P1.13.
  - Block theme add-on installs, and hide the Themes category in `about:addons`.
- **Verify:** with `ui.systemUsesDarkTheme` set to 1, chrome, `about:preferences` and `chromeColorSchemeIsDark` are light, while a web page sees `prefers-color-scheme: dark`. No theme other than the default can be selected or installed.

### P1.7 Firefox View
- Remove `browser/components/firefoxview`, its toolbar button and tab-strip entry, its menu items, and its tests. Remove `browser.tabs.firefox-view*` prefs.
- **Verify:** no Firefox View button or menu entry; `about:firefoxview` does not resolve.

### P1.8 Sync and Mozilla accounts (done)
- Lock `identity.fxaccounts.enabled` and `browser.aboutwelcome.enabled` off: the prefs the `DisableFirefoxAccounts` enterprise policy locks. That hides the account button, the app-menu sign-in, the Tools-menu Sync items and Send Tab.
- **Settings:** hide the "account disabled" group, which would otherwise read as an administrator lockout. The Account & Sync pane keeps Default browser, Import and Backup; its title is renamed in P2.4.
- **Sidebar:** don't register the synced-tabs sidebar tool when accounts are off.
- **Deviation from the original plan:** `MOZ_SERVICES_SYNC` stays on and `services/sync` stays built. About 24 browser and toolkit modules import Sync modules (`resource://services-sync/...`), so building without them would turn each import into a runtime failure. With no account, Sync never runs and makes no network requests (checked in P1.14).

### P1.9 VPN (IP Protection) (done)
- Lock `browser.ipProtection.enabled` off and `browser.ipProtection.blockIPProtectionCallouts` on, and remove its startup and quit hooks from `BrowserComponents.manifest`. The service, toolbar button and Settings section are all gated on that pref, so none of them load.
- As with Sync, the dormant code stays built: removing it would touch about 20 files (Settings, CustomizableUI, policies) with no user-visible difference.

### P1.10 Telemetry and data collection (done)
- `MOZ_SERVICES_HEALTHREPORT` is `False` in `browser/moz.configure`. `MOZ_TELEMETRY_REPORTING` stays off because `MOZILLA_OFFICIAL` is never set.
- `m6e.js` defines and locks every upload pref off, including the health-report and usage prefs the flag no longer defines: telemetry, the archive, the ping senders, the server URL, new-tab telemetry and discovery. The data-submission notification bar is bypassed.
- **Verified:** `Services.telemetry.canRecordBase` and `canRecordExtended` are false. The Glean and Telemetry modules stay built as dormant code.
- **Build cost:** changing a configure flag rewrites the global config header and rebuilds the whole tree (1 h 54 min here). So P1.11 and P1.12's configure changes (`--disable-crashreporter`, `--disable-updater`, `MOZ_NORMANDY`) are batched into one rebuild.

### P1.11 Crash reporting (done, committed with P1.12)
- `--disable-crashreporter` in the mozconfig. The crash-report settings, unsubmitted-report prompts and tab-crash "send report" option are off, and `browser.crashReports.*` and `browser.tabs.crashReporting.sendReport` are locked off.
- The Linux crash-reporter crates (`rust_minidump_writer_linux`, `breakpad-client/linux`) stay: the crash helper's Cargo manifests reference them, and they aren't compiled on macOS.

### P1.12 Updates, AMO, Normandy, Remote Settings, Safe Browsing (done)
- **Configure:** `--disable-updater`, and `MOZ_NORMANDY` `False` (Normandy's code is still built, only its startup and `about:studies` are gated). With telemetry, health report, crash reporter and Normandy all off, `MOZ_DATA_REPORTING` is off, which removes the Settings data-collection section. The last Windows installer file (`updater_append.ini`) is gone with the updater.
- **Prefs (`m6e.js`, locked):**
  - **Add-ons:** system add-on updates, Normandy and studies, AMO recommendations, the discover pane, add-on update checks and abuse reports are all off.
  - **Safe Browsing:** malware, phishing, download and blocked-URI checks are off, and the Google provider URLs are blank. The Mozilla provider's `moz-sbrs:` update URL stays, because it delivers the tracking-protection lists through Remote Settings.
  - **Startup checks:** captive-portal, connectivity and region checks are off, and the welcome and what's-new URLs are blank.
- **Remote Settings (D2):** `Utils.isAllowedCollection()` in `services/settings/Utils.sys.mjs` is the single hard-coded allowlist. It covers the `security-state` and `blocklists` buckets, plus these `main` collections: `anti-tracking-url-decoration`, `bounce-tracking-protection-exceptions`, `fingerprinting-protection-overrides`, `partitioning-exempt-urls`, `query-stripping`, `third-party-cookie-blocking-exempt-urls`, `tracking-protection-lists` and `url-classifier-exceptions`. It is enforced in:
  - `RemoteSettingsClient.sync()` and `maybeSync()`;
  - the attachment downloader (`downloadAsBytes`, `cacheAll`);
  - `pullStartupBundle()`, which never runs, because the bundle carries other collections.
- **Media plugins (D10):** `media.gmp-manager.url` is blank, so the installer falls back to the bundled lists. Verified: OpenH264 comes from `ciscobinary.openh264.org`, and Widevine from `edgedl.me.gvt1.com` with `www.google.com` as mirror. Installs are triggered by the `BrowserGlue` idle task, which doesn't need the updater.
- **DNS over HTTPS (D11):** `network.trr.mode` defaults to 2 (not locked), with `mozilla.cloudflare-dns.com`. The region-based rollout (`DoHController`) is no longer started.

### P1.13 Sponsored content and AI features (done)
- **Sponsored content:** lock off New Tab's sponsored tiles and stories, recommended (Pocket) stories, weather, the unified-ads endpoints, Contile, Firefox Suggest and Merino, and extension/feature recommendations (CFR). New Tab's own `DefaultPrefs.init()` never replaces an existing default, so the locked values hold.
- **AI (D3):** lock `browser.ai.control.*` to `blocked`, with each feature's switch off, as the `AIControls` enterprise policy does: sidebar chatbot, Translations, PDF alt text, link-preview key points, smart tab groups, Smart Window, speech recognition, and the ML engine `browser.ml.enable`. Hide the Settings AI pane (`browser.preferences.aiControls`).
- As with Sync, the AI, ML and Translations code stays built: around 150 external references across those directories, with no user-visible difference once the switches are off.
- **Verified:** New Tab shows the default shortcuts and widgets only, the chatbot sidebar tool is hidden, and the AI pane is gone. Marionette's test profile disables New Tab feeds, so checking New Tab needs `browser.newtabpage.activity-stream.testing.shouldInitializeFeeds` set at launch.

### P1.14 Pocket leftovers and Phase 1 check
- Search for leftover Pocket prefs, strings and icons (`extensions.pocket`, `pocket` in `browser/` front-end files) and remove them.
- Repeat the P0.4 network capture. Expected:
  - **Allowed:** `push.services.mozilla.com` (D12); the Remote Settings hosts, for allowlisted collections only (D2); the DoH resolver `mozilla.cloudflare-dns.com` (D11); and the Widevine and OpenH264 downloads from Google and Cisco on a first run (D10).
  - **Off:** captive-portal and connectivity checks, region lookup (`location.services.mozilla.com`), the first-run welcome page, new-tab DNS prefetching, and the DoH rollout probes.
- **Verify:** the 5-minute idle log contains only the allowed hosts above; `./mach run` works for basic browsing.

## Phase 2: Rebranding and UI

### P2.1 Branding directory
- Copy `browser/branding/unofficial` to `browser/branding/m6e`.
- Set the brand names to M6E in `locales/en-US/brand.ftl` and `brand.properties`, and set the display name in `configure.sh` (`MOZ_APP_DISPLAYNAME=M6E`).
- Add `--with-branding=browser/branding/m6e` to the mozconfig.
- Remove the `aurora`, `nightly` and `official` branding directories once nothing references them. `official` holds Mozilla's trademarked logos.
- **Verify:** the app is `M6E.app`; the menu bar and About dialog say M6E.

### P2.2 Icon pipeline
- Write `browser/branding/m6e/generate-icons.sh`, using a small Swift/CoreGraphics script (no third-party dependencies), that turns `M6E-Icon.png` into every branding image.
  - **Crop:** find the white rounded tile automatically (roughly x 757 to 2058 and y 138 to 1398 in the current 2816x1536 source) instead of hardcoding it. The crop discards the grey background and the watermark.
  - **Mask:** make the area outside the tile's rounded corners transparent.
  - **Canvas:** centre the tile on a 1024x1024 transparent canvas at Apple's icon-grid size (an 824x824 tile with margin for the shadow). Never stretch.
  - **Outputs:** the full `.iconset` via `sips`, then `iconutil -c icns`; plus every other image the branding directory expects (the about-dialog logo, in-app logos, `content/` images, DMG background and volume icon).
- Commit the script, the 1024x1024 master and the generated assets, using the stable names the branding directory already uses.
- **Verify:** Dock, Finder, the About dialog and the DMG show the autorickshaw at every size; replacing `M6E-Icon.png` and rerunning the script updates everything with no build changes.

### P2.3 App identity (D6)
- In `browser/moz.configure` and the branding `configure.sh`, set `MOZ_APP_VENDOR=M6E`, a new `MOZ_APP_ID` GUID, `MOZ_APP_NAME=m6e` and bundle ID `org.m6e.browser`.
- **Verify:** profile created in `~/Library/Application Support/M6E`; an installed Firefox and M6E run side by side without sharing profiles.

### P2.4 User-visible strings
- Write a script that lists every literal "Firefox" and "Mozilla" in the `en-US` `.ftl`, `.properties` and `.dtd` files, and in user-visible `.xhtml` and `.html`, skipping comments.
- Replace each with the brand terms (`-brand-short-name` and similar) or reword it. Literal names include "Firefox Account", "Mozilla VPN" and "Firefox Relay"; most belong to features removed in Phase 1 and can be deleted along with them.
- Remove `about:mozilla`. Rewrite `about:rights` and the About dialog's credits.
- Apply D7 (hide `support.mozilla.org` "Learn more" links). Remove "More from Mozilla" (`moreFromMozilla.*`).
- **Keep** "Mozilla"/"Firefox" in license text, MPL headers, `AUTHORS`, code identifiers and internal URLs that users never see.
- **Verify:** unpack `obj-m6e/dist/bin` and its `omni.ja` archives, then search the user-visible localization files and markup for `Firefox` and `Mozilla`. The only hits allowed are licensing and attribution.

### P2.5 Licence attribution
- Add "M6E is based on Firefox source code, licensed under the MPL 2.0" to `about:license` and the About dialog.
- **Verify:** both show the notice.

### P2.6 Modern macOS UI (needs user review)
- Before changing CSS, capture screenshots of the main window, menus, tabs, the urlbar, `about:preferences` and new tab. Agree a short list of concrete changes with the user (for example spacing, tab shape, toolbar density, vibrancy).
- Implement the changes in `browser/themes/osx` and `browser/themes/shared`, keeping them light-only (P1.6).
- **Verify:** compare before and after screenshots with the user.

## Phase 3: Features and privacy

### P3.1 Search engines (D4, D5)
- Edit `services/settings/dumps/main/search-config-v2.json`, plus `search-config-v3.json` if the v3 gate can be enabled:
  - Keep only Google (default for every region and locale) and Bing.
  - Remove partner codes (D4).
  - Prune `search-config-icons` and `search-default-override-allowlist.json` to match.
  - Remove `search-telemetry-v2.json` usage.
- Delete search tests that assert other engines (`toolkit/components/search/tests/xpcshell/searchconfigs/`), and update the remaining ones.
- Apply D5: disable OpenSearch discovery and adding engines from the urlbar, the search settings and the context menu.
- **Verify:** `./mach test toolkit/components/search` passes; the search settings list exactly Google (default) and Bing; searches go to plain Google and Bing URLs.

### P3.2 Bookmarks
- Keep the bookmark manager and importing from other browsers (`MOZ_PROFILE_MIGRATOR` stays on).
- Add a "Show bookmarks toolbar" checkbox to `about:preferences` (General, browser layout), bound to `browser.toolbars.bookmarks.visibility` (`always` or `never`).
- **Verify:** the checkbox toggles the toolbar both ways; importing from Safari and Chrome works.

### P3.3 Reader View
All changes are inside `toolkit/components/reader` and `toolkit/themes/shared/aboutReader.css`. Do not edit the vendored `readability/` library; post-process its output in `AboutReader.sys.mjs`.
- **Trigger:** keep the reader button for pages detected as articles. Add a View menu item that forces reader mode on any page.
- **Clutter:** after Readability runs, remove every remaining `iframe`, `nav`, `aside`, form and ad-like element.
- **Typography:** the base text size is 1.25 times the stock base at the default size step. Calculate the multiplier from how `reader.font_size` maps to pixels, rather than editing the step value. The user can still resize.
- **Styling:** text `#000000` on `#FFFFFF`. Remove the other colour schemes from `reader.color_scheme.values`, along with their toolbar controls, and remove every background image and page colour.
- **Media:** keep inline article `img` and `picture` elements. Drop tracking pixels, icons and other tiny images (below roughly 48 px in both dimensions) and all video embeds.
- **Verify:**
  - `./mach test toolkit/components/reader` passes, with updated expectations.
  - Manual check on three news or blog articles: black on white, 25% larger text, no sidebars, ads or iframes, article images kept.

### P3.4 Privacy hardening defaults
- Add a single M6E pref file (for example `browser/app/profile/m6e.js`, included from `firefox.js`), so upstream merges touch one place.
- Start from a curated Arkenfox subset and document each pref with its reason:
  - **Tracking:** Enhanced Tracking Protection in Strict mode (`browser.contentblocking.category`), and `privacy.resistFingerprinting` on.
  - **HTTPS:** HTTPS-Only mode.
  - **Storage:** first-party isolation, through Total Cookie Protection, which Strict mode includes.
  - **Leaks:** stricter referrer trimming, no prefetching or speculative connections, and WebRTC IP-leak protection.
- Skip Arkenfox prefs whose only job is to disable things already removed in Phase 1.
- Do not disable Web Push, notifications or DRM (D10, D12). Check that `privacy.resistFingerprinting` doesn't block or spoof the Notification and Push APIs; if it does, exempt them.
- **Verify:** a fresh profile shows the expected values in `about:config`. Run a fingerprinting test page (manual) and five popular sites as a compatibility sample, and record what breaks.

### P3.5 Privacy Shield panel
- Add a "Privacy Shield" section at the top of the Privacy pane (`privacy.inc.xhtml` and `privacy.js`), with a master toggle backed by `m6e.privacyShield.enabled`.
- A small module (for example `browser/modules/M6EPrivacyShield.sys.mjs`) applies the strict set from P3.4 when the toggle is on. When it is off, it restores compatibility values: Standard ETP and fingerprinting resistance off. It must respect any pref the user changed by hand.
- Show plainly what the toggle changes, and keep the existing detailed controls below it.
- **Verify:** toggling flips the listed prefs both ways and survives restart; preference tests for the Privacy pane pass.

### P3.6 Reliable push notifications (D12)
- **Native alerts:** route notifications through macOS Notification Center (`alerts.useSystemBackend`), and make sure the macOS notification-permission request appears on first use.
- **Stay running:** M6E receives pushes only while it runs. Keep it running after its last window closes (the macOS default; confirm nothing in Phase 1 changed that).
- **Launch at login:** add an optional, off-by-default "Open M6E at login, hidden" setting, using a macOS login item through `SMAppService`. Agree its scope with the user before building it, since it needs native code.
- **Verify:** subscribe on a Web Push test page, close every window, send a push, and confirm a native macOS notification arrives. Repeat with the Privacy Shield on and off, and after a restart.

## Phase 4: Performance

### P4.1 Measure
- Repeat the P0.4 startup and memory measurements on the Phase 3 build. Record how much Phase 1's removals changed startup and memory.

### P4.2 Build flags
- Add to the mozconfig, one at a time, measuring after each:
  - **LTO:** `--enable-lto=cross` (needs the bootstrapped clang).
  - **PGO:** `export MOZ_PGO=1`, which adds an instrumented training run.
  - **CPU target:** `-mcpu=apple-m1` for C and C++ and `-C target-cpu=apple-m1` for Rust. This is safe because M6E is arm64-only and M1 is the oldest Apple Silicon.
  - **Optimisation level:** set `--enable-optimize` explicitly to the spec's `-O3`, and confirm Rust `opt-level = 3` for release.
- Keep each flag only if it builds reliably and measurably helps.
- **Verify:** the build passes; startup and Speedometer 3 (run locally) do not regress.

### P4.3 Profile front-end hotspots
- Take startup and interaction profiles with the Firefox Profiler, and list front-end JS functions above a threshold (for example 10 ms of main-thread time during startup).
- Prefer removing or deferring work: lazy-loading modules, deleting work for removed features.
- **Verify:** profile comparison saved in `artifacts/`.

### P4.4 Rust rewrites (gated)
- Rewrite a JS controller in Rust only if P4.3 shows a hotspot that deferring cannot fix. Present the profile data to the user and get approval first.
- Prefer native Rust behind an XPCOM/`xpcom` interface (the existing Rust-in-Gecko pattern) over WebAssembly in the chrome.
- The expected outcome is that no rewrite is needed.
- **Verify:** for each rewrite, a before-and-after profile and passing tests.

## Phase 5: Packaging and final acceptance

### P5.1 Package
- Run `./mach package` to produce `M6E.app` and the `.dmg` in `obj-m6e/dist/`.
- Sign the app ad-hoc (D8): `codesign --force --deep -s -`. Apple Silicon only runs code with at least an ad-hoc signature, and changing files after linking invalidates the linker's signature.
- **Verify:** the DMG mounts, shows the M6E artwork, and the app copied to `/Applications` launches. On another Mac, the Gatekeeper prompt for an unnotarized app is expected.

### P5.2 Acceptance checklist
Run on the packaged app with a fresh profile:
- **Network:** no outbound connections while idle for 5 minutes. While browsing, only the sites visited plus Google or Bing search.
- **Branding:** no user-visible "Firefox" or "Mozilla" (repeat the P2.4 scan); licence attribution present.
- **Theme:** macOS Dark Mode leaves the chrome light, while web content still follows `prefers-color-scheme`.
- **Search:** only Google (default) and Bing.
- **Removed features:** no Firefox View, Sync, VPN, Pocket, sponsored content, telemetry or crash-report UI, or AI features.
- **Bookmarks:** the toolbar toggle works; import works.
- **Reader View:** meets every P3.3 rule.
- **Privacy Shield:** the master toggle works and survives restart.
- **Performance:** startup and memory are at least as good as P0.4.

### P5.3 Ongoing maintenance
- With updates disabled, security fixes arrive only through manual upstream merges (D9). Keep the M6E changes concentrated (the prefs file, the branding directory, small patches) to keep those merges manageable.

## Execution notes

- **Session per phase:** start a fresh session for each phase, using the spec, this plan and `CLAUDE.md` as the handoff. The tree is large enough that a single session will lose context.
- **Model choice:** Sonnet suits Phases 0 to 2 (high-volume, build-driven). Opus suits Phases 3 and 4 (design and profiling). Fable is for stubborn build-system or privacy-review problems.
- **Main risks:**
  - Stale build references after directory removal. Mitigated by the per-step build gate.
  - Forced Aqua appearance leaking into web content's `prefers-color-scheme` (P1.6).
  - Arkenfox-style prefs breaking sites. Mitigated by the Privacy Shield toggle.
  - LTO and PGO lengthening builds.
  - Security exposure from the frozen base (D2, D9).
  - Remote Settings allowlist drift (D2): upstream may add security collections or move existing ones, so review the allowlist on every upstream merge.
  - Dependence on Mozilla's push service (D12). If Mozilla changes or restricts it, push notifications stop until M6E self-hosts autopush.
  - Stale DRM plugin versions in the bundled `gmp-sources` lists (D10). Refresh them during upstream merges.
