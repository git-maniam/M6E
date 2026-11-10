# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

@AGENTS.md

## M6E project context
This is a Firefox fork being turned into "M6E", a debloated, privacy-first browser for Apple Silicon Macs only. Read these before changing anything:
- `m6e_browser_agent_instructions.md`: the spec, covering what M6E is, current status and the numbered decision log (D1 to D37; Phase 8, the BitTorrent feature, is specified but not started).
- `M6E_IMPLEMENTATION_PLAN.md`: how it's built, covering the step loop, tools, a progress log with commits, as-built notes and the next step.

Resuming: Phases 0 to 6 are finished. The current work is the 161.0a2 release (version bumped; remaining: the P6.3 clobber-and-build sequence, `release.sh`, acceptance, then publishing). Read the plan's section 4, "161.0a2" and its approved steps first. When the user says "Resume from Phase N", follow the resuming checklist at the top of section 4. The "Resume from Sunday / Tuesday / after restart" bookmarks in section 4 are finished history (Restart bookmark: only optional follow-ups); don't act on them unless asked.

"Start Bittorrent feature implementation" starts Phase 8, the BitTorrent feature: open the plan's section 4, read "Phase 8 bookmark", run the pre-flight in section 5 "Phase 8", then follow its waves without asking questions. Spawn the agents of each wave in one message so they run in parallel (Agent tool, `isolation: "worktree"`, branches `m6e-p8/<name>`). All Phase 8 work happens on the branch `BT-Feature`; `main` stays untouched until the user merges it. Only the lead builds, tests, merges, commits to `BT-Feature` and edits the spec and plan.

**Torrent testing rule (spec 8.9, D38; never breach):** test only with torrents from the Fedora page (https://fedoraproject.org/torrents/45/) and the Debian arm64 page (https://cdimage.debian.org/debian-cd/current/arm64/bt-dvd/). Any other torrent or magnet link is off-limits, because this is an office-issued, security-hardened computer. Synthetic fixtures need the user's explicit approval first.

Working rules:
- **Where M6E changes live:**
  - behaviour prefs: `browser/app/profile/m6e.js`;
  - visual overrides: `toolkit/themes/shared/m6e-tokens.css`;
  - branding: `browser/branding/m6e`, regenerated from `M6E-Icon.png` by `tools/m6e/branding/make-icons.sh`;
  - other source edits: keep them small and marked `M6E:`, to keep upstream merges manageable.
- **Build:**
  - `./mach build faster` for front-end changes and `./mach build` for C++ or Rust.
  - Configure changes (mozconfig, `moz.configure`) rebuild everything in 40 minutes to 2 hours, so batch them.
  - The app is `obj-m6e/dist/M6E.app`, with the executable `Contents/MacOS/m6e`.
  - PGO is on (`MOZ_PGO=1`), so a C++ or version change needs `./mach clobber` before the rebuild.
  - The toolchain is Homebrew's (`--disable-bootstrap`), so `./mach bootstrap` fails here (Cloudflare TLS inspection).
- **Check behaviour** in the running build with `tools/m6e/chrome_eval.py` and `tools/m6e/measure.py` (see the plan for Marionette pitfalls).
- **Branding text:** never blindly replace "Firefox" or "Mozilla". They must stay in licences, MPL headers, the attribution text, internal identifiers and upstream URLs. `tools/m6e/branding/scan-strings.py` checks user-visible strings.
- **Tests and release:**
  - Quick front-end rebuild: `make -C obj-m6e/faster`.
  - One test: `./mach test <path> --headless` (panel and popup suites refuse `--headless`). Regression suites: `tools/m6e/run_tests.sh <log directory>`.
  - Checks on a packaged app: `tools/m6e/acceptance.py` (15 checks) and `tools/m6e/unreachability_check.py` (7 checks).
  - Release: `tools/m6e/package/release.sh [output directory]`, then `tools/m6e/package/homebrew-cask.sh`. Never test the signed app from `/tmp`.
  - After an upstream merge: `tools/m6e/merge/post-merge-checks.sh`.
  - Lint and format changed files with `./mach lint` and `./mach format`.
- **Commits:** one commit per plan step (`M6E P<phase>.<step>: …`), pushed to `origin/main` after it builds and checks out.
- **Checkpoints:** during a step, also commit and push work in progress (`… (work in progress): …`) and keep the checkpoint note in the plan's section 4 current, so a dropped connection never loses work.

## Architecture: how M6E overlays Firefox
- **Prefs:** `browser/app/profile/m6e.js` is pulled in by `#include m6e.js` at the end of `browser/app/profile/firefox.js`. Every M6E pref change lives there, many of them `locked`.
- **Privacy Shield:** `browser/components/protections/M6EPrivacyShield.sys.mjs` flips prefs and the `m6e-protect` site permission. With the Shield off it relies on small C++ hooks (`ContentBlockingAllowList::Check`, `nsContentUtils::ETPSaysShouldNotResistFingerprinting`, `nsHTTPSOnlyUtils::PotentiallyDowngradeHttpsFirstRequest`). Its fallback values must stay in sync with `m6e.js` and `testing/profiles/base/user.js`. Tests are in `browser/components/preferences/tests/etp/browser_m6e_*.js`.
- **Page telemetry button:** `browser/components/pagetelemetry/` with `browser/base/content/browser-m6ePageTelemetry.js`; timings are measured locally and never sent.
- **Deleted code:** Windows, Linux, Android and iOS code, and (Phase 6) Sync, Mozilla accounts, the VPN, Translations, AI and machine learning, the updater, the crash reporter and Nimbus (a stub stays) are gone. Remaining references are proven unreachable by `tools/m6e/unreachability_check.py`; don't re-add them.
- **Upstream:** the base is Firefox commit `034cd8a8`; merge tooling is in `tools/m6e/merge/`.
