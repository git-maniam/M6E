# Project M6E: Specification

This is the source of truth for **what** M6E is and **why**. `M6E_IMPLEMENTATION_PLAN.md` records **how** each requirement is built, step by step, with results and commits. If the two disagree, this file wins and the plan is updated.

## 1. Overview

**Objective:** turn the Mozilla Firefox codebase into **M6E**, a hyper-optimized, heavily debloated, privacy-first browser for macOS that matches or exceeds LibreWolf's performance and anti-telemetry behaviour.

**Identity**

| | |
|---|---|
| Name | M6E (said "'Am Sexy") |
| Icon | A stylized Bangalore autorickshaw: yellow body, green canopy, on a white rounded-square tile. Source artwork: `M6E-Icon.png` in the repository root. |
| Language | English. The interface is en-US, with a UK English (en-GB) spell-check dictionary (D1). |
| Platform | macOS only, Apple Silicon (arm64) only. No Intel or universal build. |
| App bundle | `M6E.app`, bundle ID `org.m6e.browser`, executable `m6e`, profiles in `~/Library/Application Support/M6E` (D6) |
| Code base | Firefox 160.0a1 (Nightly), upstream `mozilla-firefox/firefox` commit `034cd8a8207e38b04afec5e6f75e5161f75a112f` (D9) |
| Repository | `github.com/git-maniam/M6E`, branch `main`, single developer |

**About M6E text** (shown in the About dialog, above the licence attribution):

> Welcome to M6E, a hyper-optimized, macOS-exclusive browser engineered by Ravi Subramaniam using Claude Code to make the web fast, private, and beautifully simple. Inspired by the agility & versatile nature of the Autorickshaw (Tuk Tuk), M6E ('Am Sexy) strips away legacy bloat to deliver uncompromising built-in tracker blocking, a pristine light-themed interface, and a custom Reading Mode that removes page clutter for effortless reading. The focus of this browser is speed and privacy.
>
> Please note: M6E is currently an experimental release, so while it is built for pure performance, you may occasionally encounter bugs or website compatibility issues.

## 2. Status (2026-10-29)

| Phase | Scope | Status |
|---|---|---|
| 0 | Toolchain, mozconfig, baseline build and measurements | Done |
| 1 | Debloating: platforms, languages, themes, Mozilla features and services | Done |
| 2 | Rebranding and macOS UI | Done |
| 3 | Search engines, bookmarks, Reader View, privacy hardening, Privacy Shield, push, Tracker Blocker button | Done |
| 4 | Performance, platform removal, dormant-feature removal, security hardening | Done (2026-10-24). Speedometer 3 is about +28% over the Phase 3 build; Windows, Linux and Android code removed; hardening as P4.8; the dormant features marked for deletion in P6.1 (D30). |
| 5 | Packaging (`.dmg`) and acceptance | Done (2026-10-24). Signed ad hoc, release DMG built and verified, acceptance 15 of 15; first paint 579 ms, blank-tab memory 443 MB. |
| 6 | Delete the dormant features (D30) | **In progress** (started 2026-10-24; plan steps P6.0 to P6.3). Done by 2026-10-29: P6.0 (baseline), P6.1a to P6.1i (VPN, Translations, generative AI, machine learning, Sync, Mozilla accounts, application updater, crash reporter, Nimbus) and P6.2 (lint, config, Fluent and CI clean-up). Next: P6.3 (configure-flag clean-up, LTO and PGO back on, clean build, full regressions including the window suites, acceptance 15 of 15 on the packaged app, measurements, release DMG), prepared on `m6e-p6/ready-p62d` and `ready-p63`. "Resume after restart" starts at the Restart bookmark in plan section 4. |

## 3. Requirements

### Phase 1: Debloating (done)
- **Platforms:** remove the Windows, Linux, Android and iOS directories and platform-only source subdirectories. Leave `#ifdef` branches in shared code, and vendored third-party code, in place.
- **Languages:** the en-US interface only, with no language switching or language-pack downloads, plus an en-GB spell-check dictionary (D1).
- **Theme:**
  - The browser UI (browser window, menus and dialogs, `about:` pages, PDF viewer) is always light, whatever the macOS appearance.
  - Web content keeps following `prefers-color-scheme`.
  - The default theme is the only theme; no theme add-ons can be installed.
- **Removed features:**
  - **Removed outright:** Pocket (already removed upstream), Firefox View, sponsored tiles and stories, sponsored search suggestions, and server-backed New Tab widgets and wallpapers.
  - **Switched off and locked:** Mozilla accounts and Sync, the Mozilla VPN, telemetry and data collection, the crash reporter, updates, add-on recommendations and update checks (AMO), studies (Normandy), Safe Browsing lookups, and all AI features (D3).
  - Some code stays built but dormant where deleting it is unsafe (D13).
- **Network:** a fresh profile contacts only these:
  - Remote Settings, for security and tracking-protection data only (D2);
  - Web Push (D12);
  - the DNS-over-HTTPS resolver, Cloudflare for Families (security), which also blocks malware and phishing domains (D11, D25);
  - Google and Cisco, for the DRM and video-call plugins (D10).

  Mozilla is contacted for exactly two things: Web Push and the Remote Settings security allowlist.

### Phase 2: Rebranding and UI (done)
- **Branding:** use Firefox's branding mechanism (`browser/branding/m6e`), not a blind text replace.
  - "Firefox" and "Mozilla" must not appear in anything user-visible (UI, app bundle, `about:` pages, displayed URLs).
  - Keep `LICENSE`, the MPL headers and `AUTHORS` intact, and keep internal identifiers such as `MOZ_` prefixes and the app ID (D14).
- **Attribution:** the About dialog and `about:license` state that M6E is based on Firefox source code from the Mozilla project under the Mozilla Public License 2.0.
- **Icon:** every branding image is generated from `M6E-Icon.png` by `tools/m6e/branding/make-icons.sh`. To change the icon, replace the artwork and rerun the script.
- **macOS look (D15):** a neutral, Safari-like grey toolbar with a white selected tab, and accent colours that follow the macOS system accent.
- **No fox artwork:** Firefox's fox mascot (Kit), fox illustrations and Firefox logos are replaced by the M6E icon everywhere they appear: popups, error pages, feature callouts, panels, the sidebar, New Tab and onboarding.
- **Home page:** the M6E splash illustration (`home_page_splash.png` in the repository root) sits above the Google search box, in place of the logo and wordmark, and scales with the window.
- **No Internet page:** when a page can't load because there is no internet connection, the error page shows the M6E "No Internet Connection" illustration (`no_internet.png` in the repository root) above the message, sized to the window. Other errors keep the M6E icon.

### Phase 3: Features and privacy (done)
- **Search engines:**
  - Google is the default and Bing the only alternative. Every other engine is removed from the bundled search configuration.
  - Mozilla partner codes are removed from search URLs (D4), and users can't add engines (D5).
- **Bookmarks:** keep the bookmark manager and importing from other browsers. Add an on/off setting for the bookmarks toolbar in Settings.
- **Reading Mode:** modify the existing Reader View in place:
  - **Trigger:** user-toggleable on an article or page.
  - **Clutter:** strip sidebars, navigation, ads and non-essential iframes.
  - **Typography:** base text 25% larger.
  - **Colours:** strict black (`#000000`) on white (`#FFFFFF`), with no background images or page colours.
  - **Images:** keep only essential inline article images.
- **Privacy:**
  - Arkenfox-style hardening defaults, with Fingerprinting Protection rather than full fingerprinting resistance (D18).
  - A prominent "Privacy Shield" section in Settings (`about:preferences`, not a new page). Its master toggle sets the browser's default behaviour for every site (D20):
    - **On (default):** trackers and ads are blocked and the privacy hardening applies. A "Maximum protection" option adds full fingerprinting resistance.
    - **Off:** no tracker blocking on any site, which is Firefox's per-site "protections off" applied everywhere and includes cookie isolation. The hardening that can't apply per site is at Firefox's values.
    - **Overrides:** the Tracker Blocker's per-site switch overrides this default in either direction.
- **Push notifications:** must work reliably, with native macOS notifications (D12).
- **Tracker Blocker button (D19):**
  - **Placement:** a small button at the left of the address bar.
  - **Icon:** shows how many trackers and other privacy invasions have been blocked on the current tab. The state is always visible:
    - **Enabled:** the active Tracker Blocker icon with the number of blocked items.
    - **Disabled:** a distinct, inactive icon with no number.
  - **Panel:** clicking the button opens a dropdown panel that lists what was blocked: trackers, ads, social trackers, fingerprinters, cryptominers, cross-site cookies and other privacy threats, each with the blocked domains. The panel has a toggle switch that turns the Tracker Blocker on or off for the current site.
  - **Per-site switch (D20):** it overrides the global Privacy Shield for the current site, in either direction.
    - **Shield on:** turn protection off for an important site that breaks.
    - **Shield off:** turn protection on for a high-risk site.
    - **Duration:** the choice lasts until M6E quits (session only).
    - **What it covers:** everything the Shield covers that can apply per site: tracker and ad blocking (every category), fingerprinting protection (and full fingerprinting resistance in Maximum mode), and HTTPS-Only.
    - **Global only:** TLS options, punycode display, WebRTC IP handling, PDF scripting, referrer trimming, GPC and window move or resize. The engine applies these per connection or browser-wide, not per site.

- **Page telemetry button (added 2026-10-23, D29):**
  - **Placement and look:** a small button beside the Tracker Blocker button and the address bar, with a telemetry (gauge) icon and the tooltip "Telemetry".
  - **Panel:** clicking it opens a small panel with accurate figures for the page in the current tab:
    - TTFB (time to first byte);
    - FCP (first contentful paint);
    - TTI (time to interactive);
    - page size;
    - script parse time;
    - script execution time;
    - TLS/SSL handshake time.
  - **Local only:** the figures are measured on the device for the user and are never sent anywhere. M6E has no data collection (Phase 1).

### Phase 4: Performance
- Apply compile-flag optimisations first: LTO, the optimisation levels (`-O3`, Rust `opt-level = 3`) and Apple Silicon CPU tuning.
- Rewrite JavaScript in Rust (native or Wasm) only where profiling shows a hotspot.

### Phase 4: Platform removal, dormant features and security hardening (added 2026-10-23)
The user added these before Phase 4 can finish:
- **Platform removal (D21):** remove everything specific to Windows, Linux and Android, including `#ifdef` branches in shared code. Check each removal for dependencies first, and keep it only if the build and tests pass.
- **Security hardening (D22):** harden M6E as far as possible. This covers:
  - merging upstream security fixes;
  - reducing the attack surface that web content and the network can reach;
  - compiler and runtime hardening;
  - sandbox and process isolation;
  - a security review of every M6E change;
  - static analysis and a review of first-party code that parses untrusted input;
  - fixing what is found.
- **Dormant features (D23):** delete each dormant Mozilla feature if the build and tests pass without it. Anything that can't be deleted must be shown to be unreachable remotely: no listening sockets, no network endpoints, no entry points from web content or IPC, and locked prefs.
- **Testing:** test thoroughly when this is done, then push to GitHub.

### Phase 5: Packaging
- Produce `M6E.app` and a `.dmg` for Apple Silicon, signed ad hoc (D8), and run the final acceptance checks.
- **Release artifact:** `M6E-<version>-macos-arm64.dmg`, made by `tools/m6e/package/release.sh`: a window with an "Install M6E" background, the app on the left and the Applications link on the right. The app is signed ad hoc (D8), and its Info.plist carries no developer paths and none of Mozilla's helper identities.
- **Acceptance** (`tools/m6e/acceptance.py`, on the packaged, signed app with a fresh profile): the 15 checks listed in the plan, including: only the 8 allowed hosts while idle; Dark Mode leaves the browser UI, including the PDF viewer, light; search is Google and Bing only; the removed features are absent; the Phase 3 features work; the blocked test domains of D25 fail without native DNS; and startup and memory are at least as good as the stock Nightly baseline.
- **Maintenance:** upstream merges follow the procedure in the plan (P5.3), with `tools/m6e/merge/` to filter the diff and check the result.

## 4. Decision log

Decisions are numbered so the plan and commits can refer to them. Unless marked "agent", the user made or confirmed each one on 2026-10-22.

| # | Decision | Why |
|---|---|---|
| D1 | Interface in en-US only, with no language switching; ship an en-GB spell-check dictionary (SCOWL, the same release as en-US). | The tree only contains en-US strings; en-GB strings would need upkeep for every string change. |
| D2 | Keep Remote Settings for security data only. A hard-coded allowlist (`Utils.isAllowedCollection()`) allows the `security-state` and `blocklists` buckets plus tracking-protection and privacy collections. Everything else, notably the search configuration, uses bundled data and never syncs. | Keeps certificate revocation and the add-on blocklist current, and keeps the server from restoring removed search engines. |
| D3 | Remove or turn off AI features: the AI window, chatbot, Translations, ML features. | Privacy; the features depend on Mozilla services. |
| D4 | Strip Mozilla partner and attribution codes from Google and Bing URLs. | Searches shouldn't identify the browser as Firefox to the search provider. |
| D5 | Users can't add search engines (OpenSearch discovery, custom engines or extension engines). | The spec allows Google and Bing only. |
| D6 | App identity: `M6E.app`, `org.m6e.browser`, executable `m6e`, vendor M6E, its own profile folder. | M6E never shares a profile with an installed Firefox. |
| D7 | Hide Mozilla support links; blank `app.support.baseURL`. | No Mozilla URLs in the UI. |
| D8 | Ad-hoc code signing, hardened runtime on, with Mozilla's release entitlements minus the keys that need an Apple provisioning profile (`tools/m6e/package/sign-adhoc.sh`). The app is signed after packaging and the DMG is built from the signed app (`tools/m6e/package/release.sh`). A downloaded copy needs **Open Anyway** once in System Settings (steps in the plan, P5.1). Notarization only with an Apple Developer ID. | No developer account (US$99 a year), and spending money is the user's decision. Ad-hoc signing is the minimum that lets an arm64 app launch. |
| D9 | Keep the Nightly 160.0a1 snapshot as the base. Security fixes arrive only through manual upstream merges. | The tree was already built on it; switching would mean re-importing. |
| D10 | Keep DRM: Widevine and OpenH264 download automatically, from Google and Cisco directly, using the bundled download lists rather than Mozilla's update server. | The user wants DRM sites (streaming) to work. |
| D11 | DNS over HTTPS on by default (Increased Protection, mode 2), user-adjustable in Settings. The default resolver is Cloudflare for Families (security) (D25, 2026-10-24); the region-based rollout is removed. | Privacy by default, with a fallback for networks where DoH fails. |
| D12 | Web Push must work well and uses Mozilla's push service, the one Mozilla service besides D2. | Site notifications have no other practical relay. Messages are end-to-end encrypted. |
| D13 (agent) | Sync, VPN, telemetry/Glean, Normandy, AI/ML and Translations code stays built but dormant behind locked switches, rather than deleted. | Dozens of modules import them; deletion would cause runtime failures with no user-visible benefit. |
| D14 (agent) | Keep Firefox's internal `MOZ_APP_ID`. | Add-ons, dictionaries and language packs signed for Firefox stay compatible. It's never shown to users. |
| D15 | macOS look: the neutral toolbar and the macOS accent colour only, not compact density or translucency. | The user's choice in P2.5. |
| D16 | Remove `about:mozilla` and `about:rights` (which only redirected to Mozilla's online terms), and Mozilla's default bookmarks. | No Mozilla content. |
| D17 | Keep the build toolchain on Homebrew (pinned), with `mach bootstrap` disabled. | The network's TLS inspection (Cloudflare Gateway) breaks `mach bootstrap` downloads. |
| D18 | Hardening defaults use strict Enhanced Tracking Protection, which includes Fingerprinting Protection, rather than full fingerprinting resistance (`privacy.resistFingerprinting`). Full resistance is part of the Privacy Shield's strict mode. | Full resistance spoofs the time zone to UTC, letterboxes windows and breaks more sites. The gentler protection blocks most fingerprinting without that. |
| D19 (agent) | The Tracker Blocker builds on Firefox's tracking protection icon and protections panel rather than a new widget. The count covers the current tab's page. | One blocking engine and one place to see what was blocked. |
| D20 | The global Privacy Shield sets the default for every site, and the Tracker Blocker's per-site switch overrides it both ways. With the Shield on, it turns protection off for an important site that breaks. With the Shield off, it turns protection on for a high-risk site. Shield off means no tracker blocking, which also lifts cookie isolation, as Firefox's per-site off does (agent: implemented through the content blocking allow list). Per-site choices last for the session only. The switch covers everything the Shield covers that the engine can apply per site. | The user's model (2026-10-23): one global default, with per-site exceptions in either direction. Global-only hardening is an engine limit. |
| D21 | Remove all Windows, Linux and Android code, including `#ifdef` branches in shared source. This supersedes Phase 1's rule to leave them. In first-party code, branches are stripped with `unifdef`. In vendored third-party code, whole platform-only files and directories are deleted, but `#ifdef`s inside vendored libraries stay (agent; see the note). | The user's choice (2026-10-23). Note (agent): stripping `#ifdef`s doesn't change the macOS binary, because the compiler already discards them. Inside vendored libraries (libvpx, ffmpeg, NSS, SQLite and others, frequent sources of security fixes), the edits would have to be redone on every security update, which works against D22. |
| D22 | Security hardening is in scope "everything under the Sun": upstream security merges, attack-surface reduction, compiler hardening, sandbox checks, a review of M6E's changes, static analysis and a review of first-party parsers, plus fixes. | The user's choice (2026-10-23): a security-hardened browser. |
| D23 | Dormant Mozilla features are deleted when the build and tests pass without them. Whatever stays must be shown to be unreachable remotely. This supersedes D13. | The user's choice (2026-10-23). |
| D24 (agent) | During the P4.5 to P4.8 source work, builds run without PGO (LTO stays on) so each full rebuild takes 15 to 40 minutes instead of 2 hours. PGO comes back for the final measured and packaged build. | Many full rebuilds are needed. PGO triples each one and only affects speed, not correctness. |
| D25 | Malware and phishing protection comes from the DNS resolver: **Cloudflare for Families (security)**, `https://security.cloudflare-dns.com/dns-query`, which refuses to resolve known malware and phishing domains. It is free, needs no account or API key, and has no non-commercial restriction. M6E does not use Google Safe Browsing: it would need an API key licensed only for non-commercial use, and it would send hash prefixes to Google.<br>• Gecko treats the resolver's block answers (`0.0.0.0` with extended DNS error 15 Blocked or 16 Censored) like errors 4 and 17: no fallback to the Mac's ordinary DNS, so a blocked site stays blocked and shows "can't find this site" (`netwerk/dns/DNSPacket.cpp`, marked M6E).<br>• The Settings list of DNS providers offers it as "Cloudflare for Families (security)".<br>• **Limits:** blocking works per domain, not per URL; downloads aren't checked; and when DoH itself is unavailable, mode 2 falls back to ordinary DNS without filtering.<br>• Users can still choose another resolver in Settings (D11). | The user's choice (2026-10-24, replacing the deferred Google Safe Browsing plan): M6E is to be free for everyone to use, without Google services. |
| D26 | Add-on updates stay off. Installed add-ons are maintained by hand, together with the codebase. | The user's choice (2026-10-23). No automatic contact with addons.mozilla.org. |
| D27 | WebGPU stays available. | The user's choice (2026-10-23): compatibility with GPU-heavy web apps. |
| D28 | The JavaScript and Wasm JITs stay on, hardened around them: experimental JS and Wasm features are off and SpiderMonkey's built-in JIT hardening is kept. | The user's choice (2026-10-23): disabling the JIT would undo Phase 4's speed gains. |
| D29 (agent) | How the page telemetry figures are measured:<br>• **TTFB:** `responseStart` of the page's navigation.<br>• **FCP:** the Paint Timing entry.<br>• **TTI:** Gecko's own time-to-first-interactive, the first 5-second window after content paints with no long tasks.<br>• **TLS handshake:** `connectEnd − secureConnectionStart`, or "connection reused" when no new handshake took place.<br>• **Page size:** bytes transferred by all of the page's requests (document, subframes and subresources, including cross-origin ones), counted in the parent process from the network channels, with the number of requests and how many came from the cache. The parent only sees bytes on the wire, because responses are decompressed in the content process.<br>• **Script parse and execution time:** SpiderMonkey's timers for the page's main realm while it loads. Execution comes from the existing `JSTimers`. Parse is a new timer covering main-thread and off-thread compilation. | Each figure comes from the engine's own measurement rather than an estimate. Web pages' own Resource Timing hides cross-origin sizes, so the size has to be counted in the parent. |
| D30 | The remaining dormant features are marked for deletion, to be removed after all other phases are complete: the VPN (IP protection: `browser/components/ipprotection`, `toolkit/components/ipprotection`, `browser/extensions/ipp-activator`), Translations, Sync and Mozilla accounts, the updater (`toolkit/mozapps/update`), AI/ML (`toolkit/components/ml`, `browser/components/genai`, `aiwindow`), the crash reporter, and Nimbus experiments. Until then, P4.8 proves each one unreachable: locked prefs, no listening sockets, no network traffic, and no entry points from web content or IPC. | The user's choice (2026-10-23): a full removal takes about 2 days of build and test cycles, and time is short. Normandy and the platform-only code are already deleted (P4.6, P4.7a, P4.7b). |

## 5. Ground rules
- **Repository:** work in the existing checkout; one commit per plan step, pushed to `origin/main` after a successful build and checks.
- **Behaviour prefs:** all M6E prefs live in `browser/app/profile/m6e.js` (included at the end of `firefox.js`). M6E visual overrides live in `toolkit/themes/shared/m6e-tokens.css`. Keep other source changes small and marked `M6E:`, so upstream merges stay manageable.
- **Build cost:** configure-level changes (mozconfig, `moz.configure`) rebuild the whole tree in 40 minutes to 2 hours; batch them. Front-end changes only need `./mach build faster`.
