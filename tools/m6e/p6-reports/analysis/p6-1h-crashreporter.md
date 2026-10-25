# P6.1h analysis: the crash reporter (integration order 8)

Base commit cb002d4874. Read-only analysis. Searches were `git grep -n -I` over the whole tree, plus a read of every hit that is not a test or a doc.

## Summary

The crash reporter is already compiled out. `mozconfig` has `--disable-crashreporter`, `obj-m6e/config.status` has no `MOZ_CRASHREPORTER` (only `MOZ_CRASHREPORTER_URL`), and the plan lists it under "Not built". With the flag off:

- `toolkit/crashreporter/moz.build` builds only: `annotations/` (generated `CrashAnnotations.h` plus `CrashAnnotations.cpp`), `ExtraFileParser.cpp`, `nsExceptionHandlerUtils.cpp`, `nsDummyExceptionHandler.cpp` and `ipc/CrashReporterInitArgs.ipdlh`. Everything else sits behind `if CONFIG["MOZ_CRASHREPORTER"]` and is never read by the build.
- `toolkit/components/crashes/moz.build` builds only `nsICrashService.idl`. `CrashManager`, `CrashService`, `RemoteSettingsCrashPull`, `components.conf` and all tests are behind the same flag.
- `jar.mn` (about:crashes content) and the `crashes` entries in `nsAboutRedirector.cpp` and `docshell/build/components.conf` are behind the same flag.
- The in-process exception handler, the crash helper process, the client app, the minidump analyzer and the Rust crates are not compiled. The crates are only cargo workspace members, so cargo can resolve the workspace.

So deleting is almost free for the build. The work is (a) deleting about 1,500 files, (b) pruning the Cargo workspace, `Cargo.lock`, gkrust and the build config, (c) stripping `MOZ_CRASHREPORTER` branches in first-party code (D21), and (d) removing the report-sending UI and code in `browser/` that is already dead.

What cannot go (about 75 C++ files, 444 call sites): the C++ annotation API (`CrashReporter::RecordAnnotation*`, `AnnotateCrashReport`, and the rest of `nsExceptionHandler.h`), implemented by `nsDummyExceptionHandler.cpp`. Removing it means editing `ipc/`, `dom/`, `gfx/`, `xpcom/`, `js/`, `storage/`, `accessible/`, `netwerk/`, `toolkit/xre/` and more. The shipped binary would be unchanged because the stubs are empty, and the edits would have to be redone on every upstream merge. So the plan below keeps the stub API as the single remaining piece, and the unreachability proof for it is given under "Must stay".

Decisions for the preparer (all follow from the sections below):

1. Delete everything under `toolkit/crashreporter` except: `moz.build` (trimmed), `nsExceptionHandler.h`, `nsDummyExceptionHandler.cpp`, `CrashAnnotations.yaml`, `annotations/` and `ipc/CrashReporterInitArgs.ipdlh`. Delete `toolkit/components/crashes` except `moz.build` (trimmed) and `nsICrashService.idl`.
2. Remove the `--enable-crashreporter` option and its dependants from `toolkit/moz.configure`, and `--disable-crashreporter` from `mozconfig` (configure-time).
3. Remove the Cargo members, the gkrust features and the crate dependencies. Prune `Cargo.lock`.
4. Strip the dead report-sending code in `browser/` (`UnsubmittedCrashHandler`, `CrashFileCleaner`, the subframe-crash notification, the about:tabcrashed report form, the preferences checkbox, the onboarding checkbox, ASRouter crash targeting).
5. Keep `mozinfo["crashreporter"]` (it evaluates to `False` without edits), so the roughly 49 `run-if`/`skip-if = crashreporter` test conditions keep working.
6. `toolkit/components/crashmonitor` is NOT part of this feature. It is the shutdown-checkpoint file that session restore reads (`SessionStartup.sys.mjs`), has no reference to the crash reporter, and must stay. The hint listed "crash monitor", so this is flagged in "Risks".

## Files to delete

### Directories under `toolkit/crashreporter`

All of these are already outside the build. Counts are `git ls-files`.

| Path | Files | Notes |
|---|---|---|
| `google-breakpad/` | 422 | Vendored Breakpad. No remaining first-party include (see "Must stay" and "Risks" for the three LOCAL_INCLUDES that point at it). |
| `breakpad-client/` | 38 | Vendored/patched Breakpad client, mac and common. |
| `breakpad-patches/` | 27 | Patch series and README for `update-breakpad.sh`. |
| `breakpad-windows-libxul/` | 1 | moz.build only. |
| `breakpad_wrapper/` | 2 | |
| `client/` (`app`, `cocoabind`, `gtkbind`) | 66 | Rust crash reporter client, `crashreporter` crate. Cargo member `toolkit/crashreporter/client/app`. |
| `crash_helper/` | 3 | C++ launcher for the helper. |
| `crash_helper_client/` | 10 | Rust, Cargo member. |
| `crash_helper_common/` | 33 | Rust, not a member but a path dependency of the other two. |
| `crash_helper_server/` | 18 | Rust, Cargo member. |
| `crashping/` | 13 | Rust, Cargo member. Contains `pings.yaml`, `static_metrics.yaml`, `generated_metrics.yaml`, `glean_metrics.py` (mach command `crash-ping-metrics`). |
| `minidump-analyzer/` | 7 | Rust, listed under `exclude` in root `Cargo.toml`. |
| `mozannotation_client/`, `mozannotation_server/` | 4 + 3 | Rust and moz.build. |
| `mozwer/`, `mozwer-rust/` | 3 + 3 | Windows error reporting module; `mozwer-rust` is a Cargo member. |
| `process_reader/` | 7 | Rust path dependency. |
| `rust_minidump_writer_linux/` | 4 | Linux-only crate. Restored in P4.6a only so cargo could resolve the workspace; goes with this step. |
| `win32_process_mitigations/` | 8 | Windows-only crate, path dependency of `toolkit/library/rust/shared`. |
| `networking/` | 4 | `BackgroundTask_crashreporterNetworkBackend.sys.mjs` and its xpcshell test. |
| `content/` | 3 | about:crashes page (`crashes.html`, `.js`, `.css`). |
| `test/` | 86 | xpcshell, browser and gtest tests, `test_utils` (`nsICrashReporterTestUtils`), the `testcrasher` library. |
| `tools/` | 4 | `symbolstore.py`, `upload_symbols.py`, `unit-symbolstore.py`, `python.toml`. |
| `docs/` | 6 | Includes `Using_the_Mozilla_symbol_server.md`. |

### Loose files in `toolkit/crashreporter`

`CrashReports.sys.mjs`, `CrashSubmit.sys.mjs`, `ExtraFileParser.cpp`, `ExtraFileParser.h`, `bionic_missing_funcs.cpp`, `crashreporter.mozbuild`, `jar.mn`, `linux_utils.cc`, `linux_utils.h`, `mac_utils.h`, `mac_utils.mm`, `nsExceptionHandler.cpp`, `nsExceptionHandlerUtils.cpp`, `nsExceptionHandlerUtils.h`, `update-breakpad.sh`.

- `ExtraFileParser.*` and `nsExceptionHandlerUtils.*` are compiled today but only used by `nsExceptionHandler.cpp` (git grep finds no other user), so they go with it. `moz.build` must drop them from `UNIFIED_SOURCES` and `SOURCE_HEADERS`.
- `jar.mn` has only `#ifdef MOZ_CRASHREPORTER` entries (about:crashes). Remove it and `JAR_MANIFESTS += ["jar.mn"]`.

### What remains in `toolkit/crashreporter`

`moz.build` (trimmed), `nsExceptionHandler.h`, `nsDummyExceptionHandler.cpp`, `CrashAnnotations.yaml`, `annotations/`, `ipc/CrashReporterInitArgs.ipdlh`.

Trimmed `moz.build` keeps: `EXPORTS += ["nsExceptionHandler.h"]`, `PREPROCESSED_IPDL_SOURCES`, `include("/ipc/chromium/chromium-config.mozbuild")`, `FINAL_LIBRARY = "xul"`, `DIRS += ["annotations"]`, `UNIFIED_SOURCES = ["nsDummyExceptionHandler.cpp"]`, `BUG_COMPONENT`. Drop `SPHINX_TREES`, the `Files("docs/**")` block, `JAR_MANIFESTS`, the whole `if CONFIG["MOZ_CRASHREPORTER"]` branch, `LICENSED_UNDER` for `google-breakpad`, and the `SOURCE_HEADERS`.

Optional inside `annotations/`: `CrashAnnotations.java.in`, `CrashAnnotations.kt.in` and `crash_annotations.rs.in` have no consumer once the crates are gone and Android is gone. Deleting them needs the matching generator functions in `generate.py` to go too; skip unless the preparer wants the extra tidy-up. `load.py` and `CrashAnnotations.yaml` stay.

### Directory `toolkit/components/crashes`

Delete: `CrashManager.in.sys.mjs`, `CrashManagerTest.sys.mjs`, `CrashService.sys.mjs`, `RemoteSettingsCrashPull.sys.mjs`, `components.conf`, `docs/`, `gen_CrashManager.py`, `metrics.yaml`, `tests/` (21 files total, 2 stay).

Stays: `nsICrashService.idl` (C++ consumers, see "Must stay") and a trimmed `moz.build` with only `XPIDL_MODULE`, `XPIDL_SOURCES`, `DEFINES` removed (no longer needed) and `BUG_COMPONENT`. Optional alternative: `git mv` the IDL to `xpcom/system` next to `nsICrashReporter.idl` and delete the directory plus the `"crashes"` entry in `toolkit/components/moz.build`. This is safe (the IDL has no module-specific content) but changes the XPIDL module name; the preparer may pick either. Default: keep in place.

### Loose files elsewhere

| Path | Why |
|---|---|
| `toolkit/locales/en-US/crashreporter/crashreporter.ftl` | Client strings; only `CrashSubmit.sys.mjs` and the client app load it. |
| `browser/base/content/test/browser-tabcrashed/` (whole dir, 21 files) | Every manifest has `run-if = crashreporter`; they test report sending. Remove the three manifest lines in `browser/base/moz.build` (63-65). Also `docs/contributing/debugging/img/tabcrashed.png` can stay. |
| `browser/modules/test/browser/browser_UnsubmittedCrashHandler.js`, `browser_CrashFileCleaner.js` | Test the code removed below. Remove their entries from `browser/modules/test/browser/browser.toml` and the `Files(...)` entry in `browser/modules/moz.build` line 26. |
| `dom/ipc/tests/browser_CrashService_crash.js` | Tests `nsICrashService`/`CrashManager`; remove its `browser.toml` entry (line 10). |
| `toolkit/components/backgroundtasks/tests/BackgroundTask_crash.sys.mjs`, `BackgroundTask_crashReporterStatus.sys.mjs` | Only used by the deleted `crashreporter/test/unit` tests; remove from `TESTING_JS_MODULES.backgroundtasks` in `toolkit/components/backgroundtasks/moz.build` lines 73-74. |
| `browser/base/content/aboutFrameCrashed.html` | STAYS (it is the subframe-crash page, not a report page); see edits below. |

## References from staying code

Action key: remove = delete the line or block; edit = change as described. Every file listed is first-party and stays.

### Build system and Rust

| File | What | Action |
|---|---|---|
| `Cargo.toml` | members `toolkit/crashreporter/client/app`, `crash_helper_server`, `crash_helper_client`, `crashping`, `mozwer-rust` (lines 68-72); exclude `toolkit/crashreporter/minidump-analyzer` (line 98) | remove all six lines |
| `Cargo.lock` | packages `crash_helper_client`, `crash_helper_common`, `crash_helper_server`, `crashping`, `crashreporter`, `minidump-analyzer`, `mozannotation_client`, `mozannotation_server`, `mozwer_s`, `process_reader`, `rust_minidump_writer_linux`, `win32_process_mitigations`, and the crates only they pull in (`minidump`, `minidump-common`, `minidump-unwind`, `minidump-writer` (git), `breakpad-symbols`, `crash-context`, possibly `hyper`/`rkv`/`tokio` features) | remove the crate entries and their names from the dependency lists of `gkrust-shared` (lines around 3015-3092). Regenerate with cargo offline if possible; otherwise hand-prune and let the integration build confirm. |
| `build/workspace-hack/Cargo.toml` | feature lines `crash_helper_server`, `crashreporter`, `crashping`, `crashreporter-android-export`, `mozwer_s` (lines 271-280) | remove the five lines |
| `toolkit/library/rust/shared/Cargo.toml` | deps `rust_minidump_writer_linux`, `mozannotation_client`, `mozannotation_server`, `crash_helper_client` (69-72); Windows dep `win32_process_mitigations` (150); features `oxidized_breakpad`, `crashreporter` (169-170) | remove |
| `toolkit/library/rust/shared/lib.rs` | `extern crate rust_minidump_writer_linux`, `mozannotation_client`, `crash_helper_client` (129-136), `win32_process_mitigations` (145-146) | remove the 4 `extern crate` items with their `#[cfg]` |
| `toolkit/library/rust/gkrust-features.mozbuild` | `if CONFIG["MOZ_CRASHREPORTER"]: gkrust_features += ["crashreporter"]` and the nested `MOZ_OXIDIZED_BREAKPAD` block (66-69) | remove |
| `toolkit/library/rust/moz.build` | `RUST_TESTS`: `"crashreporter"`, `"crash_helper_common"` (24-25), `RUST_TESTS += ["crash_helper_server"]` and its comment (39-40); `RUST_TEST_FEATURES.append("crashreporter/mock")` (43) and `"crashping/gecko_tests"` (44) | remove |
| `build/rust/mozbuild/generate_buildconfig.py` | `MOZ_CRASHREPORTER_MOCK` (146-147), `MOZ_APP_ID`, `MOZ_CRASHREPORTER_URL`, `MOZ_UPDATE_CHANNEL` (156-158, "Used by crash_helper_common") | remove those lines and their comments. The only Rust readers of these constants are inside the deleted crates (git grep of `*.rs`). Check `MOZ_UPDATE_CHANNEL` is not used by an updater crate that is still in the tree when this lands (the updater, order 7, is integrated earlier). |
| `.cargo/config.toml.in` | `[source."git+https://github.com/rust-minidump/minidump-writer.git?rev=..."]` (122-123 and its `replace-with`) | remove the section once `Cargo.lock` no longer references the git source |
| `third_party/rust/{minidump,minidump-common,minidump-unwind,minidump-writer,breakpad-symbols,crash-context}` | vendored crates, orphaned after the lock is pruned | leave in the tree (D21: vendored code). A later `./mach vendor rust` prunes them; not needed for the build. |
| `supply-chain/config.toml`, `audits.toml`, `imports.lock` | `[policy.minidump-writer]` and audits for the removed crates | leave (cargo-vet data, not used by the build). P6.2 may prune. |
| `toolkit/moz.configure` | `--with-crashreporter-url` / `MOZ_CRASHREPORTER_URL` (3423-3438), `MOZ_OXIDIZED_BREAKPAD` (3441-3452), `MOZ_CRASHREPORTER_MOCK` (3454-3456), `crashreporter_default` and `--enable-crashreporter` with `MOZ_CRASHREPORTER` config and define (3495-3517), the `MOZ_GTHREAD` block under `--enable-crashreporter` (3519-3525) | remove all. In `data_reporting` (3532-3538) drop the `"--enable-crashreporter"` input and the `crashreporter` parameter. Leave the `getcontext` Linux block at 3175 alone (Linux code, outside this feature). |
| `mozconfig` | `ac_add_options --disable-crashreporter` | remove in the same commit (an unknown option is a configure error) |
| `toolkit/moz.build` | `DIRS` entry `"crashreporter"` (15); `TEST_HARNESS_FILES.testing.mochitest.browser.toolkit.crashreporter.test.browser += ["crashreporter/test/browser/crashreport.sjs"]` (47-48) | keep the `crashreporter` DIRS entry (the stub stays); remove the `TEST_HARNESS_FILES` block (the file is deleted; mozbuild errors on a missing source) |
| `toolkit/components/moz.build` | `"crashes"` in DIRS (38) | keep if `nsICrashService.idl` stays in place; remove only if the IDL is moved |
| `toolkit/modules/moz.build` | `"MOZ_CRASHREPORTER"` in the `AppConstants` defines list (330) | remove |
| `toolkit/modules/AppConstants.sys.mjs` | `MOZ_CRASHREPORTER: @MOZ_CRASHREPORTER_BOOL@,` (88) | remove. Every JS reader (below) is edited or tolerates `undefined`. |
| `tools/@types/subs/AppConstants.sys.d.mts` | `MOZ_CRASHREPORTER` type | remove |
| `build/moz.build` | `"MOZ_CRASHREPORTER_URL"` in the application.ini var list (68); `"MOZ_CRASHREPORTER"` in `("MOZ_CRASHREPORTER", "MOZ_PROFILE_MIGRATOR", "MOZ_UPDATER")` (81) | remove both (a `CONFIG[var]` on a deleted config var for line 68 would otherwise break) |
| `build/application.ini.in` | `#if MOZ_CRASHREPORTER` / `[Crash Reporter]` section with `Enabled=1` and `ServerURL=` (47-50) | remove the block |
| `build/appini_header.py` | `NS_XRE_ENABLE_CRASH_REPORTER` flag mapping (line 23) | optional, see "Tier 2" below |
| `Makefile.in` | `ifdef MOZ_CRASHREPORTER include package-name.mk` (115-117); `buildsymbols` branch (147-152); `uploadsymbols` calling `toolkit/crashreporter/tools/upload_symbols.py` (155-158) | remove the `ifdef`s and the `uploadsymbols` target. `symbolsarchive`/`symbolsfullarchive` stay only if something else needs them; otherwise remove. Make-level, front-end class. |
| `config/rules.mk` | `syms_template` (545-555) and the `else ifdef MOZ_CRASHREPORTER` branch building `*_syms.track` (580-582) | remove both (dead without the flag) |
| `python/mozbuild/mozbuild/action/dumpsymbols.py`, `unify_symbols.py`, `mach_commands.py` (`symbols`), `toolkit/mozapps/installer/package-name.mk`, `packager.mk` | symbol dumping via the crash reporter's `dump_syms`/`crashreporter-symbols` | out of scope here. Leave; flag for P6.2 if the preparer wants the symbols pipeline gone. |
| `browser/installer/package-manifest.in` | `[Crash Reporter]` block (`crashreporter.app`, `crashreporter`, `mozwer`) and `[ crashhelper ]` block (394-411) | remove both blocks (all under `#ifdef MOZ_CRASHREPORTER`) |
| `browser/installer/allowed-dupes.mn` | `crashreporter.app/Contents/Resources/English.lproj/MainMenu.nib/classes.nib` and `MainMenuRTL...` (43-44) | remove |
| `browser/app/macbuild/Contents/moz.build` | `"MOZ_CRASHREPORTER"` in the `-D` list (12) | remove |
| `browser/app/macbuild/Contents/MacOS-files.in` | `#if defined(MOZ_CRASHREPORTER) /crashhelper #endif` (16-18) | remove |
| `toolkit/locales/jar.mn` | `crashreporter (%crashreporter/**/*.ftl)` (7) | KEEP (see "Must stay": `aboutcrashes.ftl` is a dom/l10n test fixture) |
| `python/mach/mach/command_util.py` | `"crash-ping-metrics": MachCommandReference("toolkit/crashreporter/crashping/glean_metrics.py")` (96-98) | remove the entry |
| `python/mozbuild/mozbuild/mozinfo.py` | `d["crashreporter"] = bool(substs.get("MOZ_CRASHREPORTER"))` (83) | NO CHANGE. `substs.get` returns `None` once the config var is gone, so the key becomes `False`, which is the value the build has today. About 49 test conditions rely on the key. |
| `build/dumbmake-dependencies` | `toolkit/crashreporter` (65) | no change (the directory remains); only the line with the removed `test` subpath, if any |
| `tools/profiler/moz.build` | `LOCAL_INCLUDES` `/toolkit/crashreporter/google-breakpad/src` (79) | remove: mozbuild raises "Path specified in LOCAL_INCLUDES does not exist" for a missing directory |
| `toolkit/components/telemetry/pingsender/moz.build` | `LOCAL_INCLUDES` `/toolkit/crashreporter/google-breakpad/src` (13) | remove, same reason (pingsender does not include any Breakpad header) |
| `js/src/make-source-package.py` | `+ /toolkit/crashreporter/tools/symbolstore.py` (195) | remove the line |
| `tools/use-moz-src/mach_commands.py`, `tools/ts/config/fixed_paths.js` | `CrashManager.sys.mjs` mapping (lines 62 and 44-45) | remove the entries |
| `toolkit/components/glean/metrics_index.py` | `"toolkit/components/crashes/metrics.yaml"` three times (156, 173, 183) | remove all three. Regenerates FOG headers (C++ rebuild of Glean-dependent files). |
| `toolkit/components/glean/tags.yaml` | tag `'Toolkit :: Crash Reporting'` (486) | optional; unused after the removal |
| `mots.yaml` | the crash reporting module paths (1703-1715) | remove the module |
| Lint, format and ignore lists | `.clang-format-ignore` 199-200, `.prettierignore` 1548-1549, `.gitignore` 417, `.hgignore` 407, `tools/lint/clippy.yml` 91-102, `tools/lint/codespell.yml` 47, `tools/lint/dot-mozilla-reference.yml` 46, `tools/lint/glean-parser.yml` 10, `tools/lint/extra-js-modules/allowlist.txt` 6, 302, 303, 538, `eslint-rollouts.config.mjs` (many), `stylelint-rollouts.config.js` 269, `tools/rewriting/ThirdPartyPaths.txt` 203-204, `build/sparse-profiles/{taskgraph,toolchain-build,upload-symbols}` | entries for paths this branch deletes. Remove them here or leave them for P6.2, as the lead decides; they do not affect the build. |
| taskcluster (`kinds/upload-symbols`, `system-symbols-upload`, `toolchain-symbols-upload`, `toolchain/minidump-stackwalk.yml`, `scripts/misc/build-dxc.sh`, `build-onnxruntime.sh`, `gecko_taskgraph/transforms/openh264_symbol_upload.py`, `test/params/mc-nightly-all.yml`, `test/data/bugbug-push-schedules.json`) | CI that uses `symbolstore.py`/`upload_symbols.py` and the Breakpad processor | leave. M6E does not use taskcluster. If a taskcluster consistency check is part of the validation, remove the rows that name deleted paths. |

### C++ and IPDL (first-party `#ifdef MOZ_CRASHREPORTER` stripping, D21)

For every block below, the `MOZ_CRASHREPORTER` branch is dead (flag off). Strip with the false branch, i.e. delete the block.

| File | What | Action |
|---|---|---|
| `docshell/base/nsAboutRedirector.cpp` | `#ifdef MOZ_CRASHREPORTER { "crashes", "chrome://global/content/crashes.html", ...}` (98-101) | remove the entry and the `#ifdef`/`#endif` |
| `docshell/build/components.conf` | `if defined('MOZ_CRASHREPORTER'): about_pages.append('crashes')` (38-39) | remove |
| `dom/ipc/BrowserChild.cpp` (~3946) | `#ifdef MOZ_CRASHREPORTER` URL annotation using `CrashReporter::GetEnabled()` | remove the block |
| `dom/ipc/ContentChild.cpp` (~4164, ~4198) | two URL annotation blocks | remove both |
| `xpcom/base/AvailableMemoryWatcher.cpp` (108-115) | `SetInactiveStateStart` / `ClearInactiveStateStart` under `#ifdef MOZ_CRASHREPORTER` | remove both blocks. The two functions do not exist in the dummy file, so the blocks are compiled out today. |
| `ipc/glue/CrashReporterClient.cpp` (8-12, 46-55) | `#if defined(XP_LINUX) && defined(MOZ_CRASHREPORTER) && defined(MOZ_OXIDIZED_BREAKPAD)` include of `rust_minidump_writer_linux_ffi_generated.h` and the `DirectAuxvDumpInfo` block | remove both (left over from the Linux strip) |
| `ipc/glue/CrashReporterHost.cpp` (17-21, 38-47, 52-56) | same triple-condition blocks (`RegisterChildAuxvInfo`, `UnregisterChildAuxvInfo`) | remove all three; the constructor and destructor bodies become empty |
| `toolkit/crashreporter/ipc/CrashReporterInitArgs.ipdlh` | `#if defined(XP_LINUX) && ... AuxvInfo ... #else ... #endif` | keep only the `#else` struct (`NativeThreadId threadId`), drop the preprocessor. Check `PREPROCESSED_IPDL_SOURCES` can stay as is. |
| `toolkit/crashreporter/nsExceptionHandler.h` | `IsDummy()` (`#ifdef MOZ_CRASHREPORTER return false; #else return true;`, 48-52) and the `AutoRecordAnnotation` private members under `#ifdef MOZ_CRASHREPORTER` (133-138) | `IsDummy()` returns `true` (keep the function; `toolkit/xre/nsEmbedFunctions.cpp:252` calls it); drop the private members |
| `toolkit/xre/components.conf` | `if defined('MOZ_CRASHREPORTER'): crash_reporter = ['@mozilla.org/toolkit/crash-reporter;1']` else `[]` | replace by the empty list: remove the variable and `+ crash_reporter` (the contract is not registered today). Keep `'nsICrashReporter'` in `interfaces`; see "Must stay". |
| `xpcom/base/AvailableMemoryWatcherWin.cpp`, `AvailableMemoryWatcherLinux.cpp`, `mozglue/baseprofiler/core/shared-libraries-linux.cc`, `ipc/chromium/src/chrome/common/process_watcher_win.cc`, `process_watcher_posix_sigchld.cc`, `toolkit/xre/nsX11ErrorHandler.cpp`, `tools/profiler/lul/*`, `tools/profiler/tests/gtest/Lul*` | Windows/Linux-only files that mention crash reporter APIs or Breakpad in comments | out of scope (platform code that D21 should have removed); no action for this step |

### Browser and toolkit JS, XHTML, FTL

| File | What | Action |
|---|---|---|
| `browser/modules/ContentCrashHandlers.sys.mjs` | (a) lazy getters `CrashReports`, `CrashSubmit`, `RemoteSettingsCrashPull` (11-14); (b) `UnsubmittedCrashHandler` and `CrashFileCleaner` (whole objects, 765-1397) and their constants (`PENDING_CRASH_REPORT_DAYS`, `DAYS_TO_SUPPRESS`, `SILENCE_FOR_DAYS_IN_S`, `CHECK_FOR_UNSUBMITTED_CRASH_REPORTS_DELAY_MS`, `CRASH_FILE_CLEANUP_DELAY_MS`, `CLEANUP_INTERVAL_DAYS`, `INSTALL_TIME_MAX_AGE_MS`, `REPORT_MAX_AGE_MS`, `MAX_PENDING_REPORTS`); (c) in `TabCrashHandler`: the `else if (AppConstants.MOZ_CRASHREPORTER)` block that sets `childMap` and calls `showSubFrameNotification` (128-143), `showSubFrameNotification` (376-460), `maybeSendCrashReport` (579-641), `removeSubmitCheckboxesForSameCrash`, `getDumpID`, the `UnsubmittedCrashHandler.submitReports` call at 497-505 inside `willShowCrashedTab`, the `MOZ_CRASHREPORTER` guards at 320, 580, 735, the `browser.tabs.crashReporting.*` prefs getter, and the `dumpID` plumbing in `onAboutTabCrashedLoad` (return no report) | edit as listed. Keep `TabCrashHandler` (tab-crash page, restore buttons, `MOZ_CRASHREPORTER_SHUTDOWN` harness handling), `BrowserWeakMap`, `ipc:content-shutdown` and `oop-frameloader-crashed` handling. Remove now-unused constants (`TABCRASHED_ICON_URI`, `SUBFRAMECRASH_LEARNMORE_URI`, `MAX_UNSEEN_CRASHED_SUBFRAME_IDS`, `pendingSubFrameCrashes*` if nothing else reads them: check with git grep inside `browser/`). Preparer must re-read the file; it is 1,397 lines and about half goes. |
| `browser/actors/AboutTabCrashedParent.sys.mjs` | `SetCrashReportAvailable` message (39) and three `maybeSendCrashReport` calls (44, 50, 56) | remove; the closeTab/restoreTab/restoreAll cases keep their `SessionStore` calls |
| `browser/base/content/aboutTabCrashed.js` | `hasReport`, `sendReport`, `includeURL`, `comments`, `autoSubmit`, `requestAutoSubmit`, `showCrashReportUI`, `CLICK_TARGETS` entry `sendReport`, the report payload sent on close/restore | remove the report handling; keep the buttons and the count update |
| `browser/base/content/aboutTabCrashed.xhtml` | `#reportBox` (request help, send-report checkbox, comments, include-URL, auto-submit), `#reportSent` | remove |
| `browser/base/content/aboutTabCrashed.css`, `browser/themes/shared/aboutTabCrashed.css` | selectors for `#reportBox`, `#options`, `#sendReport`, `#reportSent`, `#comments`, `#requestAutoSubmit` | remove those rules |
| `browser/locales/en-US/browser/aboutTabCrashed.ftl` | `crashed-request-help`, `crashed-request-help-message`, `crashed-request-report-title`, `crashed-send-report-2`, `crashed-comment`, `crashed-include-URL-2`, `crashed-report-sent`, `crashed-request-auto-submit-title`, `crashed-auto-submit-checkbox-2` (13-22) | remove. The page title and the header text stay; `crashed-title = Tab crash reporter` may be renamed to avoid the phrase "crash reporter" (user-visible; not required). |
| `browser/locales/en-US/browser/contentCrash.ftl` | all messages except `crashed-subframe-title` (needed by `aboutFrameCrashed.html`) | keep only `crashed-subframe-title`. Its text ends with "please submit a report"; since M6E cannot submit, edit it to "Part of this page crashed." (user-visible, M6E-marked). Delete `crashed-subframe-message`, `-learnmore-link`, `-submit`, and the `pending-crash-reports-*` and `requested-crash-reports-*` messages. |
| `browser/components/BrowserGlue.sys.mjs` | lazy getters for `CrashFileCleaner` and `UnsubmittedCrashHandler` under `if (AppConstants.MOZ_CRASHREPORTER)` (119-124); the init calls (839-844) | remove both blocks |
| `browser/actors/PluginParent.sys.mjs` | lazy `CrashSubmit` (10); `submitCrashReport` (65-95); the `if (AppConstants.MOZ_CRASHREPORTER)` submit-button block (148-157) | remove all three; keep the plugin-crash notification itself |
| `browser/base/content/browser.js` | `if (AppConstants.MOZ_CRASHREPORTER && aLocationURI) { ... annotateCrashReport("URL", ...) }` (2426-2442) | remove the block |
| `browser/components/preferences/config/permissions-data.mjs` | pref list entry `browser.crashReports.unsubmittedCheck.autoSubmit2` (67); the `automaticallySubmitCrashesBox` setting (464-469); its `deps` entry (~475); the item with `l10nId: "data-collection-backlogged-crash-reports"` (857-861) | remove all four |
| `browser/locales/en-US/browser/preferences/preferences.ftl` | `data-collection-backlogged-crash-reports` (2226), `collection-backlogged-crash-reports2` (2249), `collection-backlogged-crash-reports-description` (2252) | remove |
| `browser/components/asrouter/modules/OnboardingMessageProvider.sys.mjs` | the `crash-data` checkbox object with `SET_PREF browser.crashReports.unsubmittedCheck.autoSubmit2` (3472-3500) | remove the object |
| `browser/locales/en-US/browser/preonboarding.ftl` | `preonboarding-checklist-crash-reports-label`, `-description` (35-36) | remove |
| `browser/components/asrouter/modules/ASRouterTriggerListeners.sys.mjs` | `"browser.crashReports.unsubmittedCheck.autoSubmit2"` in `_trackedPrefs` (200) | remove the entry |
| `browser/components/backup/resources/PreferencesBackupResource.sys.mjs` (50), `browser/components/profiles/SelectableProfileService.sys.mjs` (366) | same pref in sync/backup lists | remove the entries |
| `browser/components/asrouter/modules/ASRouterTargeting.sys.mjs` | `QueryCache` getter `crashData` (574-590); `Environment` getters `crashCount`, `daysSinceLastCrash`, `crashCountInLastDay`, `crashCountInLastWeek` (1787-1840) | remove. `Services.crashmanager` is already `undefined`, so the getters return `[]`/`null` today. Also `browser/components/asrouter/docs/targeting-attributes.md` 1650-1680 and the matching tests in `browser/components/asrouter/tests/browser/browser_asrouter_targeting.js` (~3450-3570). |
| `toolkit/modules/Troubleshoot.sys.mjs` | `if (AppConstants.MOZ_CRASHREPORTER) { dataProviders.crashes = ... CrashReports ... }` (1085-1099); also `kMaxCrashAge` if nothing else uses it | remove the block |
| `toolkit/content/aboutSupport.xhtml` | `#ifdef MOZ_CRASHREPORTER` crashes section (286-306) | remove |
| `toolkit/content/aboutSupport.js` | `crashes(data)` handler (264-300) and its registration | remove |
| `toolkit/locales/en-US/toolkit/about/aboutSupport.ftl` | `crashes-title`, `crashes-id`, `crashes-send-date`, `crashes-all-reports`, `crashes-no-config` (11-15), `report-crash-for-days`, `crashes-time-minutes/-hours/-days`, the `crashes-all-reports` plural at ~285 | remove |
| `toolkit/mozapps/extensions/internal/XPIProvider.sys.mjs` | `if (AppConstants.MOZ_CRASHREPORTER)` at 2796 (add-ons annotation); `addAddonsToCrashReporter` (3092-3105) and its callers (2127, 2168, 2807) | remove the guarded block; `addAddonsToCrashReporter` may stay as a no-op on the stub, but removal is cleaner. Test helper `AddonTestUtils.sys.mjs` (549, 566, `crashReporter: true` in fake appinfo) leaves as is. |
| `toolkit/components/telemetry/app/TelemetrySend.sys.mjs` | `_annotateCrashReport` (845-880) uses `Cc["@mozilla.org/toolkit/crash-reporter;1"]`; the contract is not registered so it already returns early | no action required. Optional: remove `_annotateCrashReport` and its 3 callers, plus `toolkit/components/telemetry/tests/unit/test_TelemetrySend.js` 959-1034. Telemetry is not part of this feature; leave. |
| `testing/specialpowers/content/SpecialPowers*.sys.mjs`, `testing/modules/AppInfo.sys.mjs`, `testing/mochitest/BrowserTestUtils/BrowserTestUtils.sys.mjs`, `testing/xpcshell/head.js`, `mozcrash` and the other `testing/` runners | harness support for `crashmanager`, `minidumps`, `MOZ_CRASHREPORTER*` env vars | no action: test infrastructure, harmless when the feature is absent (`SPProcessCrashService` finds no crash service). `MOZ_CRASHREPORTER_SHUTDOWN` handling in `ipc/glue/CrashReporterHelper.h` and `ContentCrashHandlers` is used by the harness and stays. |
| `tools/m6e/acceptance.py` | `REMOVED_CONSTANTS["Crash reporter"] = "MOZ_CRASHREPORTER"` (191) | no action: the check passes when the constant is absent (`not undefined`). |

### Prefs

| File | Pref | Action |
|---|---|---|
| `browser/app/profile/firefox.js` | `breakpad.reportURL` (1555-1556); `browser.tabs.crashReporting.sendReport`, `.includeURL` (2742-2744); `browser.crashReports.unsubmittedCheck.enabled` (both branches), `.chancesUntilSuppress`, `.autoSubmit2`, `browser.crashReports.cleanupCheck.enabled` (2870-2888); `services.sync.prefs.sync.browser.crashReports.unsubmittedCheck.autoSubmit2` (1635) | remove all except the `services.sync...` line, which belongs to Sync (earlier feature, gone by then) |
| `browser/app/profile/m6e.js` | the "Crash reporting is not built" block (47-50) with the three locked prefs `browser.crashReports.unsubmittedCheck.enabled`, `.autoSubmit2`, `browser.tabs.crashReporting.sendReport` | remove together with the code that reads them. Keep `dom.ipc.tabs.createKillHardCrashReports=false` (284, read by `ContentParent.cpp`) and `dom.reporting.crash.enabled=false` (290, read by `BrowserParent.cpp`): both are C++ prefs that stay. |
| `modules/libpref/init/StaticPrefList.yaml` | `browser.crashReporter.memtest` and `memtestKinds` (1525-1535; only reader was `nsExceptionHandler.cpp`); `browser.crashReports.onDemand`, `requestedNeverShowAgain`, `dontShowBefore` (1537-1553; only readers were the deleted JS) | remove the five entries. Keep `toolkit.crash_annotation.testing_validation` (read by `nsAppRunner.cpp:1226`), `dom.ipc.tabs.createKillHardCrashReports`, `dom.reporting.crash.enabled`. This regenerates `StaticPrefList*.h`: wide C++ rebuild; batch with the other features' StaticPrefList edits. |
| `modules/libpref/init/all.js` | `toolkit.crashreporter.include_context_heap` (3707-3713) | keep for now: `nsAppRunner.cpp:4465` still reads it and calls the stub `SetIncludeContextHeap`. Remove together with that call only if Tier 2 is done. `tools/m6e/merge/nightly-prefs-reviewed.txt:54` then also loses the line. |

## References from earlier features (ignore)

Code that belongs to features integrated before this one (VPN, Translations, GenAI, ML, Sync, Mozilla accounts, updater). These will already be gone or edited.

- `toolkit/mozapps/update/tests/data/xpcshellUtilsAUS.js` (`MOZ_CRASHREPORTER_DISABLE` environment handling, lines 5420-5433) and `toolkit/mozapps/update/` comments (updater, order 7).
- `toolkit/components/ml/tests/browser/browser_ml_native.js` (164-182, `AppConstants.MOZ_CRASHREPORTER`, minidump cleanup) and `browser_ml_textgeneration_telemetry.js` (47-60) (ML, order 4).
- `browser/app/profile/firefox.js:1635` `services.sync.prefs.sync.browser.crashReports.unsubmittedCheck.autoSubmit2` (Sync, order 5).
- `browser/components/backup` and `browser/components/profiles` list entries for the autoSubmit pref are staying code, handled above, not an earlier feature.

## References from later features (handle here)

Nimbus (order 9) is the only later feature that touches this one:

- `toolkit/components/nimbus/ExperimentAPI.sys.mjs` lines 38-39: `const CRASHREPORTER_ENABLED = AppConstants.MOZ_CRASHREPORTER && AppConstants.MOZ_APP_NAME !== "thunderbird"`, used at 361 (crash report annotations) and `Services.appinfo.crashReporterEnabled` at 579. After `AppConstants.MOZ_CRASHREPORTER` is removed the constant evaluates to `undefined` (falsy), so behaviour is unchanged and no edit is required for correctness. Recommended: leave it, since Nimbus is deleted in P6.1i. If a clean tree matters at this commit, set the constant to `false` and drop the branches.

## Prefs/metrics/strings to remove

Prefs: see the Prefs table above.

Glean:
- `toolkit/components/crashes/metrics.yaml`: categories `crash` (`compressed_store_size`, `submit_attempt`, `submit_success`) and `crash_submission` (`success`, `failure`, `pending`, `collector_errors`, `channel_status`, `failure_event`). Deleted with the file. Remaining Glean users: `ContentCrashHandlers.sys.mjs:915` (`Glean.crashSubmission.pending`, in code removed above) and the browser-tabcrashed/UnsubmittedCrashHandler tests (deleted).
- `toolkit/crashreporter/crashping/{pings.yaml,static_metrics.yaml,generated_metrics.yaml}` deleted with the crate. `Glean.browserContentCrash.dumpUnavailable` (`browser/modules/metrics.yaml`) and `glean::subprocess::crashes_with_dump` (`CrashReporterHost.cpp`) are outside the feature and stay.
- `telemetry_mirror` targets `CRASH_STORE_COMPRESSED_BYTES`, `PROCESS_CRASH_SUBMIT_ATTEMPT`, `PROCESS_CRASH_SUBMIT_SUCCESS` in `toolkit/components/telemetry/Histograms.json` (1291, 1838, 1871) and `histogram-allowlists.json` stay unless the preparer wants them gone; removing them regenerates the Telemetry enum headers (wide C++ rebuild) for no functional gain. Recommend: leave; log for P6.2.

Strings (FTL):
- `toolkit/locales/en-US/crashreporter/crashreporter.ftl` deleted (file).
- `browser/.../aboutTabCrashed.ftl`, `contentCrash.ftl`, `preferences.ftl`, `preonboarding.ftl`, `toolkit/about/aboutSupport.ftl`: messages as listed in the table.
- `browser/locales/en-US/browser/profile/default-bookmarks.ftl:32` is only a comment line mentioning about:crashes; leave.
- `toolkit/locales/en-US/crashreporter/aboutcrashes.ftl` STAYS (see "Must stay").

Other registrations to drop: `@mozilla.org/crashservice;1` and `@mozilla.org/crashmanager;1` (components.conf in `toolkit/components/crashes`, not registered today), `@mozilla.org/toolkit/crashreporter-test-utils;1` (`test_utils`), about:crashes (`nsAboutRedirector.cpp`, `docshell/build/components.conf`, jar content), `Services.crashmanager`.

## Build-system edits

Flag per class:

| Edit | Class |
|---|---|
| `toolkit/moz.configure` options and config/defines; `mozconfig` | configure-time. Because `MOZ_CRASHREPORTER` and `MOZ_OXIDIZED_BREAKPAD` are already undefined for this target, no compile define changes; `MOZ_CRASHREPORTER_URL` disappears from substs (only `application.ini` uses it). Expect a reconfigure and an incremental build, but the project rule says treat any configure change as a possible 40 min to 2 h full rebuild; batch with the other features. |
| `Cargo.toml`, `Cargo.lock`, `build/workspace-hack/Cargo.toml`, `toolkit/library/rust/shared/{Cargo.toml,lib.rs}`, `gkrust-features.mozbuild`, `toolkit/library/rust/moz.build`, `generate_buildconfig.py`, `.cargo/config.toml.in` | Rust (gkrust and the workspace re-resolve; gkrust relinks; the generated `buildconfig` crate changes) |
| `toolkit/crashreporter/moz.build`, `toolkit/components/crashes/moz.build`, `toolkit/moz.build`, `toolkit/modules/moz.build`, `build/moz.build`, `tools/profiler/moz.build`, `pingsender/moz.build`, `browser/app/macbuild/Contents/moz.build`, `browser/base/moz.build`, `browser/modules/moz.build`, `toolkit/components/backgroundtasks/moz.build` | moz.build, re-read at build start; no source recompile on their own |
| `nsAboutRedirector.cpp`, `BrowserChild.cpp`, `ContentChild.cpp`, `AvailableMemoryWatcher.cpp`, `CrashReporterClient.cpp`, `CrashReporterHost.cpp`, `CrashReporterInitArgs.ipdlh`, `nsExceptionHandler.h` | C++ and IPDL. `nsExceptionHandler.h` is included by 52 files, so it triggers a large incremental C++ rebuild; if the header edit is left out (keep `IsDummy` as is), only 6 files recompile. |
| `StaticPrefList.yaml`, `metrics_index.py` | generated C++ (StaticPrefs, Glean) |
| `components.conf` (`docshell`, `toolkit/xre`), `jar.mn`, `package-manifest.in`, `MacOS-files.in`, `application.ini.in`, `Makefile.in`, `config/rules.mk` | front-end or packaging (`./mach build faster` covers jar/components; `application.ini.in` regenerates) |
| JS, XHTML, CSS, FTL in `browser/` and `toolkit/` | front-end (`./mach build faster`) |

## Tests to delete or edit

Delete (all depend on the removed code and are skipped or unbuilt now):
- `toolkit/crashreporter/test/**` (xpcshell `unit`, `unit_ipc`, browser tests, gtest, `test_utils`, `testcrasher`), `toolkit/crashreporter/networking/tests`, `toolkit/crashreporter/crashping/tests`, `minidump-analyzer/tests`, the Rust tests in the crates.
- `toolkit/components/crashes/tests/**` (4 xpcshell tests, 1 browser test).
- `browser/base/content/test/browser-tabcrashed/` and the three `browser/base/moz.build` lines.
- `browser/modules/test/browser/browser_UnsubmittedCrashHandler.js`, `browser_CrashFileCleaner.js` and `browser.toml` entries.
- `dom/ipc/tests/browser_CrashService_crash.js` and its `browser.toml` entry.
- `toolkit/components/crashmonitor/test` STAYS (crash monitor stays).
- The two background-task test modules named above.

Edit (tests that stay and mention the removed pieces):
- `browser/base/content/test/browser-performance/browser_startup.js` (123-126, the `CrashSubmit.sys.mjs` expectation under `AppConstants.MOZ_CRASHREPORTER`) and `browser_startup_mainthreadio.js` (80-96, `condition: AppConstants.MOZ_CRASHREPORTER`): drop the conditional entries. Not required (undefined is falsy), but dropping keeps them honest.
- `browser/base/content/test/browser-siteIdentity/browser_check_identity_state.js` (103-105), `browser/components/preferences/tests/browser_bug1020245_openPreferences_to_paneContent.js` (85, 108), `browser/components/uitour/test/browser_openPreferences.js` (46), `browser/components/sessionstore/test/browser_pinned_tabs.js` (210), `browser/components/tabbrowser/test/browser/tabs/browser_restore_isAppTab.js` (67), `toolkit/components/extensions/test/xpcshell/{head.js,test_ext_background_early_shutdown.js,test_ext_storage_session_on_crash.js}`, `toolkit/mozapps/extensions/test/xpcshell/head_addons.js`: `AppConstants.MOZ_CRASHREPORTER` checks. Behaviour is unchanged when the constant is `undefined`; clean up only if time allows.
- asrouter targeting tests and `targeting-attributes.md` (see table).
- `browser/components/preferences` tests that expect the `automaticallySubmitCrashesBox` setting: search `automaticallySubmitCrashesBox` in `browser/components/preferences/tests` and `browser/components/backup/tests` before editing the setting. Not searched here beyond the config file; the preparer must grep these two directories.
- 12 `dom/l10n/tests/mochitest/{document_l10n,l10n_mutations}/*.html` files use `crashreporter/aboutcrashes.ftl`: NO edit (the FTL stays).
- Test conditions `crashreporter` / `!crashreporter` in about 24 `*.toml` manifests: no edit (mozinfo `crashreporter` stays `False`).

Suites the integrator should run after the build: `browser/base/content/test/tabs` plus `browser-tabcrashed` replacement smoke (open a tab, `BrowserTestUtils.crashFrame`, see about:tabcrashed with the Restore buttons and without the report form), `browser/components/sessionstore` (crash monitor and `restoreAsCrashed`), `browser/components/preferences` privacy tests, `browser/components/asrouter` targeting tests, `toolkit/content/tests/browser` crash tests, `toolkit/components/crashmonitor`, `toolkit/mozapps/extensions` xpcshell head, `dom/l10n` mochitests, `toolkit/modules/tests` Troubleshoot, `toolkit/components/telemetry` TelemetrySend, and `tools/m6e/run_tests.sh`. Also `./mach rusttests` is NOT run in this project (the gkrust test list changes).

## Must stay (with proof)

### The C++ crash annotation API and the dummy handler

Stays: `toolkit/crashreporter/nsExceptionHandler.h`, `nsDummyExceptionHandler.cpp`, `annotations/`, `CrashAnnotations.yaml`, `ipc/CrashReporterInitArgs.ipdlh`; `xpcom/system/nsICrashReporter.idl`; the `nsICrashReporter` implementation in `nsXULAppInfo` (`toolkit/xre/nsAppRunner.cpp`, 1212-1480, and `components.conf` `interfaces`); `ipc/glue/CrashReporter{Client,Host,Helper}.*` and the `InitCrashReporter` messages in `PContent`, `PGPU`, `PRDD`, `PVR`, `PUtilityProcess`, `PSocketProcess`, `PGMP`.

Why it cannot go: 52 files include `nsExceptionHandler.h`, 75 files and 444 lines call `CrashReporter::`, 4 include `CrashAnnotations.h`, plus the IPDL protocols and JS callers of `Services.appinfo.annotateCrashReport`. Removing it is a tree-wide edit that changes no behaviour (the stubs are empty), and the vendored/upstream-merge cost is permanent.

Proof it is unreachable as a reporter (nothing is recorded, written, submitted or sent):
1. `nsDummyExceptionHandler.cpp` is a closed set of about 100 empty functions: `SetExceptionHandler` returns `NS_ERROR_NOT_IMPLEMENTED`, `OOPInit` returns `NS_ERROR_NOT_AVAILABLE`, `GetEnabled()` is `false`, every `RecordAnnotation*` returns `NS_ERROR_FAILURE`, `TakeMinidumpForChild`, `FinalizeOrphanedMinidump`, `CreateMinidumpsAndPair`, `TakeMinidump`, `WriteExtraFile`, `GetServerURL`, `SetServerURL` return failure or `false`. No file, socket or thread is created in it (the only system call is `mach_thread_self()` in `CurrentThreadId`).
2. After this step `MOZ_CRASHREPORTER` cannot be turned on: the configure option, the config var and the define are gone. The `ExceptionHandler`, Breakpad, `crashhelper` and client sources are deleted, so no code path can start a handler even if a pref or environment variable asks for one. The environment variable `MOZ_CRASHREPORTER=1` (used by test harnesses) only sets `NS_XRE_ENABLE_CRASH_REPORTER` in `nsAppRunner.cpp:3555`, after which `OOPInit` and `SetExceptionHandler` fail as above and `NS_WARNING` is printed.
3. No submission path: `CrashSubmit`, `CrashReports`, `UnsubmittedCrashHandler`, the client app and `crashping` are deleted; the `crash-reports.mozilla.com` URL is no longer compiled into `application.ini` (the `[Crash Reporter]` block is removed) and `--with-crashreporter-url` is gone. The `StaticHPKPins.h` entry for `crash-reports.mozilla.com` (generated from `security/manager/tools/PreloadedHPKPins.json`) is a pin list, not a connection; leave it (security/ is vendored-style generated data).
4. No listening socket: the crash helper (`crash_helper_server`, Mach port server) is deleted; `GeckoArgs.h` still names `-crashHelperSend/-Recv/-Pid` but nothing parses them (`SetRemoteExceptionHandler` and `RegisterChildIPCChannel` are stubs returning `false`).
5. No entry from web content: `InitCrashReporter` IPDL messages carry only a thread port; the receivers call `CrashReporterHost`, whose `TakeMinidumpForChild` stub fails, so `GenerateCrashReport()` returns `false` before `NotifyCrashService` runs. `nsICrashReporter` is implemented by the app-info object (`Services.appinfo`), which web content cannot reach (no content-accessible API; JS callers are chrome code), and `annotateCrashReport` ends in the failing stubs.
6. Behavioural evidence: this is exactly the shipped state since P1.11 and the Phase 4 and Phase 5 acceptance and regression runs were done on it. After the step it is the same binary behaviour.

Verification the integrator can add: `nm`/`otool` on `libxul.dylib` shows no `google_breakpad` symbols; `lsof -i` and `lsof -U` on a running profile show no crash helper listener; `tools/m6e/acceptance.py` check "Removed features are absent" passes with `AppConstants.MOZ_CRASHREPORTER` missing.

### `nsICrashService.idl`

Stays in `toolkit/components/crashes` (or `xpcom/system`). Consumers: `ipc/glue/CrashReporterHost.cpp` (`NotifyCrashService`), `ipc/glue/CrashReporterHelper.h`, `dom/ipc/ContentParent.cpp:4290` (`CRASH_TYPE_CRASH`). Proof it is inert: no implementation is registered (`components.conf` is deleted; before this step it was behind the flag), so `do_GetService("@mozilla.org/crashservice;1")` returns null and `NotifyCrashService` returns at its first check. Alternative that removes the IDL: delete `NotifyCrashService`, `RecordCrash`, `RecordCrashWithTelemetry`, `HandleOrphanedMinidump` callers and the constants in those three C++ files. Not recommended in this step (more C++ churn, touches `Glean subprocess.crashes_with_dump` accounting).

### `toolkit/locales/en-US/crashreporter/aboutcrashes.ftl` and the `toolkit/locales/jar.mn` entry

Used as a plain fixture by 12 `dom/l10n` mochitests (messages `crash-reports-title`, `id-heading`, `date-crashed-heading`). Not reachable from product UI once `crashes.html` is deleted. If the preparer prefers a clean removal, re-point the 12 tests to another toolkit FTL with three messages first.

### `toolkit/components/crashmonitor`

Not the crash reporter. It writes `sessionCheckpoints.json` and is read by `browser/components/sessionstore/SessionStartup.sys.mjs`; it has no use of `nsICrashReporter`. No network, no UI. Deleting it would change session restore after a crash.

### Test and automation infrastructure that mentions the crash reporter

`testing/mozbase/mozcrash`, `testing/` runners and profiles, `mozinfo.py`, `taskcluster/` and `build/sparse-profiles` rows, `python/mozbuild` symbol scripts: not part of the browser, no runtime effect.

### Vendored code (D21)

`third_party/rust/*` crates orphaned by the lockfile prune, `build/clang-plugin/Utils.h` (names `google_breakpad` to skip checks), `build/gyp_includes/common.gypi`: vendored or tooling, left alone.

## Risks and open questions

1. **Cargo.lock pruning is unverified.** I did not run cargo (shared objdir is busy, and the vendored source replacement needs the objdir config). The crate list above comes from the `Cargo.lock` package names; cargo will tell the exact set. If `cargo metadata --offline --locked` fails after the edit, regenerate the lock with `cargo update --workspace --offline` using the vendored config. `rust_minidump_writer_linux` was restored in P4.6a precisely because cargo resolves the whole workspace for every target; its removal needs the `toolkit/library/rust/shared/Cargo.toml` and `lib.rs` edits in the same commit, and Windows-only `win32_process_mitigations` likewise.
2. **`LOCAL_INCLUDES` of a missing directory is a mozbuild error.** The two entries pointing into `google-breakpad` (`tools/profiler/moz.build`, `pingsender/moz.build`) must go in the same commit. The `/toolkit/crashreporter` includes in `ipc/glue`, `dom/ipc`, `gfx/ipc` and `toolkit/xre` stay valid.
3. **`toolkit/moz.build` `TEST_HARNESS_FILES` for `crashreport.sjs`** points at a deleted file and also breaks moz.build evaluation. Same commit.
4. **`browser/base/moz.build` references three `browser-tabcrashed` manifests.** Same commit as the directory delete.
5. **`CONFIG["MOZ_CRASHREPORTER"]` for a removed config var.** Safe: today's configure already omits the key and `toolkit/crashreporter/moz.build` reads it. For `CONFIG[var]` loops in `build/moz.build` the variable names must be removed from the tuple, not left behind.
6. **`mozinfo` key.** Keep it. Dozens of `run-if`/`skip-if` conditions evaluate it. Evaluating a removed key would not be safe for every manifest parser.
7. **Hint mismatch.** The task hint listed "crash monitor" as part of the feature. `toolkit/components/crashmonitor` is the session-restore checkpoint and must stay. If the lead really wants it removed, session-restore behaviour changes and `SessionStartup.sys.mjs` plus session-store tests must be edited; I recommend not doing this.
8. **Tier 2, optional and not recommended here.** Shrinking the C++ API: delete `NS_XRE_ENABLE_CRASH_REPORTER`, `crashReporterURL` (`CreateAppData.cpp`, `StaticXREAppData.h`, `XREAppData.*`, `appini_header.py`), the nsAppRunner crash-handler setup (3555-3640, 4325, 4903, 4465), the nsXULAppInfo `nsICrashReporter` methods, and every `CrashReporter::` call. It is a large, binary-neutral change in code that upstream edits constantly. If wanted later, do it as its own step.
9. **User-visible text.** `crashed-title = Tab crash reporter` and `crashed-subframe-title` mention reporting. Edit with an `M6E:` marker; this is not a Mozilla-name replacement.
10. **Pref-removal wide rebuild.** `StaticPrefList.yaml` and `metrics_index.py` regenerate headers included almost everywhere. Batch with the other features, or keep the five dead prefs and drop only the JS-visible ones.
11. **dom/l10n fixture.** Deleting `aboutcrashes.ftl` would break 12 mochitests; the plan keeps the file. Open question for the lead: re-point the tests (cleaner) or keep (cheaper).
12. **Search coverage.** I searched the whole tree for `MOZ_CRASHREPORTER`, `nsExceptionHandler`, `CrashReporter*`, `CrashAnnotations`, `minidump`, `crashmonitor`, `google-breakpad`, `CrashSubmit`/`CrashManager`/`CrashService`, `about:crashes`, the Cargo names and the pref prefixes, but not `devtools/` or `remote/` beyond `annotateCrashReport` and `nsICrashReporter` (only `remote/components/Marionette.sys.mjs:170` and `RemoteAgent.sys.mjs:470` call `annotateCrashReport`; both are guarded and stay). I did not grep `browser/components/preferences/tests` or `browser/components/backup/tests` for `automaticallySubmitCrashesBox`; the preparer must.
13. **Objdir unavailable.** `obj-m6e` is being rebuilt, so I could not check the built app for leftover crash reporter binaries; the evidence is `mozconfig`, `config.status` and the plan's P5 notes.

## Critic addendum

Independent review. Searches used basenames, directory names, config var names, pref prefixes, FTL ids, Glean categories, contract IDs, Cargo package names and doc cross-references, over `git grep -n -I` with pathspecs (whole-tree greps time out while the baseline build runs). The Cargo.lock result below was computed from `Cargo.lock` with a script (reachability from the workspace's path packages), not by running cargo. Overall the report is accurate on the C++/IPC side and on the stub-API "must stay" decision. It misses the items below; none changes the recommended approach.

### Corrections to wrong or incomplete claims

1. **Cargo.lock prune list is incomplete.** The report names 12 local crates plus "minidump*, breakpad-symbols, crash-context, possibly hyper/rkv/tokio". Reachability from the remaining workspace members shows exactly 40 packages become unreachable. The report omitted these 21: `cachemap2`, `circular`, `cobs`, `debugid 0.8.0`, `embed-manifest`, `error-graph`, `failspot`, `framehop`, `gimli 0.32.3`, `linked-hash-map 0.5.6`, `macho-unwind-info`, `memoffset 0.9.0`, `memtest` (git), `nix 0.30.1`, `pe-unwind-info`, `phf_codegen 0.14.0`, `postcard`, `process-backend` (git), `process-reader` (git, hyphenated, distinct from the local `process_reader`), `range-map`, `smart-default`, `sys-locale`. The full 40 are: the 12 local crates (`crash_helper_client`, `crash_helper_common`, `crash_helper_server`, `crashping`, `crashreporter`, `minidump-analyzer`, `mozannotation_client`, `mozannotation_server`, `mozwer_s`, `process_reader`, `rust_minidump_writer_linux`, `win32_process_mitigations`) plus `breakpad-symbols`, `crash-context`, `minidump`, `minidump-common`, `minidump-unwind`, `minidump-writer` and the 21 above. `hyper`, `rkv` and `tokio` stay (other crates still use them). Every removed name is single-version in the lock, so no remaining entry gets re-qualified. The only remaining package whose dependency list names a removed package is `gkrust-shared` (`crash_helper_client`, `mozannotation_client`, `mozannotation_server`, `rust_minidump_writer_linux`, `win32_process_mitigations`). The report's gkrust-shared line is right; the "possibly hyper/rkv/tokio features" remark is a non-issue for the lock file.
2. **`.cargo/config.toml.in` has two git source sections to drop, not one.** Besides `minidump-writer` (rev 3ab1604f...), the `[source."git+https://github.com/mozilla/memtest?rev=ad681ba4..."]` section (lines ~93-96) is only used by `toolkit/crashreporter/client/app` (`memtest = { git = ... }`). The report only lists `minidump-writer`. `process-backend` and `process-reader` use the same minidump-writer git source, so that one section covers three lock entries.
3. **`toolkit/library/rust/moz.build` has a third site.** Lines 63-67 (`# Code coverage builds time out when executing the crashping tests...` plus `if not CONFIG["MOZ_CODE_COVERAGE"]: RUST_TESTS += ["crashping"]`). The report lists 24-25, 39-40, 43-44 only. A `RUST_TESTS` entry for a deleted crate fails moz.build/cargo.
4. **`toolkit/components/crashes/moz.build` still has `SPHINX_TREES["crash-manager"] = "docs"` and `with Files("docs/**")`.** Both go with the `docs/` deletion (report only says "XPIDL_MODULE, XPIDL_SOURCES, DEFINES removed"). Same for `toolkit/crashreporter/moz.build` (the report does cover its `SPHINX_TREES`).
5. **Test `browser/components/preferences/tests/browser_bug1020245_openPreferences_to_paneContent.js`** (the report left this as "grep before editing"): two `add_task` blocks at about lines 82-125 assert `#automaticallySubmitCrashesBox`. Both start with `if (!AppConstants.MOZ_CRASHREPORTER) return;`, so they are already no-ops. Delete both tasks when the setting goes. `browser/components/backup/tests` has no `automaticallySubmitCrashesBox` reference.
6. **Scope of `browser-tabcrashed/` deletion loses non-report coverage.** `browser_aboutRestartRequired*.js`, `browser_shown.js`, `browser_withoutDump.js`, `browser_multipleCrashedTabs.js` and `browser_printpreview_crash.js` test about:tabcrashed / about:restartrequired behaviour, not only report sending. They are gated by `run-if = crashreporter`, so they do not run today. Deleting the directory is correct for this step but the report should say that tab-crash page coverage is lost; the "replacement smoke" in the suites list is manual.
7. **Plan text to update (not code):** `M6E_IMPLEMENTATION_PLAN.md` ("Not built: the updater service and the crash reporter", "Crash-reporter crates, packaging scripts, fuzzing targets and PHC stay") and `m6e_browser_agent_instructions.md` line 50 / D30. Update after the step.

### References the report missed (file, reference, action)

Build, config, mozconfigs
- `browser/config/mozconfigs/linux64/debug-fuzzing`, `debug-fuzzing-noopt`, `fuzzing-ccov` (line 17-22), `browser/config/mozconfigs/macosx64/nightly-asan` (17), `build/unix/mozconfig.asan` (11), `build/unix/mozconfig.tsan` (8), `build/win64/mozconfig.asan` (13): `ac_add_options --disable-crashreporter`. Remove the line (an unknown option is a configure error for anyone using these files) and the adjacent comments in the three linux64 files. None is used by M6E's `mozconfig`, so it is not build-critical.
- `tools/sanitizer/docs/{asan,memory_sanitizer,tsan}.md` (lines 135, 87, 122): same option in doc snippets. Remove the line.
- `pyproject.toml:122`: ruff exclude `tools/crashreporter/*.configure` (no such file exists). Remove.
- `build/docs/jar-manifests.md:96,102`, `build/docs/locales.md:255`, `build/docs/mozinfo.md:68`: docs mention `crashreporter` jar entry and the mozinfo key. The jar-manifests example is an illustration of `toolkit/locales/jar.mn`, which stays; leave. `mozinfo.md` stays (key stays).

Top-level `tools/crashreporter/` (not `toolkit/crashreporter`)
- `tools/crashreporter/system-symbols/mac/` (7 files: `PackageSymbolDumper.py`, `get_update_packages.py`, `list-packages.py`, `run.sh`, `scrapesymbols/*`, `yaa_extractor.py`). Mac system-symbol scraping for Mozilla's symbol server; only used by taskcluster (`system-symbols-upload`). Delete with the feature (the report never mentions this directory). Also remove `mots.yaml:1705` (`tools/crashreporter/**/*`), `build/sparse-profiles/docker-image:17`, `build/sparse-profiles/taskgraph:132`.

Docs (sphinx will warn or fail on dangling links and toctrees)
- `toolkit/docs/index.md` lines 13 and 15: toctree entries `components/crashes/crash-manager/index` and `crashreporter/crashreporter/index`. Remove both when the two `docs/` dirs go.
- `docs/crash-reporting/index.md` lines 43-45: links to `/toolkit/crashreporter/crashreporter/index.md`, `/toolkit/components/crashes/crash-manager/index.md`, `.../crash-ping-lifecycle.md`. Remove the three bullets (the page itself is about crash-stats triage and can stay or go; leave it).
- `docs/contributing/debugging/{capturing_minidump,debugging_a_minidump,debugging_on_windows,process_dump_task_manager,stacktrace_windbg}.md` and `taskcluster/docs/using-the-mozilla-source-server.md`: links to `/toolkit/crashreporter/crashreporter/index` and `.../Using_the_Mozilla_symbol_server` (deleted). Re-point or drop the links.
- `ipc/docs/processes.md:135` (link to the crashreporter docs) and `:256` (mentions `toolkit/components/crashes/metrics.yaml`): edit.
- `toolkit/components/telemetry/docs/concepts/crashes.md:8,19`: `{doc}` links to `crashes/crash-manager/crash-ping-lifecycle` (deleted). Remove the links.
- `browser/components/asrouter/docs/targeting-attributes.md` 1650-1680 (report has it) links to `CrashManager.in.sys.mjs`.
- `docs/contributing/debugging/stacktrace_report.md:58,75`, `netwerk/docs/submitting_networking_bugs.md:87`: tell the reader to open about:crashes. Optional doc edit.
- `docs/code-quality/lint/linters/dot-mozilla-reference.md`: names `crash_helper_server` once; optional.

Tooling and generated type data
- `tools/@types/generated/tspaths.json` (lines ~5792, 6401, 7568, 7913) and `tools/@types/generated/lib.gecko.modules.d.ts` (lines 662-663, 927, 929, 1487): entries for `CrashService`, `RemoteSettingsCrashPull`, `CrashManagerTest`, `CrashManager`, `CrashReports`, `CrashSubmit`. Remove these entries (generated by `./mach` ts tooling; hand-edit or regenerate). `lib.gecko.xpcom.d.ts` keeps `nsICrashService`/`nsICrashReporter` (IDLs stay). The report only lists `tools/@types/subs/AppConstants.sys.d.mts` and `fixed_paths.js`.
- `tools/lint/eslint/eslint-plugin-mozilla/lib/rules/valid-services.mjs:31`, `valid-services-property.mjs:62` (`"crashmanager"` in the allowed `Services.*` lists) and `tools/lint/eslint/eslint-plugin-mozilla/tests/valid-services.mjs:22` (valid-case `Services.crashmanager`): the `Services.crashmanager` property is defined only by the deleted `components.conf` (`js_name: crashmanager`). Remove the entries and the test case. Remaining JS users: `testing/mochitest/BrowserTestUtils/BrowserTestUtils.sys.mjs:2402` and `testing/specialpowers/content/SpecialPowersParent.sys.mjs:1036`, both reached only with a `dumpID` or crash ID, which cannot exist; leave.
- `tools/lint/fluent-lint/exclusions.yml:14`: `crashed-include-URL-2` ID01 exclusion, tied to `browser/aboutTabCrashed.ftl`. Remove with the message.
- `tools/lint/clippy.yml` 91-102: 12 entries incl. `toolkit/crashreporter/client/app/` (report says 91-102 but lists only the range; ok). `tools/lint/codespell.yml:47`, `dot-mozilla-reference.yml:46`, `glean-parser.yml:10`: report covers.
- `memory/build/PHC.cpp:91`: comment mentioning `./mach test toolkit/crashreporter/test/unit`. Comment in vendored-style code; leave.
- `xpcom/geckoprocesstypes_generator/geckoprocesstypes/__init__.py`: `crash_ping` per-process field, consumed only by `toolkit/components/crashes/gen_CrashManager.py` (deleted). Leave (tooling data, changing it regenerates process-type headers).

Config in staying C++/JS
- `browser/modules/moz.build:26-27` (`Files("test/browser/browser_UnsubmittedCrashHandler.js")`; report has this) and `browser/base/content/moz.build:95-96` (`Files("test/browser-tabcrashed/**")`): the second is missing from the report. Remove.
- `toolkit/content/moz.build:80-81`: `Files("tests/browser/browser_content_url_annotation.js")` with BUG_COMPONENT Crash Reporting. That test (`toolkit/content/tests/browser/browser_content_url_annotation.js`, `run-if = crashreporter`, `browser.toml` line 91-94) checks the URL annotation in `BrowserChild.cpp`/`ContentChild.cpp` that the report removes. Delete the test, its `browser.toml` entry and the `Files()` block.
- `toolkit/modules/tests/browser/browser_Troubleshoot.js:217-245`: schema entry `crashes` (`required: false`). Remove it with the Troubleshoot provider.
- `toolkit/components/messaging-system/lib/SpecialMessageActions.sys.mjs:416`: `"browser.crashReports.unsubmittedCheck.autoSubmit2"` in the `SET_PREF` allowlist. Remove (the report lists the OnboardingMessageProvider and ASRouterTriggerListeners uses of the same pref but not this one). Check its test `toolkit/components/messaging-system/test` for the pref name.
- `browser/locales/en-US/browser/browser.ftl:1284-1285` `plugin-crashed-notification-submit-button` ("Submit a crash report"), used only by the `PluginParent.sys.mjs` block the report removes. Remove the message.
- `browser/components/sessionstore/test/browser_background_tab_crash.js:167`: sets `browser.crashReports.unsubmittedCheck.autoSubmit2 = true` and relies on the backlogged-report branch of `TabCrashHandler` (`willShowCrashedTab`). That branch is removed; the test `test_background_crash_autosubmit_backlogged` then tests nothing and should be removed or rewritten. (Whole file is `run-if crashreporter`-gated by its manifest; check.)
- `browser/components/aboutwelcome/tests/browser/browser_aboutwelcome_multiselect.js:141-146` and asrouter `ContentTiles.test.jsx:1606`, `MultiSelect.test.jsx:397`: use a literal `crash-data` multiselect fixture, independent of `OnboardingMessageProvider`. No action; they still pass after the provider object is removed.
- `toolkit/xre/test/test_launch_without_hang.js:221-224` and `testing/marionette/harness/.../test_crash.py:37`: set `MOZ_CRASHREPORTER*` env vars or read `AppConstants.MOZ_CRASHREPORTER` (evaluates to undefined, falsy). No action.
- `browser/app/nsBrowserApp.cpp:261-265`: `CrashReporter::RegisterRuntimeExceptionModule()` and `mozglue/misc/RuntimeExceptionModule.cpp` (reads `MOZ_CRASHREPORTER` env). Windows-oriented stubs in the dummy API; stays under the "C++ API stays" decision. Listed so nobody searches for it.
- `testing/gtest/mozilla/GTestRunner.cpp:194-219` and `testing/gtest/mozilla/MozHelpers.cpp:9-19`: look up `@mozilla.org/toolkit/crash-reporter;1`, which is not registered; null-checked. Stay.
- `testing/tools/minidumpwriter/` (`minidumpwriter.cpp`, `moz.build`; DIRS entry `"/testing/tools/minidumpwriter"` in `toolkit/toolkit.mozbuild:175` under `ENABLE_TESTS`; bug component "Crash Reporting" in `testing/tools/moz.build:12`; named in `python/mozbuild/mozbuild/action/test_archive.py:46` and `artifacts.py:985`). It is a Windows-only (`windows.h`, `dbghelp.h`) helper for `mozcrash`; the moz.build builds nothing on macOS. Not in the report. Optional: delete it with the `toolkit.mozbuild` DIRS line and the `Files("minidumpwriter/**")` block; harmless if left.
- `build/pgo/profileserver.py:37-52,180-181`, `build/valgrind/mach_commands.py:105`, `build/valgrind/cross-architecture.sup:39-56`, `python/mozboot/mozboot/bootstrap.py:265` (`minidump-stackwalk` toolchain), `python/mozbuild/mozbuild/{artifacts.py,action/dumpsymbols.py,action/unify_symbols.py,mach_commands.py:2590-2824}` (`mach run --enable-crash-reporter` sets `MOZ_CRASHREPORTER=1`): symbol and env tooling. Same status as the report's "out of scope" symbol pipeline row; leave, flag for P6.2.
- `toolkit/mozapps/installer/{package-name.mk:33-34, packager.mk:60-70, upload-files.mk:84-93}`, `build/moz-automation.mk` (`buildsymbols`, `uploadsymbols` tiers): same symbol-pipeline group. `Makefile.in` edits that remove the `uploadsymbols` body still leave the `uploadsymbols` target referenced by `moz-automation.mk:48,73`; keep an empty `uploadsymbols:` target or leave `Makefile.in` alone for this step.

### Merge hazards against earlier features (order 1-7)

These hunks sit next to lines that the updater (7), sync (5) and others edit, so expect textual conflicts even though the logical edits are independent:
- `build/moz.build` line 81 tuple `("MOZ_CRASHREPORTER", "MOZ_PROFILE_MIGRATOR", "MOZ_UPDATER")`; the updater branch will already have removed `"MOZ_UPDATER"`. Same for `build/application.ini.in` (the `#if MOZ_CRASHREPORTER` block sits directly above `#if MOZ_UPDATER`), `browser/installer/package-manifest.in` (Crash Reporter block follows the maintenance-service block), `browser/installer/allowed-dupes.mn`, `browser/app/macbuild/Contents/MacOS-files.in` (the `updater` bits) and `Makefile.in`.
- `Cargo.toml` members list (the VPN, ML and updater crates are removed from the same list), `Cargo.lock`, `build/workspace-hack/Cargo.toml` feature block, `toolkit/library/rust/shared/{Cargo.toml,lib.rs}`.
- `toolkit/moz.configure` `data_reporting()` (3532-3538): MOZ_NORMANDY is order 9 and stays here; only `crashreporter` goes.
- `browser/app/profile/m6e.js` and `firefox.js` pref blocks.

### Verified claims

- `ExtraFileParser.*` / `nsExceptionHandlerUtils.*` are only used by `nsExceptionHandler.cpp` (git grep over toolkit, xpcom, ipc, dom, gfx, browser; `SimpleNoCLibDtoA` has no outside user). Correct.
- `toolkit/components/crashmonitor` is separate from the crash reporter (no reference to `nsICrashReporter`; consumer `SessionStartup.sys.mjs`). Correct, and the report's flag in "Risks" is right.
- `build/appini_header.py` tolerates a missing `[Crash Reporter]` section (the `getint` is in try/except and `serverurl` defaults to ""), so removing the `application.ini.in` block is safe. `CreateAppData.cpp` reads the keys with default-on-missing.
- `toolkit/xre/components.conf`, `docshell/build/components.conf`, `nsAboutRedirector.cpp` hunks are exactly as described.
- `tools/m6e/acceptance.py` `REMOVED_CONSTANTS` only records `AppConstants[name]`; undefined is reported as removed. `tools/m6e` has no other reference to the removed prefs (checked `crashReports`, `unsubmittedCheck`, `crashreporter`; only `merge/nightly-prefs-reviewed.txt:54` as the report says).
- Enterprise policies (`browser/components/enterprisepolicies`), `remote/`, `devtools/`, `services/settings` and `security/manager` have no live dependency (only the two guarded `annotateCrashReport` calls in `remote/components/{Marionette,RemoteAgent}.sys.mjs`, comments, and the `crash-reports.mozilla.com` HPKP pins in generated `StaticHPKPins.h`). The Remote Settings collection `crash-reports-ondemand` is not in any allowlist (nothing to remove); it is read only by the deleted `RemoteSettingsCrashPull.sys.mjs`.
- Glean: after `toolkit/components/crashes/metrics.yaml` is removed, no remaining file outside `ContentCrashHandlers.sys.mjs` and the deleted tests uses `Glean.crash*` or `Glean.crashSubmission` (git grep across the tree excluding those paths returned nothing). There is no `crashping` ping registration in `metrics_index.py` (the crate ships its own yaml).
- Remaining `AppConstants.MOZ_CRASHREPORTER` readers in staying non-test JS are exactly: `PluginParent`, `browser.js`, `BrowserGlue`, `permissions-data.mjs`, `ContentCrashHandlers`, `aboutSupport.js`, `Troubleshoot`, `XPIProvider`, `ExperimentAPI` (later feature). The report covers all nine.
- Remaining test readers beyond the report's list: `browser/modules/test/browser/browser_UnsubmittedCrashHandler.js` (deleted), `toolkit/components/ml/tests/browser/browser_ml_native.js` (earlier feature), `toolkit/xre/test/test_launch_without_hang.js`, marionette `test_crash.py`, `testing/xpcshell` and mochitest runners: all tolerate `undefined`.

### Changes to the preparer checklist

Add to the deletion list: `tools/crashreporter/` (7 files), `toolkit/content/tests/browser/browser_content_url_annotation.js`, optionally `testing/tools/minidumpwriter/`.
Add to the edit list: the `.cargo/config.toml.in` memtest section, `toolkit/library/rust/moz.build` lines 63-67, `SpecialMessageActions.sys.mjs`, `browser.ftl` plugin submit message, `toolkit/docs/index.md`, `docs/crash-reporting/index.md`, the eslint `valid-services*` rules and test, `tools/@types/generated/{tspaths.json,lib.gecko.modules.d.ts}`, `fluent-lint/exclusions.yml`, `browser/base/content/moz.build` and `toolkit/content/moz.build` `Files()` blocks, `browser_Troubleshoot.js` schema, `browser_bug1020245_openPreferences_to_paneContent.js` tasks, `browser_background_tab_crash.js`, the seven `--disable-crashreporter` mozconfigs and three sanitizer docs.
Cargo.lock: use the 40-package list above as the expected result of `cargo update --workspace --offline`.
