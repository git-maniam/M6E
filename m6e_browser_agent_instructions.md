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

## 2. Status (2026-11-04)

| Phase | Scope | Status |
|---|---|---|
| 0 | Toolchain, mozconfig, baseline build and measurements | Done |
| 1 | Debloating: platforms, languages, themes, Mozilla features and services | Done |
| 2 | Rebranding and macOS UI | Done |
| 3 | Search engines, bookmarks, Reader View, privacy hardening, Privacy Shield, push, Tracker Blocker button | Done |
| 4 | Performance, platform removal, dormant-feature removal, security hardening | Done (2026-10-24). Speedometer 3 is about +28% over the Phase 3 build; Windows, Linux and Android code removed; hardening as P4.8; the dormant features marked for deletion in P6.1 (D30). |
| 5 | Packaging (`.dmg`) and acceptance | Done (2026-10-24). Signed ad hoc, release DMG built and verified, acceptance 15 of 15; first paint 579 ms, blank-tab memory 443 MB. |
| 6 | Delete the dormant features (D30) | Done (2026-10-30). VPN, Translations, generative AI, machine learning, Sync, Mozilla accounts, the application updater, the crash reporter and Nimbus are deleted (Nimbus leaves a small stub); what stays is proven unreachable (`tools/m6e/p6-reports/unreachability.md`, `tools/m6e/unreachability_check.py` 7 of 7 on the packaged app). Clean LTO and PGO build, regression suites match the baseline, acceptance 15 of 15, Speedometer 3 45.5 (Phase 5: 44.7), blank-tab memory 430 MB, first paint 606 to 624 ms on a quiet machine (Phase 5: 579 ms), release DMG 106.5 MiB. Optional C++ follow-ups are on `m6e-p7-optional/cpp-followups`. |
| 161.0a2 | Fingerprinting policy for standard mode (D31) and the documented passkey limit (D32) | **Planned, not yet built or published.** The current release is 161.0a1. The release is a pref change plus documentation, with a version bump that needs a clean LTO and PGO rebuild. |
| 8 | BitTorrent: the "BT" button, torrent panel, `.torrent` and `magnet:` handling, and the "Shady Torrent" warning (D33 to D40) | **In progress on branch `BT-Feature`** (2026-11-10). The five Wave A branches are merged. The engine is chosen (libtorrent-rasterbar, D39). The Node tests for the risk rules (179 cases) and the protocol module (23) pass. The helper is under way. The dev build, the xpcshell tests and the browser tests have not passed yet. Ships in a release after 161.0a2. Requirements are in Phase 8 below. |

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

  BitTorrent (Phase 8) adds no traffic to this list: the torrent engine does not run, listen or connect until the user adds a torrent (D36). Torrent traffic goes to trackers and peers, not to a fixed host list.

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
  - Arkenfox-style hardening defaults.
  - **Fingerprinting (D18, D31):** known third-party fingerprinters (Firefox's list) are blocked in standard and Maximum mode while the Privacy Shield is on, except on sites switched off in the Tracker Blocker; with the Shield off nothing is blocked (D20). With the Shield on and Maximum off ("standard mode"), Firefox's Fingerprinting Protection no longer applies to any site, first-party sites included, so fewer sites are affected (planned for 161.0a2; 161.0a1 still applies it). Maximum protection applies full fingerprinting resistance to all sites.
  - A prominent "Privacy Shield" section in Settings (`about:preferences`, not a new page). Its master toggle sets the browser's default behaviour for every site (D20):
    - **On (default):** trackers and ads are blocked, known third-party fingerprinters are blocked and the privacy hardening applies. A "Maximum protection" option adds full fingerprinting resistance for all sites (the UTC time zone, a spoofed user agent, screen size and locale, and the other targets of Firefox's `privacy.resistFingerprinting`; window letterboxing is not enabled) (D31).
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
    - **What it covers:** everything the Shield covers that can apply per site: tracker and ad blocking (every category, including known third-party fingerprinters), full fingerprinting resistance in Maximum mode, and HTTPS-Only.
    - **Shield off, site on:** the site gets tracker and known third-party fingerprinter blocking and HTTPS-Only, but no first-party fingerprinting protection; that exists only as Maximum protection, which needs the Shield on (D31).
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

### Known limitations (D32)
- **Passkeys:** platform passkeys (Touch ID, iCloud Keychain) and the iPhone QR code flow do not work; only USB FIDO security keys do (D32).
- **DRM:** Widevine L3 (software) is installed (D10). DRM video works through it, but some streaming services may not play every title. The cause of the failures reported so far is under investigation and no fix is claimed.

### Phase 5: Packaging
- Produce `M6E.app` and a `.dmg` for Apple Silicon, signed ad hoc (D8), and run the final acceptance checks.
- **Release artifact:** `M6E-<version>-macos-arm64.dmg`, made by `tools/m6e/package/release.sh`: a window with an "Install M6E" background, the app on the left and the Applications link on the right. The app is signed ad hoc (D8), and its Info.plist carries no developer paths and none of Mozilla's helper identities.
- **Acceptance** (`tools/m6e/acceptance.py`, on the packaged, signed app with a fresh profile): the 15 checks listed in the plan, including: only the 8 allowed hosts while idle; Dark Mode leaves the browser UI, including the PDF viewer, light; search is Google and Bing only; the removed features are absent; the Phase 3 features work; the blocked test domains of D25 fail without native DNS; and startup and memory are at least as good as the stock Nightly baseline.
- **Maintenance:** upstream merges follow the procedure in the plan (P5.3), with `tools/m6e/merge/` to filter the diff and check the result.

### Phase 8: BitTorrent (added 2026-11-04, D33 to D37)
Adapted from a feature specification for a standalone uTorrent-style client (the "source spec", removed from the repository once this section replaced it; it remains in git history). What was cut, and why, is in 8.6.

**Goal:** a small, private BitTorrent client inside M6E. A "BT" button beside the address bar opens a panel with the torrent controls. M6E can open `.torrent` files and `magnet:` links. A torrent with risky traits gets a "Shady Torrent" warning, which informs the user and never blocks. Nothing runs, listens or connects until the user adds a torrent (D36). **Testing is restricted to the two official torrent pages in 8.9 (D38); read it before running any torrent.**

#### 8.1 The BT button, panel and pages (D33)
- **Placement and look:** a small button beside the Tracker Blocker and Telemetry buttons at the address bar, showing the letters "BT" as its icon, with the tooltip "BitTorrent". Build it the way the Page telemetry button is built (D29): `browser/base/content/browser-m6eTorrent.js`, a stylesheet and SVG next to `m6e-page-telemetry.css`, and strings in `browser/locales/en-US/browser/m6eTorrent.ftl`.
- **States:** idle (plain icon); active (a badge with the number of downloading or seeding torrents); attention (a torrent has an error).
- **Panel:** clicking the button opens a drop-down panel with:
  - a header with the total download and upload speed;
  - **Add torrent file…** and **Add magnet link…** (a paste field);
  - the torrent list, one compact row each: name, progress bar, speed, ETA and state, with pause or resume, **Show in Finder** and remove (a choice to also delete the files). Clicking a row opens it in `about:torrents`. An empty list shows a short hint;
  - **Pause all / Resume all**;
  - **Turtle mode**, a switch for the alternative speed limits;
  - quick settings: download and upload limits (KB/s, 0 is unlimited), encryption (Disabled, Enabled, Forced) and the seeding goal (stop at a ratio, after hours, or never);
  - **BitTorrent settings…**, which opens the Settings section.
- **Add dialog:** every torrent, from a file, a magnet link or Finder, opens an "Add torrent" dialog window before any file data is downloaded:
  - name, total size and a file tree with checkboxes and a priority per file (High, Normal, Low, Do not download), hiding `.pad` files;
  - the save location (default: the browser's download folder) and a free-space check that warns when the selected files will not fit;
  - a **Sequential download** checkbox and a **Start now** checkbox;
  - the "Shady Torrent" banner when 8.3 says so;
  - **Add** and **Cancel**.
- **`about:torrents`:** a page for the full view, a later step (P8.6). A sidebar filters by All, Downloading, Seeding, Completed, Active, Inactive and Errored. The table has the columns Name, Size, Done, Status, Down, Up, ETA, Ratio, Seeds/Peers and Added. The detail pane has the tabs General (transfer figures, hash, piece size, path), Trackers, Peers (address, client, encryption, speeds, progress), Pieces (a map of downloaded, verified and in-flight pieces), Files (priorities and progress), Speed (a live graph) and Log.
- **Settings:** a "BitTorrent" section in `about:preferences`, next to the Privacy Shield (not a new page). It holds the panel's quick settings plus everything in 8.4 and 8.5, and the "default app" button of 8.2.
- **Off switch:** the pref `m6e.torrent.enabled` (default true; Settings has "Show the BT button") hides the button and keeps the engine from ever starting.

#### 8.2 Opening torrents (D33)
- **Registration:** M6E's `Info.plist` (template: `browser/app/macbuild/Contents/Info.plist.in`) declares the document type `.torrent` (`application/x-bittorrent`) and the URL scheme `magnet`. M6E does not make itself the default silently. Settings has a button "Make M6E the default app for .torrent files and magnet links" (Launch Services); Finder's Open With works without it.
- **Inside M6E:** a link to a `.torrent` file, a download whose type is `application/x-bittorrent`, and a navigation to a `magnet:` link all open the Add dialog instead of saving the file or asking to open an external app. Use Gecko's handler service for this, and look at how the tree handles external protocols now before deciding where to hook in. The page the link came from is recorded for the source check in 8.3.
- **From Finder or another app:** the same Add dialog, with source "unknown".
- **Go to the torrents page (D40):** whenever a `.torrent` file is downloaded or opened in M6E (a link, a download of type `application/x-bittorrent`, a file opened from Finder, or a `magnet:` link), M6E first brings the user to the torrent page (`about:torrents`, 8.1): it selects the existing tab or opens one in the current window, then shows the Add dialog over it. After **Add**, the new torrent is selected in the page's list; after **Cancel**, the page stays open. The `.torrent` file type is associated with M6E (this section's registration), so a double-click in Finder also lands on the page.
- **No silent adds:** a web page can never add or start a torrent. Only a user click and the Add dialog's **Add** do.
- **Magnet links:** the Add dialog opens at once in a "Fetching torrent information…" state. Only the metadata (BEP 9) is fetched. The file tree and the 8.3 check appear when it arrives, and no file data is downloaded until the user clicks **Add**. **Cancel** drops the metadata fetch.
- **Duplicates:** a torrent that is already in the list is not added again; its row is shown.
- **Parsing hostile input:** reject a `.torrent` larger than 16 MiB, with bencoding nested deeper than a fixed limit, or with any file path that is absolute or has a `..`, empty or NUL component. Never write outside the chosen folder, and never follow a symlink out of it. A malformed torrent shows an error and is not added. These are safety limits, not the Shady warning.

#### 8.3 The "Shady Torrent" warning (D35)
- **What it is:** an informational banner at the top of the Add dialog, headed **Shady Torrent**, with a line such as "This torrent has traits that are common in malicious or misleading downloads", the reasons found (each with the file names concerned), and the buttons **Cancel** and **Add anyway**. It never blocks and never adds anything by itself. A torrent with no such traits, or from a trusted source, shows no banner and no extra step. M6E makes no judgement about copyright.
- **Offline and explainable:** the check runs on the torrent's metadata and where it came from, on the device. It does not query any reputation service (no data leaves the device, D2). Every reason is shown to the user in words.
- **Where it lives:** `browser/components/torrent/M6ETorrentRisk.sys.mjs`, a pure function from the parsed metadata and the source to `{ verdict: "ok" | "shady", reasons: [{ code, severity, files }] }`. The rules and trusted sources are bundled data (`browser/components/torrent/m6e-torrent-rules.json`) so they can be tuned without code changes. Cover it with xpcshell tests.
- **Trusted source (no banner):** the `.torrent` was downloaded, or the `magnet:` link was clicked, on a page whose origin is **https** and belongs to a site in the bundled trusted list. The source is the strong signal because the connection is authenticated. A tracker address or a torrent name alone is never enough, because anyone can copy those into a malicious torrent. The initial list covers the official download pages of well-known Linux and BSD distributions (Debian, Ubuntu, Fedora and the Red Hat family: Rocky Linux, AlmaLinux, CentOS; plus Arch, openSUSE, Linux Mint, Tails and FreeBSD) and of open-source software projects that publish release torrents (for example Blender and the Wikimedia dumps). The implementer must check each entry against the project's own download page, and drop any project that does not publish torrents (Red Hat's own site, for example, may not). Do not add general hosting or sharing sites such as the Internet Archive, because anyone can upload to them. The user can add a site with **Always trust torrents from this site** in the Add dialog (stored in prefs). The list changes only with M6E releases (no Remote Settings, D2).
- **Reasons, for a torrent not from a trusted source:**
  - *High* (one is enough for **Shady Torrent**):
    - a Windows program or script: `.exe .scr .com .pif .bat .cmd .vbs .vbe .jse .wsf .hta .msi .ps1 .cpl .lnk`;
    - a double extension that disguises a program (`Movie.mkv.exe`), or a name with right-to-left override or other invisible characters;
    - file names that advertise piracy tools: `crack`, `keygen`, `patch`, `activator`, `loader`, `serial`;
    - a program or archive inside a torrent that is otherwise named and filled like video or audio.
  - *Medium* (two are needed): a macOS installer or program (`.dmg .pkg .app .command .osax`), `.jar` or `.sh`; a `.url` or `.webloc` shortcut; a file named like `password.txt` or `readme` placed next to an archive.
- **The user's examples:** a Debian or Fedora ISO torrent from its project's site is a trusted source, so no banner. The same ISO opened from Finder has one large `.iso` and none of the traits above, so it also gets no banner. A torrent called "Movie 2025 1080p" holding `Movie.mkv.exe` gets **Shady Torrent**.
- **Remembered choice:** after **Add anyway** the warning is not shown again for that torrent in the session.

#### 8.4 Engine and protocols (D34)
- **Required for the first version:**
  - BEP 3 (peer wire protocol, HTTP trackers, SHA-1 piece verification), BEP 15 (UDP trackers), BEP 9 and BEP 10 (magnet metadata and the extension protocol), BEP 53 (select files in a magnet link);
  - BEP 5 (DHT) and BEP 11 (peer exchange);
  - BEP 27 (private torrents): DHT, peer exchange and local discovery are off for the torrent for good;
  - message stream encryption (Disabled, Enabled, Forced), IPv4 and IPv6, rarest-first and end-game piece picking, rate limits for download and upload (global, per torrent, and Turtle mode), selective files with priorities, sequential download, sparse files, a queue (maximum active downloads and torrents) and seeding goals (ratio or hours, then stop or use the alternative upload limit).
- **Wanted, after the first version:** BEP 29 (uTP, with LEDBAT congestion control), BEP 19 (web seeds), BEP 52 (BitTorrent v2 and hybrid torrents, SHA-256 Merkle trees) and BEP 14 (local peer discovery, off by default, D36).
- **Engine chosen (2026-11-10, D39): libtorrent-rasterbar 2.1.2** (BSD-3-Clause, C++, static, built offline from the pinned release tarball). It covers every Required and all four Wanted items. Helper-side code is needed only for seeding goals, Turtle mode, the IP-filter file parsers and the interface kill-switch watchdog. Built without TLS at first (no `https://` trackers or web seeds; the Homebrew OpenSSL targets macOS 26, M6E targets 11.0), without WebTorrent and I2P, and with DHT, local discovery, UPnP and NAT-PMP off and no listen socket until a torrent is confirmed. librqbit was rejected (no encryption, no rarest-first); anacrolix/torrent is the fallback.
- **Engine choice (spike P8.0):** pick an existing, maintained engine. Do not write a protocol stack. Candidates: libtorrent-rasterbar (C++, BSD), librqbit (Rust, Apache-2.0) and anacrolix/torrent (Go, MPL-2.0). Rule: the engine must cover every "Required" item (or the gaps must be small and listed), have a licence that can ship in an MPL-2.0 product, and build for arm64 macOS offline with the pinned Homebrew toolchain (D17); among those, prefer the one covering most "Wanted" items, then the smaller memory use. Read each project's documentation to check coverage and record the result in the plan.
- **Process model:** the engine runs in its own helper executable, `m6e-torrent`, inside `M6E.app`, not inside the browser process. Reasons: a crash or exploit stays out of the browser, memory returns to the system when idle, and the Gecko build (a 40-minute to 2-hour rebuild) is untouched.
  - **Source and build:** `tools/m6e/torrent/` (helper source and `build-helper.sh`), not built by `./mach build`. The script puts the binary into the app, and `tools/m6e/package/finalize-app.sh` and `sign-adhoc.sh` must include and sign it (D8) with the hardened runtime.
  - **Control:** the browser starts it with `Subprocess.sys.mjs` from a parent-process service (`M6ETorrentService.sys.mjs`, in `browser/components/torrent/`), and talks to it over its standard input and output, one JSON message per line. There is no control socket and no web server. Messages include add (file or magnet), set file priorities, start, pause, remove, list, set limits, set config and shut down; events include metadata received, progress (batched once a second), tracker and peer changes, completion and errors.
  - **Lifetime:** started on the first need; it exits 60 seconds after no torrent is active, and always when M6E quits. There is no background service after quit. Quitting with active torrents asks for confirmation, like active downloads.
  - **Confinement:** try to run it under a macOS sandbox profile that allows only network access and the chosen download folders plus the profile's `torrents/` folder, with no process execution. If per-torrent folders make that impractical, confine writes in code instead (canonical path under the torrent's root) and record the gap in the plan.
- **State:** resume data and the session list are kept in `<profile>/torrents/`. Torrents active when M6E quit resume at launch (setting "Resume torrents at startup", default on). A profile with no torrents has no helper and no activity.
- **Disk:** sparse files (APFS); a modest engine cache, with the operating system's file cache doing the rest. While a torrent is active, the helper prevents idle system sleep (a power assertion) and releases it afterwards.
- **Target to measure, not a promise:** no helper process at idle; with 5 active torrents, helper memory under 60 MB, excluding the file cache.

#### 8.5 Privacy and safety defaults (D36)
- **No idle activity:** with no active torrent there is no helper process, no listening socket, no DHT, no port mapping and no network traffic. The existing idle checks (the allowed hosts and `tools/m6e/unreachability_check.py`) keep passing.
- **No payload before consent:** file data is downloaded only after **Add** in the Add dialog (8.2).
- **Listening port:** a random port, chosen at each start. UPnP and NAT-PMP port mapping are off by default (they open the home router to the internet, against D22), with a setting to turn them on. Without port mapping, incoming connections arrive only if the user forwards the port by hand; outgoing connections always work.
- **Peer discovery:** DHT and peer exchange are on for public torrents and always off for private ones (BEP 27). Local peer discovery is off by default, because it announces the user on the local network (and macOS may ask for Local Network permission).
- **Identity:** a Peer ID prefix that does not name M6E or its version (setting to randomise it per session); a generic tracker User-Agent.
- **Proxy:** SOCKS5 with a username and password; the setting chooses between trackers only, or trackers and peers. Host names are resolved by the proxy (no local DNS leak). If the chosen engine cannot send DHT traffic through the proxy, DHT is switched off while peers go through it, and the panel says so (no traffic may bypass the proxy). A separate setting from the browser's own network settings, because the Privacy Shield and Tracker Blocker do not apply to torrent traffic.
- **Network interface binding and kill switch:** bind the engine to a chosen interface (for example a VPN's `utun` or `wg` device). If that interface goes away, the helper closes every torrent connection and opens none until it returns. M6E removed Mozilla's VPN (D30); this is how a user's own VPN is kept from leaking.
- **IP filter:** load a user-supplied block list (PeerGuardian `.p2p`, eMule `ipfilter.dat`) into a memory-efficient range structure; no list is bundled or downloaded.
- **Encryption:** the three modes of 8.4. The settings text says the encryption hides the protocol from simple traffic shaping; it does not make the user anonymous.
- **Files:** completed files get the macOS quarantine attribute, with the same mechanism Gecko uses for ordinary downloads (find it in the tree rather than writing a new one), so Gatekeeper checks anything the user opens. Integrity comes from hash verification before data is written as complete.
- **Seeding:** the default goal is to stop at ratio 1.0, user-adjustable. Seeding is a user-visible state in the panel.

#### 8.6 Cut from the source spec (D37)
| Source item | Why it is not in M6E |
|---|---|
| Web UI, REST API, passwords, CSRF, CORS, brute-force bans, HTTPS listener (3.4) | The UI is the browser's own panel and pages. There is no network control surface, so none of this applies. |
| Tauri, Qt or React front end, SQLite state (8) | Same reason; state is plain files in the profile. |
| System tray icon, minimize to tray (6.2) | M6E is a Dock app, and the helper stops when M6E quits. |
| RSS downloader, watch folders (7) | Background automation that a simple browser feature does not need. |
| Run external program on completion (7.2) | It would run arbitrary commands from torrent metadata or settings, against D22. |
| Antivirus integration (3.5) | macOS quarantine and Gatekeeper cover it (8.5). |
| 24/7 scheduler grid, "seeding only" and "stop all" states (4.1) | Turtle mode and manual pause cover the need. Could return later. |
| Zero-fill pre-allocation (5.1) | Sparse files plus the free-space check in the Add dialog are enough on APFS. |
| Auto-shutdown or hibernate when done (6.2) | Out of scope for a browser. |
| Windows paths and Windows tools (3.2, 3.5) | M6E is macOS only (D21). |

**Corrections to the source spec:** local peer discovery is BEP 14 (not BEP 26); BEP 53 only selects files in a magnet link, and magnet links themselves come from BEP 9; BEP 3 uses SHA-1, and SHA-256 arrives with BEP 52.

#### 8.7 Delivery steps
Each step follows the plan's step loop (build, check, commit as `M6E P8.<step>: …`).
1. **P8.0 Engine spike:** compare the candidates against 8.4, choose one, and build a bare helper that downloads a test torrent from a local tracker and seed (`tools/m6e/torrent/`, so the end-to-end tests need no internet). Record the choice and the coverage in the plan. *Done when:* the helper downloads and verifies a file offline. The fixture is generated by the harness and served on 127.0.0.1 only (D38, approved by the user on 2026-11-10).
   Status: the engine evaluations and the choice are done (D39); the helper (`build-helper.sh`, engine wrapper) and its offline download check are in progress; the first dev build has not finished.
2. **P8.1 Helper and service:** the JSON message protocol, `M6ETorrentService.sys.mjs`, lifetime rules, resume data, parsing limits (8.2). *Done when:* xpcshell tests add a torrent and a magnet link, report progress, survive a helper crash and restart, and show no helper at idle.
   Status: the protocol module and `M6ETorrentService.sys.mjs` are merged and work against a fake helper (23 Node protocol tests pass); the real helper, the confinement checks and the xpcshell tests are in progress or not started.
3. **P8.2 BT button, panel, Add dialog:** 8.1, with accessibility labels and keyboard operation. *Done when:* the panel adds, pauses, resumes and removes a torrent from the offline fixture.
   Status: merged and unverified; the mock-driven browser tests are written but have not run.
4. **P8.3 Shady Torrent:** `M6ETorrentRisk.sys.mjs`, the rules and trusted list, the banner. *Done when:* the unit tests cover every rule and the three examples in 8.3 give the stated results.
   Status: the rules and trusted list are merged; the Node runner passes all 179 cases, including the three examples; the xpcshell wrapper has not run; the banner wiring and the "Always trust torrents from this site" pref are not done.
5. **P8.4 Opening torrents:** the `Info.plist` entries, the handler hooks, the default-app button, the source recorded from the page. *Done when:* a `.torrent` link and a `magnet:` link in a page and from Finder open the Add dialog, and the page cannot start anything.
   Status: the `Info.plist` entries, handler hooks and source hand-off are merged and unverified; the default-app button is not done.
6. **P8.5 Settings and privacy:** the Settings section, limits, queue, seeding goals, encryption, proxy, interface binding, the IP filter, and the "Wanted" protocols as far as the chosen engine supports them. *Done when:* each setting has a test, and the kill switch test of 8.8 passes.
   Status: not started.
7. **P8.6 `about:torrents`:** the full page of 8.1.
   Status: not started.
8. **P8.7 Packaging and acceptance:** sign the helper, extend `acceptance.py` and `unreachability_check.py`, run the P6.3 sequence (clobber, build, tests, release, acceptance, measurements), update the README, and update this section's status.
   Status: not started.

#### 8.8 Acceptance (added to `tools/m6e/acceptance.py`, on the packaged, signed app)
1. A fresh profile left idle for 5 minutes: no `m6e-torrent` process, no new listening socket, and the allowed-host check unchanged.
2. A `.torrent` from a trusted https origin and the same file opened from Finder: no banner. A crafted loopback fixture torrent containing `Movie.mkv.exe` (generated by the harness, never fetched from outside; D38): the **Shady Torrent** banner with the reason, and **Add anyway** still works.
3. A torrent with a `../` or absolute path never writes outside the chosen folder.
4. A magnet link downloads no file data until **Add** is clicked.
5. A private torrent uses no DHT, peer exchange or local discovery.
6. With the bound interface missing, the helper opens no connection.
7. Quitting M6E leaves no `m6e-torrent` process.
8. The helper is signed with the hardened runtime and passes the post-signing checks.
9. Launch Services lists M6E as an app that can open `.torrent` files and `magnet:` links.

#### 8.9 Test torrents (mandatory, D38)
This is a hard rule for every person and every agent working on BitTorrent. It is never to be relaxed, worked around or reinterpreted. The reason: the developer's computer is an office-issued machine under security hardening, and downloading an untrusted torrent on it could breach policy and cost the developer their job.

- **Allowed test torrents.** Any torrent that is added, downloaded, seeded or contacted over a network, whether manually or by a script, must be a torrent link (a `.torrent` file or a `magnet:` link) listed on one of these two official download pages, and from no other source:
  - Fedora Linux: https://fedoraproject.org/torrents/45/
  - Debian: https://cdimage.debian.org/debian-cd/current/arm64/bt-dvd/
- **Everything else is "Shady" for testing.** A torrent or magnet link from anywhere else is off-list and must not be used, even if it looks legitimate. This includes torrents of the projects below when they are not taken from the two pages above (so a Debian or Fedora torrent from a mirror, a forum, a search result or another page is also off-list):
  - Ubuntu and its official flavors (Kubuntu, Lubuntu, Xubuntu), Debian, Linux Mint, Arch Linux, Fedora, openSUSE;
  - the specialty distributions Kali Linux (penetration testing), Tails (privacy-focused) and Raspberry Pi OS;
  - the BSD variants FreeBSD, OpenBSD and NetBSD;
  - LibreOffice, GIMP, Kiwix and the Internet Archive.
- **This is separate from the product feature.** "Shady" here is a testing policy about which torrents may be run. The "Shady Torrent" warning (8.3) is what the product shows users, and the bundled trusted-site list in 8.3 does not widen this allow-list. Nothing in 8.3, 8.7 or 8.8 permits testing with a torrent that is not on the two pages.
- **How to apply it.** Test code, fixtures, documentation and acceptance steps must not name, embed or fetch any other torrent or magnet link. An agent that needs a torrent for a test uses one of the two pages, or stops and asks the developer. Never add a torrent to a running M6E to "see if it works" outside this list.
- **Steps that need the user's decision.** The offline fixture of P8.0 (a local tracker and seed) and the crafted `Movie.mkv.exe` case in 8.8 item 2 are synthetic torrents, not taken from the two pages. They are not covered by the allow-list above, so they must not be run until the developer approves them explicitly.

**Confirmed by the user (2026-11-04):**
- The engine is chosen by the P8.0 spike under the rule in 8.4.
- "Shady" means risk of malware or deception only (8.3), with no copyright judgement.
- The trusted list covers Linux and BSD distributions and open-source software projects (8.3).
- Torrents stop when M6E quits (8.4).
- Port mapping is off by default (8.5).
- Test torrents are limited to the Fedora and Debian official pages, plus a synthetic fixture the harness generates and serves on 127.0.0.1 (D38, 2026-11-10).
- The bundled trusted-site list stays as the implementer checked it, including Kali Linux and Raspberry Pi OS (8.3, 2026-11-10).
- The engine is libtorrent-rasterbar 2.1.2 (D39), built without TLS for now: no HTTPS trackers or web seeds until TLS can be built for the macOS 11.0 target (2026-11-10).
- A downloaded or opened torrent leads to `about:torrents` with the Add dialog over it (D40, 2026-11-10).

**Still open:**
- Acceptance item 2 of 8.8 uses a loopback fixture under D38, not a torrent from an official page.

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
| D8 | Ad-hoc code signing, hardened runtime on, with Mozilla's release entitlements minus the keys that need an Apple provisioning profile (`tools/m6e/package/sign-adhoc.sh`). The app is signed after packaging and the DMG is built from the signed app (`tools/m6e/package/release.sh`). A downloaded copy needs **Open Anyway** once in System Settings (steps in the plan, P5.1). Notarization only with an Apple Developer ID, which is also what platform passkeys would need (D32). | No developer account (US$99 a year), and spending money is the user's decision. Ad-hoc signing is the minimum that lets an arm64 app launch. |
| D9 | Keep the Nightly 160.0a1 snapshot as the base. Security fixes arrive only through manual upstream merges. | The tree was already built on it; switching would mean re-importing. |
| D10 | Keep DRM: Widevine and OpenH264 download automatically, from Google and Cisco directly, using the bundled download lists rather than Mozilla's update server. | The user wants DRM sites (streaming) to work. |
| D11 | DNS over HTTPS on by default (Increased Protection, mode 2), user-adjustable in Settings. The default resolver is Cloudflare for Families (security) (D25, 2026-10-24); the region-based rollout is removed. | Privacy by default, with a fallback for networks where DoH fails. |
| D12 | Web Push must work well and uses Mozilla's push service, the one Mozilla service besides D2. | Site notifications have no other practical relay. Messages are end-to-end encrypted. |
| D13 (agent) | Sync, VPN, telemetry/Glean, Normandy, AI/ML and Translations code stays built but dormant behind locked switches, rather than deleted. | Dozens of modules import them; deletion would cause runtime failures with no user-visible benefit. |
| D14 (agent) | Keep Firefox's internal `MOZ_APP_ID`. | Add-ons, dictionaries and language packs signed for Firefox stay compatible. It's never shown to users. |
| D15 | macOS look: the neutral toolbar and the macOS accent colour only, not compact density or translucency. | The user's choice in P2.5. |
| D16 | Remove `about:mozilla` and `about:rights` (which only redirected to Mozilla's online terms), and Mozilla's default bookmarks. | No Mozilla content. |
| D17 | Keep the build toolchain on Homebrew (pinned), with `mach bootstrap` disabled. | The network's TLS inspection (Cloudflare Gateway) breaks `mach bootstrap` downloads. |
| D18 | Hardening defaults use strict Enhanced Tracking Protection rather than full fingerprinting resistance (`privacy.resistFingerprinting`). Full resistance is the Privacy Shield's Maximum protection option. **Amended by D31 (planned for 161.0a2):** M6E's strict category no longer applies Firefox's Fingerprinting Protection; as built in 161.0a1 it still does. | Full resistance spoofs the time zone to UTC and the user agent and breaks more sites, so it is opt-in. Fingerprinting Protection also affected first-party sites (D31). |
| D19 (agent) | The Tracker Blocker builds on Firefox's tracking protection icon and protections panel rather than a new widget. The count covers the current tab's page. | One blocking engine and one place to see what was blocked. |
| D20 | The global Privacy Shield sets the default for every site, and the Tracker Blocker's per-site switch overrides it both ways. With the Shield on, it turns protection off for an important site that breaks. With the Shield off, it turns protection on for a high-risk site. Shield off means no tracker blocking, which also lifts cookie isolation, as Firefox's per-site off does (agent: the content blocking allow-list check treats every site as exempt while the Shield is off, unless the site has the session `m6e-protect` permission; the blocking prefs keep their strict values). The per-site switch also covers known third-party fingerprinter blocking and, in Maximum mode, fingerprinting resistance. With the Shield off, turning a site on gives it tracker and known third-party fingerprinter blocking but no first-party fingerprinting protection (D31). Per-site choices last for the session only. The switch covers everything the Shield covers that the engine can apply per site. | The user's model (2026-10-23): one global default, with per-site exceptions in either direction. Global-only hardening is an engine limit. |
| D21 | Remove all Windows, Linux and Android code, including `#ifdef` branches in shared source. This supersedes Phase 1's rule to leave them. In first-party code, branches are stripped with `unifdef`. In vendored third-party code, whole platform-only files and directories are deleted, but `#ifdef`s inside vendored libraries stay (agent; see the note). | The user's choice (2026-10-23). Note (agent): stripping `#ifdef`s doesn't change the macOS binary, because the compiler already discards them. Inside vendored libraries (libvpx, ffmpeg, NSS, SQLite and others, frequent sources of security fixes), the edits would have to be redone on every security update, which works against D22. |
| D22 | Security hardening is in scope "everything under the Sun": upstream security merges, attack-surface reduction, compiler hardening, sandbox checks, a review of M6E's changes, static analysis and a review of first-party parsers, plus fixes. | The user's choice (2026-10-23): a security-hardened browser. |
| D23 | Dormant Mozilla features are deleted when the build and tests pass without them. Whatever stays must be shown to be unreachable remotely. This supersedes D13. | The user's choice (2026-10-23). |
| D24 (agent) | During the P4.5 to P4.8 source work, builds run without PGO (LTO stays on) so each full rebuild takes 15 to 40 minutes instead of 2 hours. PGO comes back for the final measured and packaged build. | Many full rebuilds are needed. PGO triples each one and only affects speed, not correctness. |
| D25 | Malware and phishing protection comes from the DNS resolver: **Cloudflare for Families (security)**, `https://security.cloudflare-dns.com/dns-query`, which refuses to resolve known malware and phishing domains. It is free, needs no account or API key, and has no non-commercial restriction. M6E does not use Google Safe Browsing: it would need an API key licensed only for non-commercial use, and it would send hash prefixes to Google.<br>• Gecko treats the resolver's block answers (`0.0.0.0` with extended DNS error 15 Blocked or 16 Censored) like errors 4 and 17: no fallback to the Mac's ordinary DNS, so a blocked site stays blocked and shows "can't find this site" (`netwerk/dns/DNSPacket.cpp`, marked M6E).<br>• The Settings list of DNS providers offers it as "Cloudflare for Families (security)".<br>• **Limits:** blocking works per domain, not per URL; downloads aren't checked; and when DoH itself is unavailable, mode 2 falls back to ordinary DNS without filtering.<br>• Users can still choose another resolver in Settings (D11). | The user's choice (2026-10-24, replacing the deferred Google Safe Browsing plan): M6E is to be free for everyone to use, without Google services. |
| D26 | Add-on updates stay off. Installed add-ons are maintained by hand, together with the codebase. | The user's choice (2026-10-23). No automatic contact with addons.mozilla.org. |
| D27 | WebGPU stays available. | The user's choice (2026-10-23): compatibility with GPU-heavy web apps. |
| D28 | The JavaScript and Wasm JITs stay on, hardened around them: experimental JS and Wasm features are off and SpiderMonkey's built-in JIT hardening is kept. | The user's choice (2026-10-23): disabling the JIT would undo Phase 4's speed gains. |
| D29 (agent) | How the page telemetry figures are measured:<br>• **TTFB:** `responseStart` of the page's navigation.<br>• **FCP:** the Paint Timing entry.<br>• **TTI:** Gecko's own time-to-first-interactive, the first 5-second window after content paints with no long tasks.<br>• **TLS handshake:** `connectEnd − secureConnectionStart`, or "connection reused" when no new handshake took place.<br>• **Page size:** bytes transferred by all of the page's requests (document, subframes and subresources, including cross-origin ones), counted in the parent process from the network channels, with the number of requests and how many came from the cache. The parent only sees bytes on the wire, because responses are decompressed in the content process.<br>• **Script parse and execution time:** SpiderMonkey's timers for the page's main realm while it loads. Execution comes from the existing `JSTimers`. Parse is a new timer covering main-thread and off-thread compilation. | Each figure comes from the engine's own measurement rather than an estimate. Web pages' own Resource Timing hides cross-origin sizes, so the size has to be counted in the parent. |
| D30 | The remaining dormant features are marked for deletion, to be removed after all other phases are complete: the VPN (IP protection: `browser/components/ipprotection`, `toolkit/components/ipprotection`, `browser/extensions/ipp-activator`), Translations, Sync and Mozilla accounts, the updater (`toolkit/mozapps/update`), AI/ML (`toolkit/components/ml`, `browser/components/genai`, `aiwindow`), the crash reporter, and Nimbus experiments. Until then, P4.8 proves each one unreachable: locked prefs, no listening sockets, no network traffic, and no entry points from web content or IPC. | The user's choice (2026-10-23): a full removal takes about 2 days of build and test cycles, and time is short. Normandy and the platform-only code are already deleted (P4.6, P4.7a, P4.7b). **Done in Phase 6 (2026-10-30)**: all nine are deleted (Nimbus leaves a stub) and the rest is proven unreachable. |
| D31 | Fingerprinting policy:<br>• **Privacy Shield standard mode** (Shield on, Maximum off) does not apply Firefox's Fingerprinting Protection (canvas randomization, WebGL renderer and vendor masking, hardware-concurrency tiering and similar) to any site, first-party sites included, so fewer sites are affected. Private windows lose it too: stock Firefox applies it there by default.<br>• **How:** a pref change only. The strict category string lists the Fingerprinting Protection tokens as off (`-fpp,-fppPrivate`); merely omitting them would leave it on in private windows. Firefox's baseline tier (`privacy.baselineFingerprintingProtection`) is set to false as well, because the Standard category applies it to all sites and it also randomizes the canvas, rounds the available screen size and collapses touch points.<br>• **Known third-party fingerprinters stay blocked** through Firefox's fingerprinter list (the `fp` token and `privacy.trackingprotection.fingerprinting.enabled`) in standard and Maximum mode while the Shield is on, except on sites switched off in the Tracker Blocker. With the Shield off nothing is blocked (D20). With the Shield off, a site switched on gets this blocking but no first-party fingerprinting protection.<br>• **Maximum protection** keeps full resistance (`privacy.resistFingerprinting`) for all sites, private windows included: the UTC time zone, a spoofed user agent, screen size and locale, and the other targets of that pref. Window letterboxing is not enabled.<br>• **Trade-off:** the list only holds known fingerprinters, so unlisted third-party scripts can fingerprint in standard mode, and private windows get no Fingerprinting Protection in it. Only Maximum covers both.<br>• **Upgrade:** a profile in the "strict" category (M6E's default) is migrated at startup by `ContentBlockingPrefs.init` and stays "strict" with Fingerprinting Protection off. A profile in the "custom" category is not changed, because those settings are the user's own choice. A profile in the Standard category gets no first-party Fingerprinting Protection either, private windows included.<br>• **Status:** planned for 161.0a2, not yet built. Supersedes the Fingerprinting Protection part of D18. | The user's choice for the 161.0a2 plan, after reports of site failures in 161.0a1 and the suspicion that blocking was too aggressive. Fingerprinting Protection applies to first-party sites too and masks values that sites read for legitimate reasons; it is not claimed to fix any specific report. Blocking known third-party fingerprinters is what protects users from tracking, and Maximum remains for those who accept breakage. |
| D32 | Platform passkeys (Touch ID, iCloud Keychain) and the iPhone QR code (hybrid) flow are unsupported.<br>• macOS only lets an app use them if it carries the entitlement `com.apple.developer.web-browser.public-key-credential` (checked in `dom/webauthn/MacOSWebAuthnService.mm`). The entitlement needs a paid Apple Developer account with a Team ID, a provisioning profile and Apple's approval.<br>• M6E is signed ad hoc (D8), and `tools/m6e/package/sign-adhoc.sh` leaves that entitlement out. Without it, M6E falls back to Firefox's own authenticator (`dom/webauthn/WebAuthnService.h`), which supports USB FIDO security keys only in production (`dom/webauthn/authrs_bridge`), and reports no platform authenticator.<br>• A site that offers only passkeys can show a generic error. Users should pick a security key or a password where the site offers one.<br>• **Path to support:** a paid Developer ID, notarization (D8) and the entitlement with its provisioning profile and Apple's approval. | The user's choice for the 161.0a2 plan: accept the limit and document it. Support depends on the paid Apple account that D8 deferred. |
| D33 | BitTorrent is a built-in M6E feature (Phase 8): a "BT" button beside the address bar opens a drop-down panel with the torrent options; M6E can open `.torrent` files and `magnet:` links; the interface is the browser's own panel, dialog and pages, not a separate application. M6E declares itself able to open them but becomes the default only when the user chooses (agent). | The user's request (2026-11-04). A default handler taken silently would surprise users who already have a torrent client. |
| D34 (agent) | The torrent engine is an existing, maintained library run in a separate helper executable, `m6e-torrent`, controlled over a private standard-input and output pipe, started on demand and stopped when idle or when M6E quits. The library is chosen by a spike (P8.0) against the Required and Wanted lists in 8.4. It is built by `tools/m6e/torrent/build-helper.sh`, outside `./mach build`. | A protocol stack of this size is not worth writing. Separate process: an exploit or crash stays out of the browser, memory is freed when idle, there is no listening control socket (D22, D23), and the Gecko tree and its long rebuilds are untouched. |
| D35 | The "Shady Torrent" warning is informational and never blocks: a banner in the Add dialog with the reasons, and the choice to add anyway. Legitimate sources, such as the Linux distributions' own sites, show nothing. (User, 2026-11-04.) The method is the agent's: an offline check on the metadata and the origin the torrent came from, with a bundled trusted-site list and rule data (8.3), and no outside reputation service. | The user wants the user made aware, not blocked. Offline matches D2 and the no-data-leaves-the-device rule. The origin of an https page is the one signal a malicious torrent cannot fake by copying tracker or file names. |
| D36 (agent) | Privacy and safety defaults for BitTorrent: nothing runs, listens or connects until a torrent is added; no file data before the Add dialog is confirmed; random listening port; port mapping and local peer discovery off; DHT and peer exchange off for private torrents; a neutral Peer ID and tracker User-Agent; no download of block lists; optional SOCKS5 and network-interface binding with a kill switch; finished files carry the macOS quarantine attribute; default seeding goal is ratio 1.0. | M6E is privacy-first and its attack surface is kept small (D22). A torrent client is the opposite by default (open port, public discovery), so the defaults are set the other way, and each is a setting. |
| D37 (agent) | The source spec's Web UI, tray icon, RSS, watch folders, run-external-program, antivirus hook, scheduler grid, zero-fill pre-allocation, auto-shutdown and Windows items are not built (table in 8.6). | They duplicate what the browser already provides, add attack surface (D22) or do not apply to macOS (D21). |
| D38 | Torrent testing is limited to torrents from two official pages: the Fedora Linux page (https://fedoraproject.org/torrents/45/) and the Debian arm64 BitTorrent DVD page (https://cdimage.debian.org/debian-cd/current/arm64/bt-dvd/). Every other torrent is "Shady" for testing, including Ubuntu and its flavors, Linux Mint, Arch, openSUSE, Kali, Tails, Raspberry Pi OS, FreeBSD, OpenBSD, NetBSD, LibreOffice, GIMP, Kiwix and the Internet Archive, and also Debian or Fedora torrents taken from anywhere else. Never to be breached (8.9). (User, 2026-11-10.) | The developer's computer is office-issued and security-hardened. Running an untrusted torrent could breach company policy and put the developer's job at risk. |
| D39 | The torrent engine is libtorrent-rasterbar 2.1.2, statically linked into the `m6e-torrent` helper, built offline from a pinned tarball without TLS, WebTorrent or I2P. HTTPS trackers and web seeds are deferred until TLS can be built for the macOS 11.0 target. | Confirmed by the user on 2026-11-10. The P8.0 evaluation: libtorrent covers every Required and Wanted item, with the smallest gaps, memory (2.5 to 5 MB idle) and a licence that ships in an MPL-2.0 product. librqbit lacks encryption and rarest-first; anacrolix/torrent lacks per-torrent limits, queue and seeding goals and needs a new toolchain. |
| D40 | A downloaded or opened `.torrent` (and a `magnet:` link) takes the user to `about:torrents`, with the Add dialog shown over it; the file type is associated with M6E. The Add dialog and its consent rule (D36) are unchanged. (User, 2026-11-10; the Add-dialog-over-the-page reading is the agent's.) | The user wants a torrent file to lead to the torrent page, not to a stray dialog. The dialog stays so that nothing is added or fetched without a click. |

## 5. Ground rules
- **Repository:** work in the existing checkout; one commit per plan step, pushed to `origin/main` after a successful build and checks.
- **Behaviour prefs:** all M6E prefs live in `browser/app/profile/m6e.js` (included at the end of `firefox.js`). M6E visual overrides live in `toolkit/themes/shared/m6e-tokens.css`. Keep other source changes small and marked `M6E:`, so upstream merges stay manageable.
- **Build cost:** configure-level changes (mozconfig, `moz.configure`) rebuild the whole tree in 40 minutes to 2 hours; batch them. Front-end changes only need `./mach build faster`.
