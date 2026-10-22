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

## 2. Status (2026-10-23)

| Phase | Scope | Status |
|---|---|---|
| 0 | Toolchain, mozconfig, baseline build and measurements | Done |
| 1 | Debloating: platforms, languages, themes, Mozilla features and services | Done |
| 2 | Rebranding and macOS UI | Done |
| 3 | Search engines, bookmarks, Reader View, privacy hardening, Privacy Shield, push | In progress: P3.1 search done; next, an in-app restart fix, then P3.2 bookmarks |
| 4 | Performance | Not started |
| 5 | Packaging (`.dmg`) and acceptance | Not started |

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
  - the DNS-over-HTTPS resolver (D11);
  - Google and Cisco, for the DRM and video-call plugins (D10).

  Mozilla is contacted for exactly two things: Web Push and the Remote Settings security allowlist.

### Phase 2: Rebranding and UI (done)
- **Branding:** use Firefox's branding mechanism (`browser/branding/m6e`), not a blind text replace.
  - "Firefox" and "Mozilla" must not appear in anything user-visible (UI, app bundle, `about:` pages, displayed URLs).
  - Keep `LICENSE`, the MPL headers and `AUTHORS` intact, and keep internal identifiers such as `MOZ_` prefixes and the app ID (D14).
- **Attribution:** the About dialog and `about:license` state that M6E is based on Firefox source code from the Mozilla project under the Mozilla Public License 2.0.
- **Icon:** every branding image is generated from `M6E-Icon.png` by `tools/m6e/branding/make-icons.sh`. To change the icon, replace the artwork and rerun the script.
- **macOS look (D15):** a neutral, Safari-like grey toolbar with a white selected tab, and accent colours that follow the macOS system accent.

### Phase 3: Features and privacy (in progress)
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
  - Arkenfox-style hardening defaults.
  - A prominent "Privacy Shield" section in Settings (`about:preferences`, not a new page), whose master toggle switches strict privacy (fingerprinting resistance, strict blocking) off when a site breaks.
- **Push notifications:** must work reliably, with native macOS notifications (D12).

### Phase 4: Performance
- Apply compile-flag optimisations first: LTO, the optimisation levels (`-O3`, Rust `opt-level = 3`) and Apple Silicon CPU tuning.
- Rewrite JavaScript in Rust (native or Wasm) only where profiling shows a hotspot.

### Phase 5: Packaging
- Produce `M6E.app` and a `.dmg` for Apple Silicon, signed ad hoc (D8), and run the final acceptance checks.

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
| D8 | Ad-hoc code signing for local use; notarization only with an Apple Developer ID. | No developer account yet. |
| D9 | Keep the Nightly 160.0a1 snapshot as the base. Security fixes arrive only through manual upstream merges. | The tree was already built on it; switching would mean re-importing. |
| D10 | Keep DRM: Widevine and OpenH264 download automatically, from Google and Cisco directly, using the bundled download lists rather than Mozilla's update server. | The user wants DRM sites (streaming) to work. |
| D11 | DNS over HTTPS on by default (Increased Protection, Cloudflare), user-adjustable in Settings; the region-based rollout is removed. | Privacy by default, with a fallback for networks where DoH fails. |
| D12 | Web Push must work well and uses Mozilla's push service, the one Mozilla service besides D2. | Site notifications have no other practical relay. Messages are end-to-end encrypted. |
| D13 (agent) | Sync, VPN, telemetry/Glean, Normandy, AI/ML and Translations code stays built but dormant behind locked switches, rather than deleted. | Dozens of modules import them; deletion would cause runtime failures with no user-visible benefit. |
| D14 (agent) | Keep Firefox's internal `MOZ_APP_ID`. | Add-ons, dictionaries and language packs signed for Firefox stay compatible. It's never shown to users. |
| D15 | macOS look: the neutral toolbar and the macOS accent colour only, not compact density or translucency. | The user's choice in P2.5. |
| D16 | Remove `about:mozilla` and `about:rights` (which only redirected to Mozilla's online terms), and Mozilla's default bookmarks. | No Mozilla content. |
| D17 | Keep the build toolchain on Homebrew (pinned), with `mach bootstrap` disabled. | The network's TLS inspection (Cloudflare Gateway) breaks `mach bootstrap` downloads. |

## 5. Ground rules
- **Repository:** work in the existing checkout; one commit per plan step, pushed to `origin/main` after a successful build and checks.
- **Behaviour prefs:** all M6E prefs live in `browser/app/profile/m6e.js` (included at the end of `firefox.js`). M6E visual overrides live in `toolkit/themes/shared/m6e-tokens.css`. Keep other source changes small and marked `M6E:`, so upstream merges stay manageable.
- **Build cost:** configure-level changes (mozconfig, `moz.configure`) rebuild the whole tree in 40 minutes to 2 hours; batch them. Front-end changes only need `./mach build faster`.
