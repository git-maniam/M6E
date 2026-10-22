# Project M6E: AI Agent Execution Prompt

## 1. Project Overview & Objective

**Target Agent:** AntiGravity, Claude Code, or equivalent autonomous coding agent.
**Objective:** Fork the Mozilla Firefox codebase and transform it into a hyper-optimized, heavily debloated, privacy-first browser named **M6E**. The end product must match or exceed the performance and anti-telemetry behavior of **LibreWolf**, tailored specifically for macOS.

**Core Identity:**

* **Name:** M6E
* **Icon/Branding:** A stylized Bangalore Autorickshaw (yellow body, green canopy, on a white rounded-square tile). The source asset is `M6E-Icon.png` in the repository root.
* **Language:** Strictly English (US/UK).
* **Platform:** macOS ONLY, Apple Silicon (arm64) only. No Intel or universal build.

## 2. Phase 1: Aggressive Debloating & Codebase Stripping

Firefox contains significant code for features, platforms, and themes that are unnecessary for a lean, focused browser.

* **Platform Stripping:**
  * Remove only the top-level platform directories related to Windows, Linux, Android, and iOS (e.g. `mobile/android`, `widget/windows`, `widget/gtk`). Leave `#ifdef` branches in shared code in place. The browser will compile and run exclusively on macOS.

* **Language Stripping:**
  * Scan the entire repository for localization files (`.ftl`, `.properties`, `.dtd`).
  * Delete ALL localization files and code branches EXCEPT English (`en-US` and `en-GB`). Remove UI elements for language switching.

* **Theme Stripping:**
  * Strip all dark mode CSS variables, system-theme matching logic, and custom theme capabilities.
  * Hardcode the browser chrome to ONLY the default Light Theme, regardless of macOS appearance. Keep the `prefers-color-scheme` plumbing for web content.

* **Feature Eradication:**
  * Completely rip out **Pocket**, **Firefox View**, **Firefox Sync**, and **Firefox VPN** integrations.
  * Strip all **Telemetry**, **Crashpad**, and **Data Collection** modules.
  * Disable updates, AMO, Normandy, Remote Settings and Safe Browsing lookups. Extensions are installed manually.
  * Remove Sponsored Top Sites and Sponsored Search suggestions.

## 3. Phase 2: Rebranding & UI Modernization

* **Name & Asset Replacement:** 
  * Use Mozilla's branding mechanism (a `browser/branding/m6e` directory and the related configure options) instead of a blind repo-wide replace. "Mozilla" and "Firefox" must not appear in anything user-visible (UI, app bundle, about pages, displayed URLs). Keep `LICENSE`, MPL headers and `AUTHORS` intact, and add a "Based on Firefox source" note in the license/about text. Do not rename internal identifiers such as `MOZ_` prefixes.
  * Replace the default macOS app icons (`.icns`) with the **Bangalore Autorickshaw** icon generated from `M6E-Icon.png` (repository root).
    * **Source properties:** 2816x1536 PNG. It is not square, and it is not transparent. The white rounded-square tile with the autorickshaw sits centred on an opaque light-grey background, and a small sparkle watermark is in the bottom-right corner, outside the tile.
    * **Preparation:** Crop to the white rounded-square tile only, which discards the grey background and the watermark. Make the area outside the tile's rounded corners transparent, and fit the result onto a square 1024x1024 transparent canvas. Never stretch or squash the non-square source.
    * **Generation:** Build the full macOS iconset (16, 32, 128, 256 and 512 px, each at @1x and @2x) from the 1024x1024 master using `sips`, then `iconutil -c icns`. Commit the generated master PNG and `.icns` under `browser/branding/m6e/`. Also derive every other branding image Firefox's branding directory expects (in-app logos, about-dialog artwork, DMG artwork) from the same master.
    * **Stable names:** Build scripts must reference stable asset names, so a revised icon can later be dropped in by replacing `M6E-Icon.png` and regenerating, without build-script changes.

* **Modern UI:** 
  * Refine the default CSS to ensure a clean, modern, minimalist aesthetic that looks native to modern macOS environments. 

## 4. Phase 3: Features & Privacy Controls

* **Search Engines:** 
  * Set **Google** as the default search engine.
  * Retain **Bing** as the only other selectable (fallback) option.
  * Completely strip out all other search engines (DuckDuckGo, Yahoo, Wikipedia, etc.) from the codebase outright.

* **Bookmark Management:**
  * Retain core Mozilla bookmarking capabilities (import from other browsers, bookmark manager).
  * Ensure a Bookmark Toolbar is available and easily toggleable (on/off) from the settings menu.

* **M6E Enhanced Reading Mode (NEW):**
  * Modify the existing Firefox Reader View in place (do not build a separate reader) to adhere strictly to these rules:
    * **Trigger:** User-toggleable when on an article/page.
    * **Clutter Removal:** Strip all sidebars, navigation, ads, and non-essential iframes.
    * **Typography:** Automatically increase the base extracted text size by 25% for easier reading and copying.
    * **Styling:** Force a strict black text (`#000000`) on white background (`#FFFFFF`) palette. Remove all background images or page colors.
    * **Media:** Retain only essential inline article images/pictures.

* **User-Configurable Privacy Toggles:** 
  * Implement Arkenfox-style privacy hardening defaults.
  * Build a prominent "Privacy Shield" settings section inside the existing settings page (`about:preferences`), not a new about page. This must include a master toggle allowing the user to easily switch off strict privacy settings (anti-fingerprinting, strict blocking) if website compatibility breaks.

## 5. Phase 4: Tech Stack Optimization & Speed Upgrades

* **Execution Directive:** Scan the UI rendering pipeline and networking stack.
  * Apply compile-flag optimizations first. Rewrite JS only if profiling shows a hotspot. If specific JavaScript components in the front-end are identified as CPU-heavy or slow during initialization, rewrite those specific UI logic controllers into **Rust** via WebAssembly (Wasm) or by moving the logic into the native Rust backend.
  * Compile the macOS binary using aggressive optimization flags (e.g., `-O3` for C++, `opt-level = 3` for Rust, and enable Link-Time Optimization (LTO) in the build scripts).

## 5b. Summary of Clarified Decisions (already applied above)

* **Architecture:** Apple Silicon (arm64) only. No Intel or universal build.
* **Repository:** Work in the existing checkout (already cloned; do not re-clone). One commit per phase step, and a successful `./mach build` is required before moving to the next step.
* **Platform stripping:** Remove only top-level platform directories (e.g. `mobile/android`, `widget/windows`, `widget/gtk`). Leave `#ifdef` branches in shared code in place.
* **Rebranding:** Use Mozilla's branding mechanism (a `browser/branding/m6e` directory and the related configure options) rather than a blind repo-wide replace. "Mozilla" and "Firefox" must not appear in anything user-visible (UI, app bundle, about pages, displayed URLs). Keep `LICENSE`, MPL headers and `AUTHORS` intact, and add a "Based on Firefox source" note in the license/about text. Do not rename internal identifiers such as `MOZ_` prefixes.
* **Theme:** The browser chrome is always light, regardless of macOS appearance. Keep the `prefers-color-scheme` plumbing for web content.
* **Network services:** Disable updates, AMO, Normandy, Remote Settings and Safe Browsing lookups. Extensions are installed manually.
* **Icon:** The application icon is `M6E-Icon.png` (repository root), a PNG rather than an SVG. Crop it to the white rounded-square tile (dropping the grey background and the watermark), place it on a 1024x1024 transparent canvas, and generate the `.icns` and all other branding images from that master. Use stable asset names so the icon can be replaced later by regenerating from a new `M6E-Icon.png`.
* **Reading mode:** Modify the existing Reader View in place; do not build a separate reader.
* **Privacy panel:** Implement the "Privacy Shield" as a section inside the existing settings page (`about:preferences`), not as a new `about:m6e` page.
* **Search engines:** Google is the default and Bing is the only other option. Remove all other engines outright in Phase 3.
* **Phase 4:** Apply compile-flag optimizations (LTO, `opt-level = 3`) first. Rewrite JS in Rust/Wasm only for hotspots that profiling shows to be a problem.

## 6. Execution Command Sequence for Agent

1. Work in the existing checkout (already cloned; do not re-clone). Run a baseline `./mach build` first. Make one commit per phase step, and require a successful `./mach build` before moving to the next step.
2. Execute Phase 1 (Strip OS, Language, Themes & Bloat). Run macOS build to ensure stable compilation.
3. Execute Phase 2 (Rebranding & Modern UI). 
4. Execute Phase 3 (Search Engines, Bookmarks, Reading Mode, Privacy Toggles).
5. Execute Phase 4 (Performance & Rust conversions).
6. Generate output `.dmg` / `.app` binaries for macOS (Apple Silicon only).