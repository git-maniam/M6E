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
| Base version | Firefox `160.0a1`, a **Nightly** snapshot. No upstream VCS history. | Nightly enables nightly-only features and code paths (`NIGHTLY_BUILD`). See decision D9. |
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
| `.gitignore` | Upstream's `.gitignore`/`.hgignore` were not imported. The current file ignores only Python bytecode, `.gradle/` and `.DS_Store`. | P0.1 must ignore the objdir before the first build, or a "commit everything" would sweep in gigabytes. |
| Toolchain | Xcode 27, `~/.mozbuild` present, arm64, 14 cores, 48 GB RAM, about 315 GB free. No `mozconfig` yet. | P0.2 to P0.3. |

## Decisions to confirm before execution

Each decision has a default, which this plan assumes unless changed. Settle D9 first: it is cheapest to change before any work starts.

- **D1 en-GB.** Default: ship the UI in `en-US` only, with no language switcher, and bundle an `en-GB` spell-check dictionary. Alternative: fetch `en-GB` strings from the l10n repository and ship both as built-in UI locales that follow the macOS language. Expect extra work every time strings change.
- **D2 Remote Settings.** Default (per the spec): disabled completely. Trade-off: this also stops updates to certificate revocation (CRLite), intermediate certificates, the add-on blocklist and the Translations models. The bundled dumps (search config and others) keep working. The alternative, keeping only the security collections, means contacting Mozilla servers.
- **D3 AI features.** Default: remove the AI window and the chatbot sidebar (`genai`), and disable Translations, which cannot fetch models once Remote Settings is off. Keep `toolkit/components/ml` only if something still needs it.
- **D4 Search partner codes.** Default: strip Mozilla partner and attribution parameters (`client`, `channel` and similar) from the Google and Bing URLs.
- **D5 User-added search engines.** Default: block them. Disable OpenSearch discovery and the "Add search engine" UI, so Google and Bing stay the only engines.
- **D6 App identity.** Default: `MOZ_APP_NAME=m6e` (binary and process name), `MOZ_APP_VENDOR=M6E`, bundle ID `org.m6e.browser`, a new `MOZ_APP_ID` GUID, and profiles in `~/Library/Application Support/M6E`.
- **D7 Support links.** Default: hide every "Learn more" link that points to `support.mozilla.org` and set `app.support.baseURL` to empty, so no Mozilla URL shows in the UI.
- **D8 Code signing.** Default: ad-hoc signing (`codesign -s -`) for local use. Notarized distribution needs an Apple Developer ID.
- **D9 Upstream base.** Default: keep the checked-in `160.0a1` Nightly snapshot. Since updates are disabled, security fixes arrive only when upstream changes are merged by hand, and this snapshot has no upstream history to merge from. Alternative: replace the base now with a Release or ESR source tree (more stable, smaller security merges later). Either way, record the exact upstream revision in this file.

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
# P1.11: ac_add_options --disable-crashreporter
# P1.12: ac_add_options --disable-updater
# P2.1:  ac_add_options --with-branding=browser/branding/m6e
# P4.2:  optimisation flags (LTO, PGO, CPU target)
```

- Never use artifact builds (`--enable-artifact-builds`). They download Mozilla-built binaries, which would bypass C++ changes and contact Mozilla.
- **Verify:** `./mach configure` succeeds.

### P0.3 Bootstrap and baseline build
- Run `./mach bootstrap` (choose Firefox for Desktop), then `./mach build`. Expect tens of minutes.
- **Verify:** `./mach run` launches stock Firefox.

### P0.4 Record baselines
Record these in `artifacts/baseline.md`, which is not committed. Phase 4 and the final checks compare against them:
- **Startup:** time to first paint and to a usable window, with a Firefox Profiler startup profile (`MOZ_PROFILER_STARTUP=1`).
- **Memory:** idle memory with one blank tab and with five typical sites (from `about:memory`).
- **Network:** every outbound host contacted by a fresh profile left idle for 5 minutes. Capture with `MOZ_LOG="nsHttp:3,nsHostResolver:3" MOZ_LOG_FILE=artifacts/net.log`, then extract the host names.
- **Upstream:** the upstream revision this snapshot corresponds to (D9).

Commit P0.1 and P0.2 only. The baselines are local notes.

## Phase 1: Debloating and stripping

### Rules for platform removal (P1.1 to P1.4)
- Delete each directory. Then search `moz.build`, `jar.mn`, `moz.configure` and `*.mozbuild` files outside it for references to its path. Fix only references that are *unconditional*; references inside `if CONFIG["MOZ_WIDGET_TOOLKIT"] == "windows"` (and similar) are inert on macOS and stay, per the spec.
- **Keep:**
  - Vendored crates in `third_party/rust/` (for example `winapi-*`). Cargo checks every vendored file against `.cargo-checksum.json`, so deleting them breaks the macOS build.
  - `widget/headless`, which macOS uses for headless tests.
  - Cross-platform directories that merely contain platform files, such as `browser/components/shell`.

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

### P1.4 Platform test fixtures
- Remove test data that exists only for removed platforms, such as Windows `.exe` and `.dll` fixtures whose tests live in removed directories.
- Leave fixtures used by cross-platform tests.
- **Verify:** build passes.

### P1.5 Language and locale UI
- Set `browser/locales/all-locales` and `browser/locales/shipped-locales` to `en-US` (plus `en-GB` if D1 changes).
- Remove the Language section and the language-pack download UI from `about:preferences` (`browser/components/preferences/main.inc.xhtml` and `main.js`). Lock `intl.multilingual.enabled` and `intl.multilingual.downloadEnabled` to `false`.
- Bundle the `en-GB` spell-check dictionary (D1).
- **Keep** ICU data, hyphenation dictionaries and fonts. They serve web content in every language, not the UI language.
- **Verify:** `about:preferences` has no language switcher; spell-check offers en-GB.

### P1.6 Light-only browser chrome
- Lock `browser.theme.toolbar-theme` to light. Leave `layout.css.prefers-color-scheme.content-override` following the system, so web pages still see the macOS appearance (spec: keep the content plumbing).
- Force `color-scheme: light` at the chrome document roots, so `light-dark()` in the design tokens resolves to light. Then remove the 40 `prefers-color-scheme: dark` blocks from chrome CSS (`browser/themes`, `toolkit/themes`, `browser/components`, `toolkit/content`, `toolkit/components`).
- Remove the built-in `dark`, `alpenglow`, `aiwindow` and `aiwindow-nova` themes from `browser/themes/addons`, along with their registration.
  - Check what `privatewindow` is used for before deciding whether it stays.
- Make the default theme light-only, and block installation of theme add-ons: hide the Themes category in `about:addons` and reject theme installs.
- Native macOS UI (menus, context menus, dialogs) follows the app's appearance. Force the Aqua appearance for the app in `widget/cocoa`.
- **Risk:** forcing the app appearance can change what `prefers-color-scheme` reports to web content. Verify it separately.
- **Verify:** with macOS in Dark Mode, chrome, menus and dialogs are light, and a test page that styles on `prefers-color-scheme` still shows its dark styling.

### P1.7 Firefox View
- Remove `browser/components/firefoxview`, its toolbar button and tab-strip entry, its menu items, and its tests. Remove `browser.tabs.firefox-view*` prefs.
- **Verify:** no Firefox View button or menu entry; `about:firefoxview` does not resolve.

### P1.8 Sync and Mozilla accounts
- Change `imply_option("MOZ_SERVICES_SYNC", True)` to `False` in `browser/moz.configure`.
- Lock `identity.fxaccounts.enabled` to `false`.
- Remove the Sync pane (`sync.inc.xhtml`, `sync.js`, `fxaPairDevice.*`) from preferences, plus the account button and the toolbar and menu sign-in entry points.
- Keep `services/fxaccounts` code only where other code still imports it; remove imports where feasible.
- **Verify:** no sign-in or Sync UI anywhere; build passes.

### P1.9 VPN (IP Protection)
- Remove `browser/components/ipprotection` from the build, along with its toolbar button, panel, prefs and tests.
- **Verify:** no VPN UI; build passes.

### P1.10 Telemetry and data collection
- In `browser/moz.configure`, change `MOZ_SERVICES_HEALTHREPORT` to `False`. Together with P1.11 and P1.12 this turns off `MOZ_DATA_REPORTING`. Keep `MOZ_TELEMETRY_REPORTING` off by never setting `MOZILLA_OFFICIAL`.
- Lock data upload off and blank the endpoints:
  - `datareporting.healthreport.uploadEnabled` and `datareporting.policy.dataSubmissionEnabled`: `false`.
  - `toolkit.telemetry.*`: off.
  - Glean upload: off.
  - `toolkit.telemetry.server` and similar endpoint prefs: empty.
- Remove the "Firefox Data Collection and Use" section from `privacy.inc.xhtml`, and the data-reporting notification bar.
- Keep `toolkit/components/glean` and `toolkit/components/telemetry` as dead-but-compiling code (see Baseline facts).
- **Verify:** the network capture shows no telemetry hosts (`incoming.telemetry.mozilla.org` and similar).

### P1.11 Crash reporting
- Add `--disable-crashreporter` to the mozconfig. Remove the crash-report UI (`about:crashes` entry points, the "send crash reports" setting).
- **Verify:** build passes; no crash-report settings.

### P1.12 Updates, AMO, Normandy, Remote Settings, Safe Browsing
- **Updates:** add `--disable-updater` to the mozconfig, and remove the update UI from the About dialog and preferences.
- **Normandy:** change `MOZ_NORMANDY` to `False` in `browser/moz.configure`. Lock `app.normandy.enabled` and `app.shield.optoutstudies.enabled` to `false`, and disable Nimbus experiments.
- **AMO:**
  - Set `extensions.getAddons.cache.enabled` and `extensions.htmlaboutaddons.recommendations.enabled` to `false`, and `extensions.getAddons.showPane` to `false`.
  - Remove the "Find more add-ons" links and the recommended-extensions pane.
  - Keep manual installation of `.xpi` files.
- **Remote Settings (D2):** point `services.settings.server` at nothing, so only the bundled dumps load.
- **Safe Browsing:** set `browser.safebrowsing.malware.enabled`, `browser.safebrowsing.phishing.enabled` and `browser.safebrowsing.downloads.remote.enabled` to `false`, and blank the provider URLs.
- **Verify:** the network capture shows no `*.mozilla.org`, `*.mozilla.com`, `*.mozilla.net`, `*.firefox.com` or Safe Browsing hosts. An `.xpi` still installs from disk.

### P1.13 Sponsored content and AI features
- Lock `browser.newtabpage.activity-stream.showSponsoredTopSites`, `browser.newtabpage.activity-stream.showSponsored` and `browser.urlbar.suggest.quicksuggest.sponsored` to `false`. Remove the sponsored settings UI and the Firefox Suggest online paths in `browser/extensions/newtab` and the urlbar.
- Apply D3: remove `browser/components/aiwindow` and `browser/components/genai`, and disable Translations.
- **Verify:** new tab shows no sponsored tiles or stories; no AI sidebar or window.

### P1.14 Pocket leftovers and Phase 1 check
- Search for leftover Pocket prefs, strings and icons (`extensions.pocket`, `pocket` in `browser/` front-end files) and remove them.
- Repeat the P0.4 network capture. Expected: no outbound connections except captive-portal and connectivity checks, which M6E should also disable or point at a neutral host.
- **Verify:** the network log is empty for 5 idle minutes; `./mach run` works for basic browsing.

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
- **Verify:** a fresh profile shows the expected values in `about:config`. Run a fingerprinting test page (manual) and five popular sites as a compatibility sample, and record what breaks.

### P3.5 Privacy Shield panel
- Add a "Privacy Shield" section at the top of the Privacy pane (`privacy.inc.xhtml` and `privacy.js`), with a master toggle backed by `m6e.privacyShield.enabled`.
- A small module (for example `browser/modules/M6EPrivacyShield.sys.mjs`) applies the strict set from P3.4 when the toggle is on. When it is off, it restores compatibility values: Standard ETP and fingerprinting resistance off. It must respect any pref the user changed by hand.
- Show plainly what the toggle changes, and keep the existing detailed controls below it.
- **Verify:** toggling flips the listed prefs both ways and survives restart; preference tests for the Privacy pane pass.

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
