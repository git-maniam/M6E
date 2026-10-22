# Project M6E: AI Agent Execution Prompt

## 1. Project Overview & Objective

**Target Agent:** AntiGravity, Claude Code, or equivalent autonomous coding agent.
**Objective:** Fork the Mozilla Firefox codebase and transform it into a hyper-optimized, heavily debloated, privacy-first browser named **M6E**. The end product must match or exceed the performance and anti-telemetry behavior of **LibreWolf**, tailored specifically for macOS.

**Core Identity:**

* **Name:** M6E
* **Icon/Branding:** A stylized Bangalore Autorickshaw. All visual assets must reflect this.
* **Language:** Strictly English (US/UK).
* **Platform:** macOS ONLY.

## 2. Phase 1: Aggressive Debloating & Codebase Stripping

Firefox contains significant code for features, platforms, and themes that are unnecessary for a lean, focused browser.

* **Platform Stripping:**
  * Identify and completely purge all codebase directories, build scripts, and dependencies related to Windows, Linux, Android, and iOS. The browser will compile and run exclusively on macOS.

* **Language Stripping:**
  * Scan the entire repository for localization files (`.ftl`, `.properties`, `.dtd`).
  * Delete ALL localization files and code branches EXCEPT English (`en-US` and `en-GB`). Remove UI elements for language switching.

* **Theme Stripping:**
  * Strip all dark mode CSS variables, system-theme matching logic, and custom theme capabilities.
  * Hardcode the browser to support ONLY the default Light Theme. 

* **Feature Eradication:**
  * Completely rip out **Pocket**, **Firefox View**, **Firefox Sync**, and **Firefox VPN** integrations.
  * Strip all **Telemetry**, **Crashpad**, and **Data Collection** modules. 
  * Remove Sponsored Top Sites and Sponsored Search suggestions.

## 3. Phase 2: Rebranding & UI Modernization

* **Name & Asset Replacement:** 
  * Perform a repository-wide case-sensitive search and replace (`Mozilla Firefox` -> `M6E`, `Firefox` -> `M6E`).
  * Replace the default macOS app icons (`.icns`) with the custom **Bangalore Autorickshaw** logo. Ensure the build scripts reference the new asset names.

* **Modern UI:** 
  * Refine the default CSS to ensure a clean, modern, minimalist aesthetic that looks native to modern macOS environments. 

## 4. Phase 3: Features & Privacy Controls

* **Search Engines:** 
  * Set **Google** as the default search engine. 
  * Retain **Bing** as the only other selectable option. 
  * Completely strip out all other search engines (DuckDuckGo, Yahoo, Wikipedia, etc.) from the codebase.

* **Bookmark Management:**
  * Retain core Mozilla bookmarking capabilities (import from other browsers, bookmark manager).
  * Ensure a Bookmark Toolbar is available and easily toggleable (on/off) from the settings menu.

* **M6E Enhanced Reading Mode (NEW):**
  * Modify the existing Firefox Reader View to adhere strictly to these rules:
    * **Trigger:** User-toggleable when on an article/page.
    * **Clutter Removal:** Strip all sidebars, navigation, ads, and non-essential iframes.
    * **Typography:** Automatically increase the base extracted text size by 25% for easier reading and copying.
    * **Styling:** Force a strict black text (`#000000`) on white background (`#FFFFFF`) palette. Remove all background images or page colors.
    * **Media:** Retain only essential inline article images/pictures.

* **User-Configurable Privacy Toggles:** 
  * Implement Arkenfox-style privacy hardening defaults.
  * Build a prominent "Privacy Shield" settings panel in the UI (e.g., `about:m6e#privacy`). This must include a master toggle allowing the user to easily switch off strict privacy settings (anti-fingerprinting, strict blocking) if website compatibility breaks.

## 5. Phase 4: Tech Stack Optimization & Speed Upgrades

* **Execution Directive:** Scan the UI rendering pipeline and networking stack.
  * If specific JavaScript components in the front-end are identified as CPU-heavy or slow during initialization, rewrite those specific UI logic controllers into **Rust** via WebAssembly (Wasm) or by moving the logic into the native Rust backend.
  * Compile the macOS binary using aggressive optimization flags (e.g., `-O3` for C++, `opt-level = 3` for Rust, and enable Link-Time Optimization (LTO) in the build scripts).

## 6. Execution Command Sequence for Agent

1. Clone Mozilla `mozilla-central` repository.
2. Execute Phase 1 (Strip OS, Language, Themes & Bloat). Run macOS build to ensure stable compilation.
3. Execute Phase 2 (Rebranding & Modern UI). 
4. Execute Phase 3 (Search Engines, Bookmarks, Reading Mode, Privacy Toggles).
5. Execute Phase 4 (Performance & Rust conversions).
6. Generate output `.dmg` / `.app` binaries for macOS (Apple Silicon and Intel).