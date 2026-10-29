# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

@AGENTS.md

## M6E project context
This is a Firefox fork being turned into "M6E", a debloated, privacy-first browser for Apple Silicon Macs only. Read these before changing anything:
- `m6e_browser_agent_instructions.md`: the spec, covering what M6E is, current status and the numbered decision log (D1 to D20).
- `M6E_IMPLEMENTATION_PLAN.md`: how it's built, covering the step loop, tools, a progress log with commits, as-built notes and the next step.

Resuming: when the user says "Resume from Phase N", follow the resuming checklist at the top of the plan's section 4 ("Next step"), then continue with that phase.
"Resume from Sunday" is a bookmark: open the plan's section 4, read "Sunday bookmark" and follow its checklist and "Next action" without asking questions.
"Resume from Tuesday" is the newer bookmark (written 2026-10-27): open the plan's section 4, read "Tuesday bookmark" and follow its checklist and "Next action" without asking questions. Use it in preference to "Resume from Sunday".

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
- **Check behaviour** in the running build with `tools/m6e/chrome_eval.py` and `tools/m6e/measure.py` (see the plan for Marionette pitfalls).
- **Branding text:** never blindly replace "Firefox" or "Mozilla". They must stay in licences, MPL headers, the attribution text, internal identifiers and upstream URLs. `tools/m6e/branding/scan-strings.py` checks user-visible strings.
- **Commits:** one commit per plan step (`M6E P<phase>.<step>: …`), pushed to `origin/main` after it builds and checks out.
- **Checkpoints:** during a step, also commit and push work in progress (`… (work in progress): …`) and keep the checkpoint note in the plan's section 4 current, so a dropped connection never loses work.
