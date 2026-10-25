# P6.1g updater: preparation notes

Branch: `m6e-p6/updater` (3 commits on top of 4110e90366, which differs from the analysis base cb002d4874 only by plan text).
No build, test or ./mach command was run. Static checks done: `node --check` on every edited .js/.mjs (preprocessed files firefox.js, backgroundtasks.js and AppConstants.sys.mjs excepted), JSON parse, `ruby YAML` parse (mots.yaml is YAML 1.2 and not parseable by the local ruby, same at HEAD), Python compile of edited .py and AST parse of every edited moz.build/.configure, and `git grep` for every deleted basename, directory, contract ID, FTL id, chrome:// URL and AppConstants key.

## Commits

1. `Delete the updater, MAR tooling and the C++ and build entries that fed them`
2. `Remove the update UI, policies, prefs, strings and telemetry`
3. `Remove the updater tests and the update parts of tests for staying code`

## Deleted

- `toolkit/mozapps/update/` (whole directory, including updater/, common/, content/, docs/, tests/, the five Glean yaml files), except `common/commonupdatedir.{cpp,h}` which were moved to `toolkit/xre/`.
- `toolkit/xre/`: `nsUpdateDriver.*`, `nsUpdateMutex.*`, `nsUpdateSyncManager.*`, `updaterfileutils_osx.*`.
- `modules/libmar/`, `tools/update-programs/`, `tools/update-packaging/`, `testing/update/`.
- `toolkit/components/taskscheduler/` except `rust/` (Windows-only crate, kept, see notDone).
- `toolkit/themes/osx/mozapps/update/`, `toolkit/locales/en-US/toolkit/updates/`, `toolkit/locales/en-US/chrome/mozapps/update/updates.properties`.
- `browser/components/preferences/widgets/update-state/` and `update-information/` (the second one is not in the analysis list; it only served the About pane), `config/about-firefox.mjs` (whole About pane).
- `browser/base/content/aboutDialog-appUpdater.js`, `browser/modules/UpdatePolicyEnforcer.sys.mjs`, `BackgroundTask_install.sys.mjs`, `BackgroundTask_uninstall.sys.mjs`, `browser/themes/shared/update-badge.svg`.
- `browser/locales/generate_ini.py`, `browser/locales/en-US/updater/updater.ini`, `build/update-settings.ini`, `toolkit/content/licenses/bspatch.txt`, `python/mozbuild/mozbuild/repackaging/mar.py` and the `mach repackage mar` command, `browser/components/urlbar/docs/assets/nontechnical-overview/intervention-update.png`.
- Tests: whats_new_page dir, `browser_bug538331.js`, five enterprise-policy browser tests and `disable_app_update/`, two policy xpcshell tests, four urlbar update tip tests, `suppress-tips/`, `browser-tips/README.txt`, seven `browser_panelUINotifications*.js` tests, `browser_advanced_update.js`, `browser_updates_managed_by_os.js`, three `UpdatePolicy*` tests, `browser_UpdatePingSuccess.js`, background-task tests for automaticrestart, shouldprocessupdates, shouldnotprocessupdates and update_sync_manager, devtools `browser_browser_toolbox_shouldprocessupdates.js`.

## Edited (staying code)

- Build: `toolkit/moz.build`, `toolkit/toolkit.mozbuild`, `toolkit/components/moz.build`, `config/external/moz.build`, `toolkit/xre/moz.build`, `toolkit/components/build/components.conf`, `toolkit/modules/moz.build`, `build/moz.build`, `build/application.ini.in`, `browser/app/moz.build`, `browser/moz.build`, `browser/moz.configure`, `browser/locales/moz.build`, `browser/components/moz.build`, `browser/components/preferences/moz.build`, `moz.configure`, `mfbt/moz.build`, `tools/moz.build`, `mozconfig`, `build/moz.configure/update-programs.configure`, `browser/installer/package-manifest.in`, `allowed-dupes.mn`, `build/sparse-profiles/taskgraph`, python (`mozinfo.py`, `artifacts.py`, `command_util.py`, `l10n_repackage.py`, `mach_commands.py`), lint and ignore configs, `mots.yaml`, docs.
- C++/ObjC: `nsAppRunner.cpp` (ShouldNotProcessUpdates, ProcessUpdates and test-process-updates blocks, includes), `nsCommandLineServiceMac.mm` (only the M6E `/bin/sh` relaunch path remains), `MacLaunchHelper.{h,mm}`, `MacRunFromDmgUtils.mm`, `CreateAppData.cpp` (`[AppUpdate]` read), `nsXREDirProvider.cpp`, `BackgroundTasks.{cpp,h}` (`IsUpdatingTaskName`), `LocaleService.cpp`, `nsContentSecurityUtils.cpp`, `nsToolkitProfileService.cpp` (comment), `TestMacChildProcessPaths.cpp`, `ThreadAllows.txt`.
- New file `toolkit/xre/updatedefines.h`: a trimmed macOS-only version with `NS_T`, `NS_tsnprintf`, `MAXPATHLEN` and the includes `MultiInstanceLock.cpp` needs.
- Front end: About dialog (xhtml, js, css), app menu (viewcache, panelUI.inc.xhtml, panelUI.js banner machinery, themes CSS), Settings (`preferences.js`, `main.js`, `main.inc.xhtml`, `preferences.xhtml`, `jar.mn`, `LegacyPaneMappings.mjs`, `privacy.mjs`, `privacy.js`, `security-privacy-card.mjs`), `BrowserGlue`, `BrowserContentHandler`, `ProfileDataUpgrader`, `ObserverForwarder`, `BrowserComponents.manifest`, urlbar (`UrlbarProviderInterventions` loses the update tips and the "update" document, `UrlbarShared`, `UrlbarUtils`, `QuickActionsLoaderDefault`, `interventions.ftl`, docs, `metrics.yaml`), ASRouter (`needsUpdate` removed), enterprise policies (8 handlers, schema, allowed-prefix and deny lists, aboutPolicies, FTL), `FirstStartup` (`deleteTasksTime`), `Troubleshoot` (`updateChannel`), `aboutSupport`, `TelemetryEnvironment` and docs, `UpdateUtils.sys.mjs` (down to channel, ABI, OS version, locale and URL formatting), `AppConstants.sys.mjs` (3 keys), FTL files, `browser.properties`.
- Prefs: update prefs removed from `firefox.js` (timer prefs and `app.update.channel` kept), `backgroundtasks.js`, the Nimbus WNP prefs (`startup.homepage_override_*nimbus*`), `toolkit.telemetry.updatePing.enabled` lock in `m6e.js` and `TelemetryUtils`.
- Glean/Nimbus: `metrics_index.py`, `browser/components/metrics.yaml` (`background_update`), `toolkit/modules/metrics.yaml` (`delete_tasks_time`), `nimbus/metrics.yaml` (six `background-update` ping lines), `FeatureManifest.yaml` (`whatsNewPage`, `backgroundUpdate`, `backgroundUpdateAutomaticRestart`, `backgroundUpdateCheckPolicy`).
- Tests: edited for the removed pieces (see commit 3). Notable rewrites: `BackgroundTask_policies` and its xpcshell test now use `DisplayMenuBar` instead of `AppUpdateURL`; `test_bug292789.html` uses `fontbuilder.js` instead of `update/history.js`; `waitForAboutDialog` moved into `browser-about/head.js`; `browser-tips/head.js` lost the update helpers; `test_UpdateUtils_url.js` and `browser_remoteTroubleshoot.js` lost their `updater` run-if.
- `tools/@types`: `tspaths.json` (15 entries), `AppConstants.sys.d.mts`, `FeatureManifest.sys.d.mts` edited by hand.

## Deviations from the analysis

- `build/moz.configure/zucchini.configure` is kept: `tools/zucchini/moz.configure` still includes it (the analysis said it was only included from the updater block).
- `BrowserContentHandler`: the new-milestone case keeps `startup.homepage_override_url`, `%OLD_VERSION%` substitution and `LaterRun.enable`, as the analysis said. `LaterRun` stays disabled by default (`browser.laterrun.enabled` false), so there is no visible change. Note this differs from today's runtime behaviour, where the missing `UpdateManager` service threw and skipped `LaterRun.enable`.
- The whole About pane was removed (critic option 1). `support` group (`supportGetHelp`, `supportShareIdeas`) went with it; nothing else used it.
- Panel notification tests: all seven deleted instead of rewritten on `addon-installed`.
- `browser_aboutDialog_referrals.js` was already stale for M6E (looks for `contributeDesc`); only its head import was fixed.
- `policy-messages.ftl` is now an empty file with the licence header (both of its messages belonged to `UpdatePolicyEnforcer`); the file is still referenced by ASRouter code, so it was not removed.
- `ScheduledTask.sys.mjs` still names the `app.update.compulsory_restart_log` log pref (console log level only), left alone.

## integrationTodos

- Configure change, full rebuild: `mozconfig` line removed and the options in `update-programs.configure` removed together. Run `./mach configure` then `./mach build` (C++ in `toolkit/xre`, `toolkit/components/backgroundtasks`, `intl/locale`, `dom/security`, plus Glean-generated bindings for the deleted metrics). `./mach build faster` is not enough.
- No Cargo.toml or Cargo.lock change was needed (`toolkit/components/taskscheduler/rust` stays as a workspace member).
- Regenerate `tools/@types/generated/lib.gecko.glean.d.ts`, `lib.gecko.xpcom.d.ts` and `lib.gecko.win32.d.ts` with the types tool; they still list the `update`, `updater`, `updateSettings`, `backgroundUpdate` Glean groups and the `nsIUpdate*` interfaces.
- Verify at integration: `./mach lint` for `eslint-rollouts.config.mjs`, `.prettierignore`, `tools/lint/*.yml`; `glean_parser` over the index (no unknown ping `background-update` should remain); `./mach python-test python/mozbuild/mozbuild/test` (mozinfo, l10n manifest).
- Tests to run: xpcshell `toolkit/components/backgroundtasks/tests/xpcshell` (notably `test_backgroundtask_policies.js`, which was rewritten), `toolkit/modules/tests/xpcshell` (`test_UpdateUtils_url.js` now runs because the `updater` condition was dropped; if it fails, delete it), `toolkit/components/telemetry/tests/unit` (TelemetryEnvironment), `browser/components/enterprisepolicies/tests/xpcshell`, `browser/components/asrouter/tests`, `browser/modules/test/unit`; browser-chrome `browser/components/preferences/tests` (including `privacy/browser_privacy_status_card.js`), `browser/components/urlbar/tests/browser-tips`, `browser-quickactions`, `browser-engagementTelemetry`, `browser/base/content/test/browser-about`, `browser-general/browser_remoteTroubleshoot.js`, `browser-static` (sentence case, all files referenced), `browser/components/customizableui/test`, `browser/components/tests/browser`, `toolkit/modules/tests/browser/browser_Troubleshoot.js`; mochitest `caps/tests/mochitest/test_bug292789.html`; marionette `toolkit/xre/test/marionette/test_relaunch_dock_overlap.py`; gtest `ipc/glue/test/gtest/TestMacChildProcessPaths.cpp`; `tools/m6e/acceptance.py` (REMOVED_CONSTANTS reads `undefined` for `MOZ_UPDATER` and passes).
- Conflicts to expect at integration: the earlier features (VPN, translations, genai, ml, sync, fxaccounts) also touch `BrowserGlue.sys.mjs`, `preferences.js`, `main.js`, `preferences.ftl`, `panelUI.js`, `browser.js`, `UrlbarShared.mjs`, `metrics_index.py`, `Policies.sys.mjs` and `policies-schema.json`.

## notDone (kept on purpose)

- `toolkit/modules/UpdateUtils.sys.mjs` (trimmed, kept): used by GMPInstallManager, GMPUtils, XPIInstall, URLFormatter, TelemetryUtils, ClientEnvironment, OSKeyStore, UITour, ReinstallCheck, ASRouterTelemetry. No network code.
- `toolkit/components/timermanager`, `update-timer` category, `app.update.timerMinimumDelay`, `app.update.timerFirstInterval`, `app.update.lastUpdateTime.*`: generic timer service for Remote Settings, Region, Search and others.
- `app.update.channel`, `channel-prefs.js`, `ChannelPrefs.framework`, `MOZ_UPDATE_CHANNEL`: a string, no behaviour.
- `UpdRootD` / `GetUpdateRootDir`: still read by `TelemetryStorage` and `BrowserUsageTelemetry`; no listener, no network.
- `commonupdatedir.cpp` and `updatedefines.h`: moved, needed for the install hash and `MultiInstanceLock`.
- `XREAppData.updateURL` and its IPC plumbing: stays, empty string now. `build/appini_header.py` still defaults `AppUpdate:url` to empty.
- `org.mozilla.update-telemetry` xattr read in `nsAppRunner.cpp`, `ProfileMetrics.sys.mjs`, `TestDowngradePing.cpp`: nothing writes it now, readers see "absent". Removing needs the metric, test, gtest and docs together.
- `toolkit/mozapps/dmgInstallHelper`, `SMPrivilegedExecutables` generation, `--enable-mac-elevated-updates-with-generic-certs` (help text reworded), `MOZ_SMAUTHORIZEDCLIENTS_REQUIREMENTS`: a different feature.
- `about:restartrequired`, `AppMenuNotifications.sys.mjs`, popup-notification infrastructure: also used by add-on notifications.
- Vendored libs left on disk (D21), not built any more: `other-licenses/bsdiff`, `modules/xz-embedded`, `third_party/zucchini`. `tools/zucchini` (a separate configure project for CI) and `build/moz.configure/zucchini.configure` remain.
- Windows-only leftovers: `toolkit/components/taskscheduler/rust` (gkrust feature `backgroundtaskregistrar`), `toolkit/components/bitsdownload`, `MOZ_BITS_DOWNLOAD`, default-browser-agent, notification-server and push-helper options, `win32` types.
- Release/CI tooling outside scope: `tools/update-verify`, taskcluster update kinds and docker images, `python/mozrelease`, `testing/mozharness` update scripts, `config/createprecomplete.py` and packager `precomplete` support, `removed-files.in` rule for `update-settings.ini`, `testing/profiles` and geckodriver/tps pref writes of `app.update.disabledForTesting`.
- `intl/l10n/rust/l10nregistry-tests` still names `toolkit/updates/history.ftl` as a fixture string; `build/docs/defining-xpcom-components.md` still uses `nsUpdateService` as an example.
- Strings left because other users exist: `mr2022-background-update-toast-*` in `asrouter.ftl` (ASRouter toast test), `update-search-engine-success`.
