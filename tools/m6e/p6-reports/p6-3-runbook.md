# P6.3 runbook: LTO and PGO back on, clean build, regressions, release DMG

For an engineer resuming cold. Read `CLAUDE.md`, the spec (`m6e_browser_agent_instructions.md`) and the plan (`M6E_IMPLEMENTATION_PLAN.md`, section 4 and "Phase 6") first. Start only when P6.1a to P6.1i are integrated on `main`. Nothing else may use the machine: no other M6E, no builds, no git activity during measurements.

## 1. Steps, in order

1. Adopt the mozconfig commit from `origin/m6e-p6/ready-p63` ("Restore LTO and PGO in the mozconfig", `87ac4ca559`, it touches only `mozconfig`): `git fetch && git cherry-pick 87ac4ca559`. Check that the file has `ac_add_options --enable-lto=cross` and `ac_add_options MOZ_PGO=1` uncommented. It is a configure change, so it forces a clean build; adopt it only now.
2. Adopt the tooling commit(s) after it on that branch the same way (`git log origin/m6e-p6/ready-p63 --oneline`); they only touch `tools/m6e/`.
3. `git status` must be clean. Quit every M6E and stray `mach`/`m6e` process (`pgrep -fl 'mach|m6e'`).
4. `./mach clobber`
5. `mkdir -p artifacts/p6/p63 && ./mach build > artifacts/p6/p63/build.log 2>&1` (run it in the background and read the log; do not pipe it through `tail`).
   - Duration: three PGO stages (instrumented build in `obj-m6e/instrumented`, a profiling run, final build with ThinLTO), about 80 to 90 minutes unattended (plan: P4.2c 1.5 hours, A2 80 minutes). Expect longer if the machine is busy.
   - If the final ThinLTO link fails with `ProfileSummary: IDs have conflicting values`, the profile was regenerated against a partial build: `./mach clobber` and start again. Never rebuild PGO incrementally after a C++ change.
   - Keep the screen awake during the profiling run; it opens windows.
6. Headless regression suites: `tools/m6e/run_tests.sh artifacts/p6/p63/regress` (section 3). It prints `<exit code> <suite>` per suite; the logs are `artifacts/p6/p63/regress/<suite>.log`.
7. Consumer suites of P6.1c to P6.1i (section 4), each with `./mach test --headless <dir>` into `artifacts/p6/p63/consumer/<name>.log`, one at a time (mochitests share port 8888).
8. Window suites with an unlocked display (section 5).
9. `./mach python tools/m6e/acceptance.py --headless --idle-seconds 60 --app obj-m6e/dist/M6E.app --out artifacts/p6/p63/acceptance-dev` (expect 13 of 15), then the packaged run in step 11.
10. `tools/m6e/package/release.sh` (section 7).
11. Acceptance on the packaged app (section 6), then measurements (section 8).
12. Record the results (section 9) and commit `M6E P6.3: ...`.

## 2. How the PGO profile is produced here

There is no M6E script for it: `MOZ_PGO=1` makes `./mach build` do all three stages itself. The profiling run is `build/pgo/profileserver.py`, using the `profileserver` profile in `testing/profiles` and the quitter add-on (`tools/quitter`); it writes `obj-m6e/instrumented/merged.profdata` (about 100 MB), which `llvm-profdata` (`LLVM_PROFDATA` in `mozconfig`) merges and the final stage reads. The profile is not kept between builds and make does not track it as a dependency, hence the clobber rule. `extensions.autoDisableScopes` and `enabledScopes` must stay unlocked or the run waits for the quitter add-on for hours (plan, P4.9).

## 3. `tools/m6e/run_tests.sh <log directory>`

Suites: m6e-own (`--headless`: preferences etp, https-first, services/settings unit), security (`--headless`: https-only, antitracking xpcshell), reader (`--headless`), nimbus (`toolkit/components/nimbus/test/unit`, now the single `test_stub.js`, no longer 33 tests), modules (`toolkit/modules/tests/xpcshell`, `toolkit/components/utils/test/unit`), policies (`--headless`), then `pagetelemetry` and `trustpanel` without `--headless`, which need an unlocked display. Compare the failures with section 9, not with memory.

## 4. Consumer suites used in P6.1c to P6.1i

Run as `./mach test --headless <path>`; a subdirectory such as sidebar is best run file by file (a timeout stops the whole chunk and hides later files).
- Static: `browser/base/content/test/static` (`browser_all_files_referenced`, `browser_parsable_css`, `browser_parsable_script`, `browser_glean_metrics_exist`).
- genai/ML (P6.1c, d): `browser/components/preferences/tests` (languages, aiFeatures, search, tabs-browsing panes), `browser/base/content/test/browser-contextMenu`, tab context menu and altStructure, `browser/components/uitour` (aiControls), `browser/components/sidebar/tests/browser`, tab groups, `browser/components/urlbar/tests/unit` (`test_UrlbarPrefs.js`), `browser/components/asrouter/tests`, `browser/components/aboutwelcome` xpcshell, `browser/extensions/formautofill`, `toolkit/components/passwordmgr`, `browser/components/sessionstore`, startup-unit, pdf.js tests.
- Sync (P6.1e): `toolkit/components/places/tests/sync`, `tests/bookmarks`, `toolkit/components/extensions` storage.sync xpcshell, `toolkit/modules/tests/xpcshell/test_BrowserUtils.js`, `toolkit/components/telemetry/tests/unit`, browser tests under `browser/components/{sidebar,customizableui,urlbar,preferences,sharing,aboutlogins,uitour,asrouter,profiles}` and `browser/base/content/test/{browser-contextMenu,browser-static,browser-siteIdentity,general}`. For firefoxview find the manifest with `git ls-files browser/components/firefoxview | grep toml`.
- Mozilla accounts (P6.1f): the same pane/urlbar/ASRouter/aboutwelcome/formautofill/passwordmgr/sessionstore/customizableui set, plus backup-unit (`browser/components/backup/tests/xpcshell`), protections, profiles.
- Updater (P6.1g): `toolkit/components/backgroundtasks/tests/xpcshell`, `toolkit/modules/tests/xpcshell`, telemetry unit, `browser/components/enterprisepolicies/tests/xpcshell`, `browser/modules/test/unit`, `browser/components/preferences/tests` (`privacy/browser_privacy_status_card.js`), urlbar `browser-tips`, `browser-quickactions`, `browser-engagementTelemetry`, `browser/base/content/test/browser-about`, `toolkit/modules/tests/browser/browser_Troubleshoot.js`, `caps/tests/mochitest/test_bug292789.html`, marionette `toolkit/xre/test/marionette/test_relaunch_dock_overlap.py`.
- Crash reporter (P6.1h): `browser/base/content/test/tabs`, `browser/components/sessionstore` (`browser_crashedTabs`, `browser_background_tab_crash`), `toolkit/content/tests/browser`, `toolkit/components/crashmonitor`, `toolkit/mozapps/extensions` xpcshell, `dom/l10n`, TelemetrySend. Manual smoke: crash a tab (`BrowserTestUtils.crashFrame`); `about:tabcrashed` offers only Close and Restore; no `google_breakpad` symbols in libxul.
- Nimbus (P6.1i): `toolkit/components/nimbus/test/unit`, `toolkit/modules/tests/xpcshell/test_firstStartup.js`, `toolkit/components/glean/tests/pytest`, telemetry unit (`test_PrefMigrationForTOU`, `test_TelemetryReportingPolicy`), asrouter `test_PreonboardingSplash.js`, privatebrowsing, customizableui (fireButton), browser-about (aboutSupport).

## 5. Window suites (need an unlocked, awake display)

`./mach test browser/components/pagetelemetry` and `./mach test browser/components/urlbar/tests/browser-trustPanel`, each alone, no `--headless`. They also need `browser/base/content/test/browser-contextMenu/browser_contextmenu.js` run with a window (headless hangs in `test_imagelink`, open since P6.1c). A locked display shows as "application timed out after 370 seconds with no output" and 0 checks.

## 6. `tools/m6e/acceptance.py`

`./mach python tools/m6e/acceptance.py --app <M6E.app> --out <dir> [--idle-seconds N] [--startup-runs N] [--only a,b] [--list] [--headless] [--lock-dir DIR]`. Defaults: 300 s idle, 5 startup runs. `--list` names the 15 check keys (bundle, branding, network, theme, search, removed, bookmarks, reader, shield, tracker, push, telemetry, d25, hardening, performance).
- On an unpackaged dev bundle (`obj-m6e/dist/M6E.app`) exactly two items fail: `codesign verifies` (App bundle) and `0 string files` (Branding; the strings are in `omni.ja`, which a dev bundle lacks). That gives 13 of 15; they must pass on the packaged app (15 of 15).
- Packaged run: mount the DMG from `artifacts/release/`, copy `M6E.app` to `/Users/Shared` (never `/tmp`: page-load hangs, plan A14), run with the default idle time and without `--headless`, quiet machine, no other M6E.
- Check "Removed features are absent" for both the dev and packaged app (26 of 26 in P6.1c to e; it counted 29 in Phase 5). The `Crash reporter` constant row passes because `AppConstants.MOZ_CRASHREPORTER` no longer exists.

## 7. `tools/m6e/package/release.sh [output dir]`

Default output `artifacts/release`. Nothing may be mounted at `/Volumes/M6E`. It runs `./mach package`, copies `obj-m6e/dist/m6e/M6E.app` to a temp directory, `sign-adhoc.sh` (ad hoc, D8; runs `finalize-app.sh` first), `make-dmg-layout.py build`, `make-dmg-layout.py verify --require-signed`, and `chrome_eval.py --script tools/m6e/package/check-signed.js --headless` on the signed app. Result: `artifacts/release/M6E-<version>-macos-arm64.dmg`. Look at the DMG in Finder with the screen unlocked, install it to `/Applications` and launch it through LaunchServices.

## 8. `tools/m6e/measure.py`

`./mach python tools/m6e/measure.py --binary <M6E.app>/Contents/MacOS/m6e --out <dir> [--startup-runs 10] [--idle-seconds 300]`; writes `results.json` and `summary.md`. Optional: `tools/m6e/speedometer.py --binary ... --out ... [--rounds 2 --iterations 10]`. Quiet-machine rule: the user's own M6E quit, no builds, tests or git activity (a run during git activity gave 816 ms instead of 597); use the 10-run median; blank-tab memory is noisy by about 30 MB, compare the "all but GPU" total. Use the packaged app, outside `/tmp`. Reference: Phase 5 first paint 579 ms, blank-tab memory 443 MB; Phase 4 Speedometer 44.7.

## 9. Record, and known failures

Record in the plan: a "P6.3 (done ...)" bullet and the progress-log row with the commit; build time, run_tests and consumer-suite results compared with this list, acceptance (dev 13 of 15, packaged 15 of 15), first paint, memory, Speedometer, DMG name and size; and remove the stale "builds without LTO and PGO" text. In the spec: Phase 6 status row in section 2, and D30 as done.

Failures that exist without Phase 6 (not regressions):
- P6.0 baseline: `https-only/test_resource_upgrade.html` and `browser_console_logging.js` (network DNS answers nonexistent hosts); reader `browser_localfile_readerMode.js` (symlinked `_tests` path); modules `test_GMPInstallManager.js` (fixed Widevine/OpenH264 URLs, D10), `test_Region.js`, `test_Region_geocoding.js` (region lookup gone), `test_Services.js` (race, passes alone); policies `test_policy_search_engine.js`, `test_simple_pref_policies.js`, timeout in `test_extensionsettings.js` (its update-checker errors disappeared with P6.1g); pagetelemetry `browser_m6e_page_telemetry.js` `test_panel_values` failed in the baseline but passed (10 passes) with an unlocked display in Phase 5: confirm.
- Flaky, pass alone: `trustpanel` `browser_trust_panel*.js` (a different file times out per run, e.g. `test_https_with_tls_key_logging`); `browser_search_results_back_button.js`; `places/tests/sync` `test_migrateOldTrackerEntries` (equal timestamps); `test_FinderIterator.js` `test_parallel_starts` (`4000 < 4000` on an idle machine).
- Caused by M6E locks (P6.1d, e): startup-unit `test_defaults_no_prefs_set` (`browser.ai.control.default` locked blocked); pdf.js `test_inherits_forced_viewer_theme` (viewer forced light); sidebar `browser_vertical_tabs.js` (telemetry locked off), `browser_toolbar_sidebar_button.js` `test_toolbar_sidebar_badges`, `browser_sidebar_context_menu.js` (`extensions.abuseReport.enabled` locked), `browser_a11y_sidebar.js`, `browser_customize_sidebar.js` (Got 3, expected 5).
- P6.1c: `browser_contextmenu.js` `test_imagelink` hangs headless; stylelint reports 3 errors in `toolbarbuttons.css` on untouched lines.
- P6.1f: backup-unit 22 failures (`app.support.baseURL` empty, "Cannot recover a backup from firefox in m6e"), `protections` and `profiles-br` fail identically on the parent; timeouts in `aboutlogins`, `aboutwelcome-br`, `asrouter-br` and the preferences search sub-pane vary by which test reaches the 370 s limit.

Not verified when this runbook was written: it was written without a build, so no command was run. The commit hashes are those of `origin/m6e-p6/ready-p63` at the time; the artifact paths under `artifacts/p6/p63` are a convention, not required by any script.
