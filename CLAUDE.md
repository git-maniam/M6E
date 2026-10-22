# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

@AGENTS.md

## M6E project context
This is a Firefox fork being turned into "M6E", a debloated, privacy-first, macOS-only browser. The full spec is in `m6e_browser_agent_instructions.md`, and the step-by-step execution plan is in `M6E_IMPLEMENTATION_PLAN.md`. Read both before making large-scale changes (platform stripping, localization removal, rebranding, telemetry removal).
- Only English (en-US and en-GB) is kept, and only the light theme.
- Windows, Linux, Android and iOS code, plus Pocket, Firefox View, Sync, VPN and telemetry, is being removed.
- Branding is "M6E" with a Bangalore Autorickshaw icon. Don't replace "Firefox" blindly: it appears in licenses, upstream URLs, and code identifiers that must not change.
