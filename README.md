<p align="center"><img src="M6E-Icon.png" alt="M6E icon: a Bangalore autorickshaw" width="160"></p>

# M6E

M6E (said "'Am Sexy") is a fast, private and deliberately simple web browser for Macs with Apple silicon. It's built from the Firefox source code, with everything that phones home, tracks you or adds clutter removed. Like the Bangalore autorickshaw on its icon, it's meant to be nimble.

> M6E is an experimental release. It's built for speed and privacy, but you may run into bugs or websites that don't work.

## What's different from Firefox

- **Debloated:** no Pocket, Firefox View, accounts and Sync, VPN, telemetry, crash reporter, updater, studies, sponsored content or AI features. Only macOS on Apple silicon is supported, and the interface is in English only.
- **Quiet on the network:** a fresh profile contacts only security-update, push, DNS-over-HTTPS and video-codec servers. Nothing else, and nothing from Mozilla except Web Push and security blocklists.
- **Privacy by default:**
  - strict tracking protection with fingerprinting protection;
  - HTTPS-Only mode, Global Privacy Control and WebRTC IP-leak protection;
  - Arkenfox-style hardening.
- **Privacy Shield:** one switch in Settings, Privacy that sets how every site is treated, plus an optional Maximum protection mode with full fingerprinting resistance.
- **Tracker Blocker:** the shield at the left of the address bar counts what was blocked on the current page. Its panel lists the blocked trackers and has a per-site switch that overrides the Privacy Shield for the session, in either direction.
- **Search:** Google by default, with Bing as the only alternative. There are no partner codes, and no way to add other engines.
- **Reading Mode:** strictly black text on white, 25% larger, with sidebars, ads, embeds and tracking pixels stripped. It can be opened on any page from the View menu.
- **Look:** a light, Safari-like interface that follows the macOS accent colour, with the M6E icon in place of every fox. The home page shows the M6E splash illustration above the search box, and there's a custom "No Internet Connection" page.
- **Also:** a bookmarks toolbar on/off switch in Settings, and push notifications delivered through macOS Notification Center.

## Status

| Phase | Scope | Status |
|---|---|---|
| 0 | Toolchain and baseline | Done |
| 1 | Debloating | Done |
| 2 | Rebranding and macOS UI | Done |
| 3 | Features and privacy | Done |
| 4 | Performance (build optimisation, profiling) | Next |
| 5 | `.dmg` packaging and acceptance tests | Not started |

## Documentation

- [`m6e_browser_agent_instructions.md`](m6e_browser_agent_instructions.md): the specification, covering what M6E is and why, the current status, and the numbered decision log.
- [`M6E_IMPLEMENTATION_PLAN.md`](M6E_IMPLEMENTATION_PLAN.md): how it's built, with a progress log of every step and its commit, and notes on the as-built state, the next step and the remaining steps.
- [`CLAUDE.md`](CLAUDE.md): working rules for development with Claude Code.

## Building

M6E builds on macOS with Apple silicon, using the Mozilla build system (`./mach`). The toolchain comes from Homebrew rather than `mach bootstrap`.

1. Install Xcode, then the Homebrew toolchain:
   ```sh
   brew install rust cbindgen nasm llvm lld wasi-libc wasi-runtimes node
   ```
2. The repository's `mozconfig` sets up an optimised arm64 build in `obj-m6e/` with the M6E branding. Build it:
   ```sh
   ./mach build          # a full build takes about 40 minutes
   ./mach build faster   # after changes to front-end files only (JS, CSS, FTL, prefs)
   ```
3. Run it with `./mach run`, or open `obj-m6e/dist/M6E.app`.

Never use artifact builds: they download Mozilla's prebuilt binaries.

M6E's own changes are kept small so that upstream Firefox fixes can still be merged:
- behaviour prefs are in `browser/app/profile/m6e.js`;
- visual overrides are in `toolkit/themes/shared/m6e-tokens.css`;
- branding is in `browser/branding/m6e`;
- other edits are marked `M6E:` in the source.

Helper tools are in `tools/m6e/`:
- `measure.py`: startup, memory and network measurements;
- `chrome_eval.py`: scripted checks;
- `branding/`: icon generation, the user-visible string scan and the fox-artwork replacement;
- `search/`: the search configuration filter;
- `push/` and `trackers/`: end-to-end checks.

## Licence

M6E is based on the Firefox source code from the [Mozilla project](https://www.mozilla.org/) and is distributed under the [Mozilla Public License 2.0](LICENSE). Firefox and Mozilla are trademarks of the Mozilla Foundation. M6E is not affiliated with or endorsed by Mozilla.
