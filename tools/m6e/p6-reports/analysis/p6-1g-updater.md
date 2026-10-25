# P6.1g The updater: analysis

Base commit cb002d4874. Feature order 7: the VPN, Translations, generative AI, ML, Sync and Mozilla accounts are already deleted when this is integrated. The crash reporter (8) and Nimbus (9) are still present and are handled here.

## Summary

- The tree is already built with `ac_add_options --disable-updater` (mozconfig). So `MOZ_UPDATER`, `MOZ_UPDATE_AGENT` and `MOZ_MAINTENANCE_SERVICE` are unset in the current build. `toolkit/moz.build` only enters `mozapps/update` when `MOZ_UPDATER` is set, so none of `toolkit/mozapps/update` (the XPIDL, the JS modules, the `update-timer` category line, the updater binary, the elevation helper) is compiled or packaged today. Deleting it cannot change the macOS build output. What breaks is source that still names it.
- The work has four parts:
  1. Delete `toolkit/mozapps/update`, the `toolkit/xre/nsUpdate*` C++, and the dead-by-config UI in `browser/` (about dialog updater section, Settings "Updates" group and widgets, app-menu update banner and doorhangers, urlbar update tips and quick action, policies, TelemetryEnvironment update settings, ASRouter `needsUpdate`).
  2. Strip `#ifdef MOZ_UPDATER` / `AppConstants.MOZ_UPDATER` branches from first-party code (D21: first-party only).
  3. Remove the configure options (`build/moz.configure/update-programs.configure`, the mozconfig line). This is configure-time and forces a full rebuild.
  4. Remove the Glean `update`, `updater`, `update.settings`, `background_update` metrics and the `update` and `background-update` pings, and the references to them.
- Things that look like the updater but must stay (details in "Must stay"):
  - `UpdateUtils.sys.mjs` (channel, ABI, URL formatting, used by GMP, add-on URL formatting, URLFormatter, Telemetry).
  - `toolkit/components/timermanager` (`nsIUpdateTimerManager`, the `update-timer` category, `app.update.timerMinimumDelay`, `app.update.timerFirstInterval`, `app.update.lastUpdateTime.*`). Remote Settings, Region, Search, Suggest and others use it.
  - `commonupdatedir.cpp/.h` and `updatedefines.h`, needed by `toolkit/xre` and the profile service (install hash). They must be moved to `toolkit/xre`, not deleted.
  - The `UpdRootD` directory-service key.
  - `app.update.channel` with `channel-prefs.js` and `ChannelPrefs.framework`.
  - `AppMenuNotifications`, `about:restartrequired`, the `dmgInstallHelper`.
- Latent bug found: `BrowserContentHandler.sys.mjs` calls `lazy.UpdateManager` (`@mozilla.org/updates/update-manager;1`) unconditionally in the `OVERRIDE_NEW_MSTONE` and `OVERRIDE_NEW_BUILD_ID` branches. That service does not exist in the current build. It is hidden by the surrounding `try {} catch (ex) {}` and by `needHomepageOverride`. Rewrite those branches (see table).
- Configure-time edits: `mozconfig`, `build/moz.configure/update-programs.configure`, `build/moz.configure/zucchini.configure`, `browser/moz.configure`, `moz.configure` (project lists). Nothing in `browser/confvars.sh` mentions the updater.

## Files to delete

### Directories

| Path | Notes |
|---|---|
| `toolkit/mozapps/update/` (everything: `updater/`, `common/`, `content/`, `docs/`, `tests/`, the `.sys.mjs` modules, `nsIUpdateService.idl`, `components.conf`, `nsUpdateService.manifest`, `jar.mn`, the five Glean yaml files, `moz.build`) | Move `common/commonupdatedir.{cpp,h}` and the part of `common/updatedefines.h` that `MultiInstanceLock.cpp` needs to `toolkit/xre` first (see "Must stay"). `updater/macos-frameworks/UpdateSettings*` goes with it. |
| `toolkit/themes/osx/mozapps/update/` | only `updates.css`; also remove its line in `toolkit/themes/osx/mozapps/jar.mn` |
| `toolkit/locales/en-US/toolkit/updates/` | `backgroundupdate.ftl`, `elevation.ftl`, `history.ftl` |
| `browser/preferences widget`: `browser/components/preferences/widgets/update-state/` | `update-state.mjs/.css/.stories.mjs` |
| `modules/libmar/` | first-party; only built when `MOZ_UPDATER` (`toolkit/toolkit.mozbuild`), or by `tools/update-packaging` |
| `tools/update-programs/` | `moz.configure`, `confvars.sh`, `app.mozbuild`, `mach_commands.py`, README |
| `tools/update-packaging/` | MAR creation scripts, with `test/` |
| `testing/update/` | update mach tests (`mach update-test`), `tools/lint/test-manifest-toml.yml` mentions it |
| `toolkit/components/taskscheduler/` (except `rust/`, see Risks) | `TaskScheduler.sys.mjs`, `TaskSchedulerMacOSImpl.sys.mjs`, Win impls, `nsWinTaskScheduler.*`, IDLs, tests. Its only consumers are `BackgroundUpdate.sys.mjs` (deleted) and one `FirstStartup.sys.mjs` branch (stripped). |

### Loose files

- `browser/base/content/aboutDialog-appUpdater.js` (also remove from `browser/base/jar.mn`)
- `browser/modules/UpdatePolicyEnforcer.sys.mjs` (compulsory-restart after update; also remove from `browser/modules/moz.build`)
- `browser/modules/BackgroundTask_install.sys.mjs`, `browser/modules/BackgroundTask_uninstall.sys.mjs` (call `nsIUpdateManager.doInstallCleanup`; in no moz.build; listed in `tools/lint/extra-js-modules/allowlist.txt`)
- `browser/themes/shared/update-badge.svg` (remove from `browser/themes/shared/jar.inc.mn`)
- `toolkit/xre/nsUpdateDriver.cpp/.h`, `nsUpdateMutex.cpp/.h`, `nsUpdateSyncManager.cpp/.h` (compiled only under `MOZ_UPDATER`)
- `toolkit/xre/updaterfileutils_osx.mm/.h` (`IsRecursivelyWritable`, only called from `nsUpdateDriver.cpp` and `updater.cpp`)
- `toolkit/locales/en-US/chrome/mozapps/update/updates.properties`
- `browser/locales/en-US/updater/updater.ini`, `browser/locales/generate_ini.py` (only generates `updater.ini`)
- `build/update-settings.ini`
- `build/moz.configure/zucchini.configure` (included only from the updater block)
- `docs`: `toolkit/mozapps/update/docs` goes with the directory; edit `toolkit/docs/index.md` (toctree line `mozapps/update/docs/index`), `browser/docs/overview.md`, `mots.yaml` (entry at the `toolkit/mozapps/update/**/*` glob)

### Left on disk (vendored, D21), once nothing builds them

`other-licenses/bsdiff`, `modules/xz-embedded`, `third_party/zucchini`. Their only DIRS entries are in `toolkit/toolkit.mozbuild`, `config/external/moz.build` and `tools/update-programs/app.mozbuild`; those entries are removed.

## References from staying code

Legend for the last column: edit, remove, replace, move.

### Build and packaging

| File | What | Action |
|---|---|---|
| `toolkit/moz.build` | `DIRS += ["mozapps/update"]` and `mozapps/update/common` under `MOZ_UPDATER`/`MOZ_MAINTENANCE_SERVICE`; `components/maintenanceservice` | remove those three blocks (the maintenance service dir no longer exists) |
| `toolkit/toolkit.mozbuild` | `MOZ_UPDATER`: `/modules/libmar`, `bspatch`, `bsdiff`, `zucchini` | remove both blocks |
| `config/external/moz.build` | `external_dirs += ["modules/xz-embedded"]` under `MOZ_UPDATER` | remove block |
| `toolkit/xre/moz.build` | `/toolkit/mozapps/update/common/commonupdatedir.cpp`; `../mozapps/update/common` include; `nsUpdate*` sources under `MOZ_UPDATER`; `DEFINES["MOZ_UPDATER"]`; `updaterfileutils_osx.*`; `MAR_NSS` define | repoint the first two to the moved files; remove the rest |
| `toolkit/components/build/components.conf` | `if defined('MOZ_UPDATER')`: `nsUpdateProcessor`, `nsUpdateSyncManager`, `nsUpdateMutex` | remove block |
| `toolkit/modules/moz.build` | `DEFINES` loops listing `MOZ_UPDATER`, `MOZ_MAINTENANCE_SERVICE`, `MOZ_UPDATE_AGENT` (and `UpdateUtils.sys.mjs` `Files()` stays) | remove the three names |
| `toolkit/modules/AppConstants.sys.mjs` | `MOZ_UPDATER`, `MOZ_MAINTENANCE_SERVICE`, `MOZ_UPDATE_AGENT` | remove the three keys (keep `MOZ_UPDATE_CHANNEL`; keep `MOZ_BITS_DOWNLOAD`, see Risks). Do after all consumers below are edited. |
| `tools/@types/subs/AppConstants.sys.d.mts`, `tools/@types/generated/*` | AppConstants keys, `nsIUpdate*` types, `UpdateUtils` path | regenerate with the types tool, or edit by hand |
| `build/moz.build` | `ACCEPTED_MAR_CHANNEL_IDS` define, `MAR_CHANNEL_ID` and `MOZ_UPDATER` in `appini_defines`, `MOZ_APPUPDATE_HOST` | remove |
| `build/application.ini.in` | `#if MOZ_UPDATER [AppUpdate] URL=https://aus5...` | remove block |
| `toolkit/xre/CreateAppData.cpp` | `ReadString(parser, "AppUpdate", "URL", ...)` | remove line; `updateURL` field stays empty (see Must stay) |
| `browser/app/moz.build` | `MOZ_UPDATER` adds `org.mozilla.updater` to `extra_files` | remove `if` block (keep `dmgInstallHelper` move) |
| `browser/app/generate_channel_prefs.py`, `browser/app/profile/channel-prefs.js` | channel substitution | keep |
| `browser/app/generate_info_plist.py`, `browser/app/macbuild/Contents/Info.plist.in` | `SMPrivilegedExecutables` from `MOZ_SMPRIVILEGEDEXECUTABLES_REQUIREMENTS` (dmgInstallHelper + updater) | keep; the configure function drops the updater key |
| `browser/installer/package-manifest.in` | `MOZ_UPDATER` blocks: `Contents/Library/LaunchServices`, `update-settings.ini`, `icons/updater.png`, `[Updater]` `updater.app/` ; `updater.ini` | evaluate as undefined and delete the blocks (packaging-time, not configure) |
| `browser/installer/allowed-dupes.mn` | `LaunchServices/org.mozilla.updater`, `updater.app/...` | remove 3 lines |
| `browser/installer/removed-files.in` | `update-settings.ini` removal rule | keep (it deletes stale files from older installs); optional |
| `browser/locales/moz.build` | `MOZ_UPDATER` `updater.ini` generation | remove block |
| `browser/components/moz.build` | `MOZ_UPDATER` adds `tests/browser/whats_new_page/browser.toml` | remove block |
| `browser/moz.build`, `browser/components/preferences/moz.build` | `DEFINES["MOZ_UPDATE_AGENT"]` | remove |
| `browser/moz.configure` | `nsis_installer_files` depends on `--enable-maintenance-service` | replace with `maintenance_service=False` (Windows-only, but configure would error on the missing option) |
| `moz.configure` | `"tools/update-programs"` in two `project not in (...)` tuples | remove the string |
| `mfbt/moz.build` | list mentioning `tools/update-programs` | remove entry |
| `python/mach/mach/command_util.py` | `"update"` and `"update-test"` mach command references | remove both |
| `python/mozbuild/mozbuild/mozinfo.py` | `d["updater"] = substs.get("MOZ_UPDATER") == "1"` | remove; then fix the manifests that use `updater` (Tests section) |
| `python/mozbuild/mozbuild/artifacts.py` | `updater.app/Contents/MacOS/org.mozilla.updater` in `_paths_no_keep_path` | remove entry |
| `python/mozbuild/mozbuild/repackaging/mar.py`, `mach_commands.py` (`--mar-channel-id`) | MAR repackaging | remove with `tools/update-packaging` (release tooling, no M6E use); optional |
| `config/createprecomplete.py`, `python/mozbuild/mozpack/packager/l10n.py`, `unpack.py` | `precomplete` file for the updater; skip-lists for `update-settings.ini`, `UpdateSettings.framework` | leave (generic packaging; harmless). Open question. |
| `Cargo.toml`, `.gitignore`, `.hgignore`, `build/sparse-profiles/*`, `docs/config.yml`, `mots.yaml`, `tools/lint/*`, `.prettierignore`, `eslint-*.config.mjs` | entries for deleted paths | remove entries for paths this feature deletes only (see Prefs/metrics/strings and Build-system sections) |
| `tools/m6e/acceptance.py` | `REMOVED_CONSTANTS` has `"Updater": "MOZ_UPDATER"`; `REMOVED_SETTINGS_GROUPS` has `"updates"` | after the key is gone from AppConstants the check reads `undefined` (still "off"); confirm how the check treats `undefined` and keep or replace with "update service contract not registered" |

### C++ and Objective-C

| File | What | Action |
|---|---|---|
| `toolkit/xre/nsAppRunner.cpp` | `#include "mozilla/glean/ToolkitMozappsUpdateSharedMetrics.h"` (unused); `#if defined(MOZ_UPDATER)` include of `nsUpdateDriver.h`/`nsUpdateSyncManager.h`; the `ShouldNotProcessUpdates*` block; the `ProcessUpdates`/`WriteUpdateCompleteTestFile` block in `XREMain::XRE_mainRun`; `-test-process-updates` args; the `org.mozilla.update-telemetry` xattr read that sets `isNewUpdate` in the lock file | remove all; the xattr/`isNewUpdate` piece is optional. Keep `GetUpdateURL`, `GetDefaultUpdateChannel` (channel). Keep the `app.update.channel` reads. |
| `toolkit/xre/MacRunFromDmgUtils.mm` | `#ifdef MOZ_UPDATER #include "nsUpdateDriver.h"` | remove |
| `toolkit/xre/nsCommandLineServiceMac.mm` | `#ifdef MOZ_UPDATER` branch (relaunch via `updater.app`) and `#ifndef MOZ_UPDATER` around `kRelaunchShell`; `org.mozilla.updater` suffix checks | keep only the M6E `/bin/sh` relaunch path; drop the updater branch and the two `hasSuffix:@"org.mozilla.updater"` conditions and the comment about it |
| `toolkit/xre/MacLaunchHelper.mm/.h` | `InstallElevatedUpdater`, `AbortElevatedUpdate`, `LaunchElevatedUpdate` | remove these three (keep `InstallPrivilegedHelperWithId`, used by the DMG helper) |
| `toolkit/xre/nsXREDirProvider.cpp/.h` | `GetUpdateRootDir`, `XRE_UPDATE_ROOT_DIR`, `XRE_OLD_UPDATE_ROOT_DIR` | keep (see Must stay); `#include "commonupdatedir.h"` path changes with the move; line 7 has a stray `#  include` that can be tidied |
| `toolkit/xre/MultiInstanceLock.cpp` | `#include "updatedefines.h"` | point at the moved/trimmed header |
| `toolkit/xre/LauncherRegistryInfo.cpp` | includes `commonupdatedir.h` | Windows-only file; leave |
| `toolkit/components/backgroundtasks/BackgroundTasks.cpp/.h` | `IsUpdatingTaskName` (only caller is the removed nsAppRunner block); `backgroundupdate` in `IsEphemeralProfileTaskName`; comments | remove `IsUpdatingTaskName`; drop `"backgroundupdate"` from the other list |
| `toolkit/profile/nsToolkitProfileService.cpp` | comment mentioning `backgroundupdate` near the ephemeral profile check | edit comment |
| `dom/security/nsContentSecurityUtils.cpp` | allowlist entries `chrome://mozapps/content/update/history.xhtml` and `.../updateElevation.xhtml` | remove both lines |
| `intl/locale/LocaleService.cpp` | `#ifdef MOZ_UPDATER MOZ_ASSERT(!locale.IsEmpty())` | remove the `#ifdef` and assert (D21 style) |
| `ipc/glue/test/gtest/TestMacChildProcessPaths.cpp` | `"updater.app"` in `kNonChildProcessBundles` | remove entry |
| `build/clang-plugin/ThreadAllows.txt` | comment naming `nsUpdateProcessor` | edit comment; optional |
| `xpcom/build/XREAppData.h`, `StaticXREAppData.h`, `dom/ipc/PContent.ipdl`, `ContentParent/Child.*` | `updateURL` plumbing | leave (IPC change would rebuild dom/ipc for no gain); it is an empty string once `[AppUpdate]` is gone |

### Front-end (browser/ and toolkit/ JS, XHTML, CSS, FTL)

| File | What | Action |
|---|---|---|
| `browser/components/BrowserComponents.manifest` | `#ifdef MOZ_UPDATER` categories: `UpdateListener.maybeShowUnsupportedNotification`, `UpdatePolicyEnforcer.registerObservers`, `UpdateListener.reset` | remove both blocks |
| `browser/components/BrowserGlue.sys.mjs` | `_checkForOldBuildUpdates` and its call; idle task `BackgroundUpdate` (uses `nsIApplicationUpdateServiceStub`); `isAllowed("postUpdateCustomPage")` in the upgrade-dialog check | remove first two; for the policy check drop the line (the policy goes) |
| `browser/components/BrowserContentHandler.sys.mjs` | `UpdateManager` lazy service getter; `getPostUpdateOverridePage`; `spinForLastUpdateInstalled`, `spinForUpdateInstalledAtStartup`; in `OVERRIDE_NEW_MSTONE` the whole update-object/Nimbus-WNP/`handleUpdateSuccessTask`/`GleanPings.update.submit` code; `OVERRIDE_NEW_BUILD_ID` case | simplify: new milestone keeps only the `startup.homepage_override_url` page (M6E branding already blanks it) and `LaterRun.enable`; remove the Glean `update.*` and `GleanPings.update` calls |
| `browser/components/ProfileDataUpgrader.sys.mjs` | version < 181 block (`MOZ_MAINTENANCE_SERVICE`, `app.update.service.enabled`, `Glean.update.autoReenableStagedUpdates`) | remove the block, keep the numbering |
| `browser/components/ReinstallCheck.sys.mjs` | `UpdateUtils.UpdateChannel` | keep (UpdateUtils stays) |
| `browser/base/content/aboutDialog.xhtml` | `#ifdef MOZ_UPDATER` blocks (update deck, buttons, channel text, help and feedback links, release notes box) and `#ifndef MOZ_UPDATER` wrapper | keep the `#ifndef` content as plain markup, delete the `#ifdef` content, remove directives |
| `browser/base/content/aboutDialog.js` | `loadSubScript(aboutDialog-appUpdater.js)`, the three `if (AppConstants.MOZ_UPDATER)` blocks, `import-globals-from aboutDialog-appUpdater.js` | remove |
| `browser/base/content/aboutDialog.css` | rules for `#updateBox`, `#updateInfo`, `#updateDeck`, `#updateButton`, `#updateIcon`, `.update` | remove |
| `browser/base/content/browser.js`, `browser.js.globals` | `UpdateUtils` lazy global | keep unless `UpdateUtils` is unused in browser.js; check |
| `browser/base/content/appmenu-viewcache.inc.xhtml` | `#appMenu-update-banner` | remove the toolbarbutton |
| `browser/components/customizableui/content/panelUI.inc.xhtml` | popupnotifications `appMenu-update-available/manual/unsupported/restart/other-instance-notification` | remove 5 elements (keep `addon-installed` etc.) |
| `browser/components/customizableui/content/panelUI.js` | `show-update-progress` observer add/remove/case; `appMenu-update-banner` command case; `_showBannerItem`, `_clearBannerItem`, `_onBannerItemSelected` and the calls | remove banner machinery entirely (the banner only supports update ids) |
| `browser/themes/shared/customizableui/panelUI-shared.css` | `badge-status="update-*"`, `notificationid="update-*"`, `.appMenu-update-banner-*`, keyframes `appMenu-update-banner-pulse`, `#appMenu-update-banner` rules | remove |
| `browser/themes/shared/toolbarbutton-icons.css`, `notification-icons.css` | `badge-status="update-*"`, `popupid="update-*"` | remove selectors |
| `browser/components/preferences/config/about-firefox.mjs` | top-level `UpdateUtils.PER_INSTALLATION_PREFS[...]` use; `UpdatesHelpers`; settings `updatesManagedByOS`, `updateApp`, `updateState`, `updateAppInfo`, `disableShowUpdateHistory`, `showUpdateHistory`, `installationFieldset`, `updateSettingCrossUserWarning`, `updateRadioGroup`, `backgroundUpdate`, `showUpdatePromptsHeader`, `showUpdatePrompts`; group `updates` (`iconSrc: update-badge.svg`); `/* global */` line | remove the whole file's update content; keep `supportLinksGroup`, `supportGetHelp`, `supportShareIdeas` if still used by other panes, or move them. Confirm which pane renders the `updates` group (`browser/components/preferences/config/LegacyPaneMappings.mjs`, `main.js` `initSettingGroup("updates")`). |
| `browser/components/preferences/main.js` | `/* import-globals-from aboutDialog-appUpdater.js */`; lazy `BackgroundUpdate`, `UpdateListener`; `initSettingGroup("updates")`; `if (AppConstants.MOZ_UPDATER)` block | remove |
| `browser/components/preferences/main.inc.xhtml` | `#ifdef MOZ_UPDATER <script aboutDialog-appUpdater.js>` | remove |
| `browser/components/preferences/preferences.js`, `privacy.js`, `config/privacy.mjs` | `gApplicationUpdateService`, `AppUpdater` lazy getters; `appUpdateStatus` setting and its use in `privacyCard` deps | remove getters; remove the `appUpdateStatus` setting and its dep |
| `browser/components/preferences/widgets/security-privacy/security-privacy-card/security-privacy-card.mjs` | `AppUpdater`, `appUpdateStatus`, `buildUpdateElement`, `#okUpdateStatus`, `L10N_IDS.update*` | remove update bullet and count term |
| `browser/components/preferences/jar.mn` | `update-state.mjs`, `update-state.css` | remove 2 lines |
| `browser/components/preferences/config/LegacyPaneMappings.mjs`, `preferences.xhtml` | `updates` mapping | remove |
| `browser/modules/ObserverForwarder.sys.mjs` | `MOZ_UPDATER` block adding `UpdateListener` observers | remove |
| `browser/components/asrouter/modules/ASRouterTargeting.sys.mjs` | lazy `AUS` (`nsIApplicationUpdateService`), `UpdateCheckSvc` (`nsIUpdateChecker`); `needsUpdate` / `checkBrowserNeedsUpdate` provider using `MOZ_UPDATER` | remove getters; make the `needsUpdate` targeting attribute return `false` or delete it (grep the message JSON and tests that use `needsUpdate`) |
| `browser/components/asrouter/modules/ASRouterTelemetry.sys.mjs` | `UpdateUtils.getUpdateChannel` | keep |
| `browser/components/urlbar/UrlbarProviderInterventions.sys.mjs` | the `update` document, `AppUpdater` lazy, `appUpdater`, tips `UPDATE_ASK/REFRESH/RESTART/WEB/CHECKING`, `checkForBrowserUpdate`, `resetAppUpdater`, `installBrowserUpdateAndRestart` | remove update tips; keep the `refresh` (clear/refresh profile) intervention (the "update" branch can also set `REFRESH`, so re-check that logic) |
| `browser/components/urlbar/content/UrlbarShared.mjs`, `UrlbarPrefs.sys.mjs`, `browser/locales/en-US/browser/urlbar.ftl` | `INTERVENTION_TIP_TYPE.UPDATE_*`, `intervention-update-*` strings, update-related prefs | remove |
| `browser/components/urlbar/QuickActionsLoaderDefault.sys.mjs` | lazy `AUS`; the `update` quick action; `quickactions-update`/`quickactions-cmd-update` in `browser.ftl` | remove entry and strings |
| `browser/components/uitour/UITour.sys.mjs` | `UpdateUtils.getUpdateChannel` for `defaultUpdateChannel` | keep |
| `browser/components/enterprisepolicies/Policies.sys.mjs` | handlers `AppAutoUpdate`, `AppUpdatePin`, `AppUpdateURL`, `BackgroundAppUpdate`, `DisableAppUpdate`, `ManualAppUpdateOnly`, `OverridePostUpdatePage`, `RelaunchRequired` (writes `app.update.compulsory_restart`); `"app.update."` in the `Preferences` allowed-prefix list and `app.update.channel/lastUpdateTime/migrated` deny entries; comments naming `UpdateService.sys.mjs` | remove handlers; for the pref lists remove the `app.update.` prefix and the three deny entries |
| `browser/components/enterprisepolicies/schemas/policies-schema.json` | the 8 policy schemas above | remove |
| `browser/components/enterprisepolicies/content/aboutPolicies.js` | `BackgroundAppUpdate: "BackgroundAppUpdate2"` | remove entry |
| `browser/locales/en-US/browser/policies/policies-descriptions.ftl` | `policy-AppAutoUpdate`, `AppUpdatePin`, `AppUpdateURL`, `BackgroundAppUpdate2`, `DisableAppUpdate`, `ManualAppUpdateOnly`, `OverridePostUpdatePage`, `RelaunchRequired` | remove 8 messages |
| `browser/locales/en-US/browser/policy-messages.ftl` | `policy-update-now` | remove if unused after the above (check) |
| `toolkit/components/enterprisepolicies/EnterprisePoliciesParent.sys.mjs` | comment about the Background Update Agent and `DisableAppUpdate` | edit comment |
| `toolkit/modules/FirstStartup.sys.mjs` | `TaskScheduler` lazy import; two `AppConstants.MOZ_UPDATE_AGENT` blocks; `deleteTasksEndTime` | remove; `Glean.firstStartup.deleteTasksTime` metric in `toolkit/modules/metrics.yaml` then has no setter (remove that metric too) |
| `toolkit/modules/Troubleshoot.sys.mjs` | `if (AppConstants.MOZ_UPDATER) data.updateChannel = ...` | remove; update `toolkit/modules/tests/xpcshell/test_Troubleshoot.js` and the `aboutSupport` consumer |
| `toolkit/content/aboutSupport.xhtml/.js` | `#ifdef MOZ_UPDATER` rows: update dir, update history, update channel; `update-dir-button`, `show-update-history-button` handlers; `UpdRootD` reads; `isPackagedApp` hide logic | remove all |
| `toolkit/locales/en-US/toolkit/about/aboutSupport.ftl` | `app-basics-update-channel`, `-update-dir`, `-update-history`, `-show-update-history` | remove |
| `toolkit/components/telemetry/app/TelemetryEnvironment.sys.mjs` | `UpdateServiceStub` lazy under `MOZ_UPDATER`; prefs `app.update.interval`, `app.update.service.enabled`, `app.update.silent`; `UpdateUtils.PER_INSTALLATION_PREFS` observer topics; `_loadAsyncUpdateSettings*`; `Glean.updater.available`, `Glean.updateSettings.*`; `settings.update.{enabled,autoDownload,background}` | remove; keep `settings.update.channel` from `UpdateUtils`; update `TelemetryEnvironmentTesting.sys.mjs`, `test_TelemetryEnvironment*.js` and `docs/data/environment.rst` |
| `toolkit/components/telemetry/app/TelemetryStorage.sys.mjs`, `browser/modules/BrowserUsageTelemetry.sys.mjs` | `UpdRootD` (uninstall ping path, profile count file) | keep (`UpdRootD` stays) |
| `toolkit/components/glean/metrics_index.py` | `toolkit/mozapps/update/metrics.yaml` (gecko), `shared_metrics.yaml` (desktop and background), `background_task_metrics.yaml`, `pings.yaml`, `shared_pings.yaml`; `background_update_metrics`, `background_update_pings`; app id `firefox.desktop.background.update` | remove all of these |
| `toolkit/components/glean/tests/pytest/test_yaml_indices.py` | names `background_update_metrics`, `background_update_pings` | remove names |
| `browser/components/metrics.yaml` | category `background_update` (`reasons_to_not_update`, `time_last_update_scheduled`) sending in ping `background-update`; comment naming `mozapps/update/metrics.yaml` | remove the category (it has no setter outside the deleted task) |
| `toolkit/components/nimbus/metrics.yaml` | `- background-update` in six `send_in_pings` lists | remove those six lines (Nimbus is feature 9, so it is ours now). The Glean parser fails on an unknown ping. |
| `toolkit/components/nimbus/FeatureManifest.yaml` | `backgroundUpdateAutomaticRestart`, `backgroundUpdateCheckPolicy`, `backgroundUpdate` (pref `app.update.background.allowUpdatesForUnelevatedInstallations`) | remove the three features; check `toolkit/components/nimbus/schemas`, docs and tests listing them |
| `toolkit/components/backgroundtasks/defaults/backgroundtasks.js` | `app.update.background.automaticRestartEnabled`, `checkPolicy.*` | remove |
| `toolkit/components/backgroundtasks/docs/index.md`, `BackgroundTask_message.sys.mjs` | text about the background update task | edit text |
| `toolkit/components/backgroundtasks/tests/` | see Tests | |
| `remote/shared/RecommendedPreferences.sys.mjs` | `app.update.disabledForTesting` | remove entry (optional; harness prefs elsewhere under `testing/` are left) |
| `browser/branding/docs/index.md`, `browser/docs/DistributionIni.md` | mention `app.update.interval`, `UpdateUtils.getUpdateChannel` | edit text only if wording becomes wrong |
| `browser/locales/en-US/browser/aboutDialog.ftl` | all `update-*` / `settings-update-*` / `aboutdialog-update-*` messages (~15) | remove; keep `aboutdialog-m6e-*`, version and channel strings still used |
| `browser/locales/en-US/browser/appmenu.ftl` | `appmenuitem-banner-update-*` (5), `appmenu-nova-update-title/description` | remove |
| `browser/locales/en-US/browser/appMenuNotifications.ftl` | `appmenu-update-*` (10 messages) | remove |
| `browser/locales/en-US/browser/preferences/preferences.ftl` | `update-application-*`, `update-history-2`, `update-setting-write-failure-*`, `update-in-progress-*`, `security-privacy-status-update-*` (5). `update-search-engine-success` is search, keep. | remove |
| `browser/locales/en-US/browser/newtab/asrouter.ftl` | `mr2022-background-update-toast-*` | remove if no other user (grep `background-update-toast` in ASRouter templates) |
| `browser/locales/en-US/browser/browser.properties` | check for update strings | grep, remove if only the deleted UI used them |
| `toolkit/locales/jar.mn` | line `mozapps/update/updates.properties` | remove |

### Other staying references that are not the updater (checked, no action)

- `UpdateTimerManager.sys.mjs` and `nsIUpdateTimerManager` users: `Region`, `SearchService`, `RemoteSettings` (`servicesSettings.manifest`), `ContentRelevancyManager`, `PageThumbs`, `UntrustedModulesPing`, `XPIProvider`, `RemoteSettingsExperimentLoader`, `SuggestBackendRust`, `extensions.manifest`.
- Add-on `update_url`, `applyBackgroundUpdates`, `AddonRepository`, `GMPInstallManager` (`redirectChromiumUpdateService`), safebrowsing `updateURL`, URL-classifier "update" strings, `UpdateLogin`, `nsTouchBarUpdater`, `updateChannels` in IPDL: unrelated.
- `testing/profiles/*`, `testing/marionette/.../geckoinstance.py`, `testing/geckodriver`, `testing/tps`: write `app.update.disabledForTesting` to prefs nobody reads after this. Left alone (upstream harness files).

## References from earlier features (ignore)

Searched `browser/components/ipprotection`, `genai`, `aiwindow`, `toolkit/components/ml`, `translations`, `services/sync`, `services/fxaccounts` for updater identifiers (`app.update`, `nsIApplicationUpdateService`, `AppUpdater`, `UpdateListener`). No real references. Hits were name coincidences (`updateIconStatus`, `recordUpdateTelemetry`, `memories-update-settings`, `FormAutofillSync`). The sync and accounts sender of `AppMenuNotifications` ("fxa-needs-authentication", `AccountsGlue.sys.mjs`) will be gone, which affects only how the panel notification tests choose a sample id (see Tests).

## Prefs/metrics/strings to remove

### Prefs

- `browser/app/profile/firefox.js`: remove `app.update.checkInstallTime`, `.log`, `.log.file`, `.backgroundMaxErrors`, `.download.maxAttempts`, `.elevate.maxAttempts`, `.suppressPrompts` (Nightly `#ifdef`), `.notifyDuringDownload`, `.auto`, `.staging.enabled`, `.service.enabled` (`MOZ_MAINTENANCE_SERVICE`), `.BITS.enabled` (`MOZ_BITS_DOWNLOAD`), `.langpack.enabled`, the `MOZ_UPDATE_AGENT` block (`.background.*`), `.noWindowAutoRestart.*`, and the comment lines about update pins and branding. Keep `app.update.timerMinimumDelay` (120) and `app.update.timerFirstInterval` (30000) because `UpdateTimerManager` reads them; keep the leading comment block for those two, reword it to say it is the timer manager.
- `toolkit/components/backgroundtasks/defaults/backgroundtasks.js`: remove `app.update.background.automaticRestartEnabled` and `checkPolicy.*` (4 prefs).
- `modules/libpref/init/all.js`: nothing app.update except `MOZ_UPDATE_CHANNEL` conditionals; keep.
- `modules/libpref/Preferences.cpp`: `app.update.channel` handling and the `app.update.lastUpdateTime.` list entry stay.
- `browser/components/enterprisepolicies/Policies.sys.mjs`: see table.
- `browser/app/profile/m6e.js`: no `app.update.*` lock exists and none is needed (see Must stay).

### Glean and telemetry

- Delete the five yaml files with the directory, and delete categories: `update`, `updater`, `update.settings`, `background_update` (both the one in `toolkit/mozapps/update/background_task_metrics.yaml` and the one in `browser/components/metrics.yaml`), pings `update` and `background-update`, plus the app id `firefox.desktop.background.update`.
- Telemetry (non-Glean) has no `UPDATE_*` histograms, scalars or events in `Histograms.json`/`Scalars.yaml`/`Events.yaml`.
- `Glean.firstStartup.deleteTasksTime`: its only setter is the stripped `MOZ_UPDATE_AGENT` block; remove it from `toolkit/modules/metrics.yaml`.

### Strings

See the FTL rows above. Also `toolkit/locales/en-US/toolkit/updates/*.ftl`, `toolkit/locales/en-US/chrome/mozapps/update/updates.properties` and `browser/locales/en-US/updater/updater.ini` are deleted with their dirs. Check `browser/locales/l10n.toml`/`jar.mn` globs: the FTL dirs are picked up by glob, so no explicit entries.

## Build-system edits

### Configure-time (full rebuild, batch them into one change)

1. `mozconfig`: delete `ac_add_options --disable-updater` (otherwise configure fails on an unknown option once the option is removed).
2. `build/moz.configure/update-programs.configure`: remove
   - `--enable-updater` and `MOZ_UPDATER` (config and define)
   - `--enable-unverified-updates`, `MOZ_VERIFY_MAR_SIGNATURE`, `DISABLE_UPDATER_AUTHENTICODE_CHECK`
   - `--enable-mac-elevated-updates-with-generic-certs`: keep the `dmgInstallHelper` requirement, drop the `"org.mozilla.updater"` entry from `smprivilegedexecutables_list`
   - `MAR_CHANNEL_ID`, `ACCEPTED_MAR_CHANNEL_IDS`, `--enable-nss-mar`/`MOZ_USE_NSS_FOR_MAR`
   - `--enable-maintenance-service`/`MOZ_MAINTENANCE_SERVICE`, `--disable-update-agent`/`MOZ_UPDATE_AGENT`
   - the `with only_when("--enable-updater")` patch-format block (`MOZ_BSPATCH`, zucchini include)
   - keep (not the updater): the DMG install helper option, `MOZ_SMAUTHORIZEDCLIENTS_REQUIREMENTS`, default browser agent, notification server, push notification helper (Windows leftovers, out of scope)
3. `build/moz.configure/zucchini.configure`: delete (only included from the block above).
4. `browser/moz.configure`: `nsis_installer_files` no longer references `--enable-maintenance-service`.
5. `moz.configure`: remove `"tools/update-programs"` from both project tuples.
6. `toolkit/moz.configure`: the `include("../build/moz.configure/update-programs.configure")` line stays (file stays, trimmed). Leave `MOZ_BITS_DOWNLOAD` there (Windows, see Risks).
7. `browser/confvars.sh`: no change. `MOZ_UPDATE_CHANNEL` (`init.configure`) stays.
8. Removing options that were already off should leave `mozilla-config.h` and most generated headers byte-identical, but `config.status`, `buildconfig` and `AppConstants` change, so assume a long rebuild.

### C++/ObjC (needs `./mach build`, no configure)

`toolkit/xre/*` (nsAppRunner.cpp, moz.build, MacRunFromDmgUtils.mm, nsCommandLineServiceMac.mm, MacLaunchHelper.mm/.h, deleted nsUpdate*, updaterfileutils_osx, moved commonupdatedir.*, MultiInstanceLock.cpp), `toolkit/components/build/components.conf`, `toolkit/components/backgroundtasks/BackgroundTasks.cpp/.h`, `dom/security/nsContentSecurityUtils.cpp`, `intl/locale/LocaleService.cpp`, `ipc/glue/test/gtest/TestMacChildProcessPaths.cpp`, plus the Glean generated bindings for the deleted metrics. Without the configure change these edits compile on their own; do them in the same change so the build runs once.

### Front-end only (`./mach build faster`)

All JS, XHTML, CSS, FTL, `jar.mn`, `.manifest`, `moz.build` JS/jar lists, `package-manifest.in`.

### Others

- Cargo/Rust: nothing required. Optional Windows-only leftovers listed under Risks.
- `python/mach/mach/command_util.py`, `python/mozbuild/*`, `tools/lint/*`, `.prettierignore`, `eslint-*.config.mjs`, `Cargo.toml`: housekeeping for deleted paths.

## Tests to delete or edit

Delete:
- `toolkit/mozapps/update/tests/` (with the directory); `testing/update/`; `tools/update-packaging/test/`; `modules/libmar/tests/`
- `browser/components/tests/browser/whats_new_page/` (dir) and `browser_bug538331.js` (+ its `run-if = ["updater"]` entry in `browser/components/tests/browser/browser.toml`)
- `browser/components/enterprisepolicies/tests/browser/`: `browser_policy_app_update.js`, `browser_policy_app_auto_update.js`, `browser_policy_background_app_update.js`, `browser_policy_override_postupdatepage.js`, `disable_app_update/` (dir); in `tests/xpcshell/`: `test_appupdatepin.js`, `test_appupdateurl.js`; plus entries in the two `.toml` files
- `browser/components/urlbar/tests/browser-tips/`: `browser_updateAsk.js`, `browser_updateRefresh.js`, `browser_updateRestart.js`, `browser_updateWeb.js`, `suppress-tips/` (dir) and their `browser.toml` lines
- `browser/components/customizableui/test/browser_panelUINotifications_novaUpdateBanner.js`
- `browser/components/preferences/tests/browser_advanced_update.js`, `browser_updates_managed_by_os.js` (and the `run-if = ["updater"]` entry in `tests/browser.toml`)
- `browser/modules/test/browser/browser_UpdatePolicyEnforcer.js`, `browser/modules/test/unit/test_UpdatePolicyEnforcer.js`, `test_UpdatePolicySettings.js` (+ manifests)
- `toolkit/components/telemetry/tests/browser/browser_UpdatePingSuccess.js` (+ manifest line)
- `toolkit/components/backgroundtasks/tests/`: `BackgroundTask_automaticrestart.sys.mjs`, `BackgroundTask_shouldprocessupdates.sys.mjs`, `BackgroundTask_shouldnotprocessupdates.sys.mjs`, `BackgroundTask_update_sync_manager.sys.mjs`, `BackgroundTask_policies.sys.mjs` and the matching `xpcshell/test_backgroundtask_*` files plus `xpcshell.toml` entries
- `toolkit/components/taskscheduler/tests/`

Edit:
- `browser/components/customizableui/test/browser_panelUINotifications*.js` (`_fullscreen`, `_fullscreen_noAutoHideToolbar`, `_multiWindow`, `_modals`, `_bannerVisibility`, and the main file): they use the real `update-manual`/`update-restart`/`update-downloading` ids and the `appMenu-update-*` elements. Either rewrite them around a remaining notification id (`addon-installed`) or delete; the banner-specific cases go.
- `browser/base/content/test/browser-static/browser_sentence_case_strings.js` (banner and `update-*` ids), `browser_all_files_referenced.js` (entries for deleted files and `TaskScheduler`), `browser-general/browser_remoteTroubleshoot.js` and `browser-general/browser.toml` (the `"updater"` condition, "depends on UpdateUtils .Locale"), `browser-about/browser_aboutDialog_distribution.js`, `browser_aboutDialog_referrals.js`, `browser-performance/browser_startup_mainthreadio.js`
- `browser/components/urlbar/tests/browser-tips/head.js` (update helpers), `browser_searchTips.js`, `browser_searchTips_interaction.js` (use `UpdRootD` to find update status files), `README.txt`; `browser-quickactions/browser_general.js`; `unit/test_quickaction_telemetry.js`; `browser-engagementTelemetry/browser_glean_telemetry_engagement_tips.js`
- `browser/components/preferences/tests/privacy/browser_privacy_status_card.js`
- `browser/components/enterprisepolicies/tests/`: `xpcshell/test_macos_policy_validation.js`, `test_policy_failures.js`, `test_extensionsettings*.js` only if they use update policies (the `update_url` ones are add-on related and stay), `browser/browser_policy_disable_buttons.js`; `toolkit/components/enterprisepolicies/tests/xpcshell/test_null_policy_value.js`
- `browser/components/asrouter/tests/xpcshell/test_ASRouterTelemetry.js`, `asrouter/tests/unit/unit-entry.js`, `aboutwelcome/tests/unit/unit-entry.js` (stubs of `UpdateUtils`/AUS), `browser/components/uitour/test/browser_UITour.js` (channel assertions stay if `UpdateUtils` stays)
- `toolkit/components/telemetry/tests/unit/` (`TelemetryEnvironmentTesting.sys.mjs`, `test_TelemetryEnvironment.js`, `head.js`, `test_TelemetryReportingPolicy.js`, `test_TelemetrySendOldPings.js`, `test_PrefMigrationForTOU.js`): remove update settings assertions
- `toolkit/modules/tests/xpcshell/`: `xpcshell.toml` (the `"updater"` conditions), `test_firstStartup.js`, `test_Troubleshoot.js`; `test_UpdateUtils_updatechannel.js`/`test_UpdateUtils_url.js` stay but drop assertions about per-installation prefs if `UpdateUtils` is trimmed
- `caps/tests/mochitest/test_bug292789.html` (loads `chrome://mozapps/content/update/history.js`)
- `toolkit/xre/test/marionette/test_relaunch_dock_overlap.py` (names `updater.app`, `org.mozilla.updater`)
- `toolkit/components/glean/tests/pytest/test_yaml_indices.py`, `toolkit/components/glean/docs/user/instrumentation_tests.md` (only if it names a deleted metric)
- `testing/marionette/harness/.../test_marionette.py` and `integration-tests.toml` (updater mentions), `testing/raptor/test/test_raptor.py`: check, probably pref names only
- Manifest conditions using mozinfo `updater` (`browser/base/content/test/browser-general/browser.toml`, `browser/components/tests/browser/browser.toml`, `toolkit/modules/tests/xpcshell/xpcshell.toml`, `browser/components/preferences/tests/browser.toml`): remove the condition once the key is gone from `mozinfo.py`
- `tools/@types/generated/`, `.prettierignore` (2 entries), `eslint-ignores.config.mjs`, `eslint-test-paths.config.mjs`, `eslint-rollouts.config.mjs` (6 entries), `tools/lint/license.yml`, `tools/lint/extra-js-modules/allowlist.txt` (8 entries), `tools/lint/test-manifest-toml.yml`: only the entries for paths deleted here

## Must stay (with proof)

| Item | Why it stays | Unreachability / safety proof |
|---|---|---|
| `toolkit/modules/UpdateUtils.sys.mjs` | `GMPInstallManager` (Widevine and OpenH264, D10), `GMPUtils` (ABI), `XPIInstall` (`formatUpdateURL`), `URLFormatter` (`%CHANNEL%`), `TelemetryUtils`, `ClientEnvironment`, `OSKeyStore`, UITour, ReinstallCheck. | It contains no network code: only string formatting, reading `app.update.channel` and (on Windows) a JSON config file. After the removal of its per-installation-pref half, `app.update.*` prefs no longer have any reader. Recommended trim: remove `getAppUpdateAutoEnabled`, `setAppUpdateAutoEnabled`, `appUpdateAutoSettingIsLocked`, `PER_INSTALLATION_PREFS*`, `readUpdateConfigSetting`, `writeUpdateConfigSetting`, `appUpdateSettingIsLocked`, `summarizeLatestUpdate`, `initPerInstallPrefs`, config-file path code (about lines 149 to 1070). Keep `getUpdateChannel`, `UpdateChannel`, `formatUpdateURL`, `getLocale`, `ABI`, `OSVersion`. Optional second step. |
| `toolkit/components/timermanager` (`UpdateTimerManager.sys.mjs`, `nsIUpdateTimerManager`), the `update-timer` category, prefs `app.update.timerMinimumDelay`, `app.update.timerFirstInterval`, `app.update.lastUpdateTime.*`, `app.update.log` as a log-level pref | Generic timer service used by Remote Settings polling, Region, Search, Suggest, content relevancy, thumbnails, XPI signature verification, Nimbus loader. Name only is "update". | It makes no network requests of its own. Registrations come from `.manifest` files for first-party callbacks; web content and IPC cannot add categories. |
| `commonupdatedir.cpp/.h` (`GetInstallHash`) and the NS_t definitions from `updatedefines.h` | Used by `nsXREDirProvider::GetInstallHash` (profile-per-install hashing in `nsToolkitProfileService`), `MultiInstanceLock.cpp` (compiled on macOS). `CityHash` (`other-licenses/nsis/Contrib/CityHash`) is used too. | Action: move both files to `toolkit/xre`, trim `updatedefines.h` to what `MultiInstanceLock.cpp` uses (it pulls in `readstrings.h`, which goes away), update `toolkit/xre/moz.build`. Pure hashing, no I/O beyond paths. |
| `nsXREDirProvider::GetUpdateRootDir` and the `UpdRootD` / `OldUpdRootD` keys | Remaining `Services.dirsvc.get("UpdRootD")` callers: `TelemetryStorage.getUninstallPingPath`, `BrowserUsageTelemetry` profile-count file, tests. It only computes `~/Library/Caches/<vendor>/updates/<hash>`. | No listener, no network. Telemetry upload is off and locked. If these callers are later removed, delete the key too. |
| `app.update.channel`, `channel-prefs.js`, `ChannelPrefs.framework` (`toolkit/mozapps/macos-frameworks`), `MOZ_UPDATE_CHANNEL` | Channel name is read by `Preferences.cpp`, Telemetry, URL formatting, `nsAppRunner` profile names. | A string. No behavior depends on its value for network access. |
| `AppMenuNotifications.sys.mjs`, `popupnotification` infrastructure in `panelUI.js` | Also used for `addon-installed`, `addon-alert`, `theme-installed`, menu messages. | Local UI only. |
| `about:restartrequired` (`aboutRestartRequired.*`) | Shown when a content process version differs from the parent (e.g. the .app was replaced while running); not updater-owned. | Static local page. The AboutRedirector entry is unchanged. |
| `toolkit/mozapps/dmgInstallHelper` and `SMPrivilegedExecutables` key generation | Different feature (run-from-DMG install). `tools/m6e/package/finalize-app.sh` already strips the key at packaging. | Not part of this feature. Only the updater half of the configure data is removed. |
| `XREAppData.updateURL` and its IPC plumbing | Removing it touches `PContent.ipdl` and recompiles dom/ipc. | After removing `[AppUpdate]` from `application.ini` the value is the empty string; nothing in the tree resolves it (the only JS reader is `testing/update`, deleted). |
| `browser/installer/removed-files.in` rule for `update-settings.ini` | Removes the stale file from old installs. | Harmless. |
| `other-licenses/bsdiff`, `modules/xz-embedded`, `third_party/zucchini` (vendored) | D21: vendored libraries are left alone. | Not built once the three DIRS blocks are gone. |
| `config/createprecomplete.py` and packager `precomplete` support | Generic packaging scripts, shared with l10n repackaging. | Static file written at package time. |
| Windows leftovers `toolkit/components/bitsdownload`, `toolkit/components/taskscheduler/rust`, `build/moz.configure` default-browser-agent, notification-server, push-helper options | Windows-only crates and options that D21 left in place; see Risks. | Compiled only for Windows targets. |

No locked pref is needed for the updater itself: with the code gone there is nothing to lock. `app.update.*` prefs that remain are the two timer prefs and `app.update.channel`; none starts a download or contacts a server.

## Risks and open questions

1. Configure-time rebuild: items 1 to 5 above force a full rebuild (40 minutes to 2 hours, per project notes). Do the C++ and front-end work in the same integration so the build runs once. The mozconfig line and the configure option must be removed in the same change.
2. `commonupdatedir.cpp`/`updatedefines.h` move: the build breaks if `toolkit/mozapps/update/common` is deleted before this is done. `updatedefines.h` includes `readstrings.h`. Verify `MultiInstanceLock.cpp` compiles with a trimmed header; it uses `NS_t*` macros only.
3. `BrowserContentHandler.sys.mjs` rewrite changes startup behavior for a new milestone. In M6E the override page is blank, so the visible effect is nil, but the file is large and central: keep the diff minimal and run `browser/components/tests/browser/browser_browserContentHandler*` if present.
4. ASRouter `needsUpdate` targeting: some ASRouter messages or tests may use `needsUpdate`. If any message file still references it, replace with a constant `false` instead of deleting the attribute.
5. `about-firefox.mjs`: besides the update group it defines `supportLinksGroup`, `supportGetHelp`, `supportShareIdeas`. Confirm which pane uses them before deleting the file; move them if needed. Also the Settings pane category "update-state" and its `LegacyPaneMappings` entry (`#updates` deep link) must go, and `acceptance.py` checks `"updates"` is absent.
6. Panel notification tests (`browser_panelUINotifications*.js`) depend on the update ids. Decide: rewrite on `addon-installed`, or delete. Sync/accounts (earlier) no longer provide a second sample id.
7. Glean: deleting the `background-update` ping requires editing `nimbus/metrics.yaml` (six lists) and `browser/components/metrics.yaml` in the same change, or `glean_parser` fails. Check `toolkit/components/crashes/metrics.yaml` (listed in `background_update_metrics`): only the list in `metrics_index.py` changes, not the yaml.
8. Out of scope but updater-adjacent, left alone: `tools/update-verify` (Cargo workspace member `tools/update-verify/marannon`, its removal needs a Cargo.lock edit), `taskcluster` update kinds and `config.yml` signing paths, `python/mozrelease` update-verify code, `testing/mozharness` update scripts, `taskcluster/gecko_taskgraph/transforms/update_integrity.py`. Remove in P6.2 or later if wanted.
9. Windows-only Rust that D21 left: `toolkit/components/bitsdownload` (`Bits.sys.mjs`, only consumer was `UpdateService`; gkrust feature `bitsdownload`, `AppConstants.MOZ_BITS_DOWNLOAD`) and `toolkit/components/taskscheduler/rust` (gkrust feature `backgroundtaskregistrar`, Cargo workspace member, `Cargo.lock`, `lib.rs`). Deleting them touches gkrust, `Cargo.toml`, `Cargo.lock` and `third_party/rust` workspace-hack features. Not compiled on macOS. Proposal: leave them for a separate Windows-leftover pass; this feature deletes only the JS side of taskscheduler. Decision needed if you want them in this branch (then `touchesCppOrRust` includes Rust).
10. `toolkit/xre/nsAppRunner.cpp` xattr `org.mozilla.update-telemetry` read and `isNewUpdate` lock-file field: dead without the updater but harmless; remove only if the lock-file JSON is not consumed elsewhere (`browser/components/` crash/telemetry readers).
11. `nsIUpdateTimerManager` naming and `app.update.*` timer prefs remain in the tree; a future rename is possible but not worth the churn.
12. `AppConstants.MOZ_UPDATER` removal makes `AppConstants.MOZ_UPDATER` `undefined`; any third-party code path left in `devtools/` or `remote/` testing that reads it would silently treat it as false, which is the current value anyway.
13. The report's search used `git grep` with case-sensitive identifier lists plus a case-insensitive pass over `browser/`, `toolkit/`, `services/`, `remote/`, `security/manager`, `tools/m6e`, `python/mozbuild`, `build`, `config`. The critic should additionally search for `"update"` string IDs in `browser/components/customizableui` and `browser/themes` after the edits, and for `Ci.nsIUpdate` in `.mjs` under `devtools/` (none found).

## Critic addendum

Read-only review at base cb002d4874. Searches used: every basename and directory name of the files to delete, contract IDs and interface names (`nsIUpdate*`, `nsIApplicationUpdateService`, `update-manager;1`), `app.update` as a fixed string across the tree, `MOZ_UPDATER` and sibling configure names, `mozapps/update` as a path, FTL id prefixes (`update-`, `intervention-update-`, `appmenu-update-`), CSS/element ids, test-manifest strings and mozinfo `updater` conditions, `tools/m6e`, policy names and `isAllowed()` feature names, Glean ping and category names. The analyst's core structure (what is deleted, what must stay, the configure plan) held up. The items below are what it missed or got wrong.

### Wrong or inaccurate claims

1. **Wrong file for the intervention strings.** The report says `intervention-update-*` strings are in `browser/locales/en-US/browser/urlbar.ftl`. That file does not exist. They are in `browser/components/urlbar/content/interventions.ftl` (packaged as `preview/interventions.ftl` via `browser/locales/jar.mn`, linked from `browser.xhtml`). Edit that file: remove `intervention-update-ask(-confirm)`, `-refresh(-confirm)`, `-restart(-confirm)`, `-web(-confirm)` and the section comments above them. Keep `intervention-clear-data*` and `intervention-refresh-profile*`. Note that `intervention-update-refresh*` is the "up to date, refresh profile" tip and `UrlbarProviderInterventions` uses it, so re-check that tip logic as the report already says.
2. **"Left alone: `testing/marionette/.../test_marionette.py`, probably pref names only" is wrong.** `testing/marionette/harness/marionette_harness/tests/unit/test_marionette.py` has `test_application_update_disabled`, which calls `Cc['@mozilla.org/updates/update-service;1'].getService(Ci.nsIApplicationUpdateService)`. It will throw once the service is gone. Remove that test.
3. **`integration-tests.toml` is not "updater mentions".** `testing/marionette/harness/marionette_harness/tests/integration-tests.toml` line 79 is `["include:../../../../../toolkit/mozapps/update/tests/marionette/manifest.toml"]`. It must be removed (a dangling include fails manifest parsing).
4. **`test_UpdateUtils_url.js` in `toolkit/modules/tests/xpcshell/xpcshell.toml`** is `run-if = ["updater"]` with reason "LOCALE is not defined without MOZ_UPDATER". It is skipped in the current build. The report says the test "stays". Decide: drop the `run-if` and see whether it passes (`default.locale` is still packaged and `LocaleService` reads it, so `UpdateUtils.getLocale` works), or delete the test. Removing the mozinfo key alone changes nothing (manifestparser treats `updater` as a known token and evaluates false).
5. **Risk 10 ("remove the xattr only if the lock-file JSON is not consumed elsewhere") has an answer: it is consumed elsewhere.** `org.mozilla.update-telemetry` is read by `toolkit/profile/ProfileMetrics.sys.mjs` (sets `Glean.profiles.daysSinceUpdate`; metric `profiles.days_since_update` in `toolkit/profile/metrics.yaml`; test `toolkit/profile/test/xpcshell/test_profile_metrics_time_since_update.js`) and by the downgrade ping (`isNewUpdate` in `nsAppRunner.cpp`, `toolkit/xre/test/gtest/TestDowngradePing.cpp` tests `IsNewUpdateTrue/False`, docs `toolkit/components/telemetry/docs/data/downgrade-ping.md`). Nothing writes the xattr after the updater is gone. Recommendation: keep the nsAppRunner read and the ProfileMetrics read as is (they just see "absent"), and do not touch the gtest. Removing them is a separate, optional cleanup that needs the metric, test, gtest and doc changes together.
6. **About Firefox pane is larger than the report says.** The `updates` group is only half of it. Also present: `CONFIG_PANES.about` in `browser/components/preferences/preferences.js` (`groupIds: ["updates", "support"]`, module `about-firefox.mjs`, `l10nId: "about-firefox-header"`), the nav button `#category-about-firefox` (`view="paneAbout"`, `data-l10n-id="pane-about-firefox-title"`) in `preferences.xhtml`, `<html:setting-group groupid="updates" ...>` in `main.inc.xhtml` (the report only lists the `<script>` line there), `main.js` line 67 (the `about-firefox.mjs` import) and `initSettingGroup("updates")`, `browser/components/preferences/jar.mn` line for `config/about-firefox.mjs`, FTL `about-firefox-header` and `pane-about-firefox-title` in `preferences.ftl`. Decision needed in the integration: remove the whole About pane (then `supportLinksGroup`, `supportGetHelp`, `supportShareIdeas` go with it and the pane, nav button, jar line and two FTL ids are deleted) or keep a pane with only the support group. Removing it also means `visible: srdSectionPrefs.all` logic goes. Also fix the comment in `config/privacy.mjs` (~line 2075) that names `about-firefox.mjs`'s `updateState`.

### Missed: build and manifests that would break

| File | Reference | Action |
|---|---|---|
| `tools/moz.build` | `with Files("update-packaging/**")` and `"update-packaging/test/python.toml"` in `PYTHON_UNITTEST_MANIFESTS` | remove both. The second one is a hard build error once `tools/update-packaging` is deleted. (`update-verify/**` Files() stays: left alone.) |
| `toolkit/components/moz.build` | `if CONFIG["MOZ_BACKGROUNDTASKS"] and OS_TARGET in ("WINNT","Darwin"): DIRS += ["taskscheduler"]` | remove the block. It is active on macOS, so leaving it after deleting the dir (other than `rust/`) breaks the build. If `taskscheduler/rust` is left, it keeps no moz.build entry, which is fine (reached only via Cargo path). |
| `toolkit/components/backgroundtasks/moz.build` | `TESTING_JS_MODULES.backgroundtasks` lists `tests/BackgroundTask_automaticrestart.sys.mjs`, `_policies`, `_shouldnotprocessupdates`, `_shouldprocessupdates`, `_update_sync_manager` | remove these 5 lines (report lists the files and `xpcshell.toml`, not this moz.build; missing files fail the build). |
| `toolkit/components/backgroundtasks/tests/BackgroundTask_targeting.sys.mjs` | `EXCLUDED_NAMES` has `"needsUpdate"` ("Queries Balrog update server") | drop that entry when the `needsUpdate` getter goes (harmless if left). |
| `devtools/client/framework/browser-toolbox/test/browser_browser_toolbox_shouldprocessupdates.js` and its entry in `devtools/client/framework/browser-toolbox/test/browser.toml` (line 49) | asserts `MOZ_TEST_SHOULD_NOT_PROCESS_UPDATES` is set to `"ShouldNotProcessUpdates(): DevToolsLaunching"` by the `nsAppRunner` block the report removes | delete the test and the manifest entry. Missed by the report. |
| `build/sparse-profiles/taskgraph` | `path:modules/libmar/`, `path:toolkit/mozapps/update/updater`, `path:tools/update-packaging/` (the report lists the file only generically) | remove those three lines (a sparse profile naming a missing path is harmless but stale; keep `tools/update-verify`). |
| `stylelint-rollouts.config.js` | `toolkit/themes/osx/mozapps/update/updates.css` (and the linux/windows siblings, which are outside this tree's macOS scope) | remove the osx entry; report lists eslint configs, `.prettierignore`, but not this file. |
| `mots.yaml` | besides the `toolkit/mozapps/update/**/*` glob, also `tools/update-packaging/**/*` (line ~1500) | remove the update-packaging glob too. |
| `tools/lint/test-manifest-toml.yml` (`testing/update/`), `tools/lint/license.yml` (`toolkit/mozapps/update/updater/crctable.h`) | stale paths | remove (the report mentions them under Tests; confirmed present). |
| `other-licenses/bsdiff/moz.build` | `LOCAL_INCLUDES` of `/toolkit/mozapps/update/updater` and `.../bspatch` | vendored, unbuilt after the DIRS removal. Leave (D21). Listed only so nobody greps it as a leftover. |
| `toolkit/content/licenses/moz.build` | `LICENSES["bspatch"]` entry (and `bspatch.txt`) shown in about:license | bspatch is no longer shipped once `toolkit.mozbuild` stops building it. Remove the entry (and check `xz-embedded`/`zucchini` entries in the same file for the same reason). Not in the report. |
| `python/mozbuild/mozbuild/action/l10n_repackage.py` | `"updater.ini"` in a file list | remove the entry together with `browser/locales/generate_ini.py`/`updater.ini`. (`test_l10n_manifest.py` uses `updater.ini` only as a fixture string; leave.) |
| `toolkit/themes/osx/mozapps/jar.mn` | `skin/classic/mozapps/update/updates.css` | in the report, confirmed. |

### Missed: code and tests

| File | Reference | Action |
|---|---|---|
| `browser/components/preferences/tests/browser_windows_launch_on_login.js`, `browser_windows_launch_on_login_msix.js` | `BackgroundUpdate: "resource://gre/modules/BackgroundUpdate.sys.mjs"` in lazy imports | remove the import (Windows tests, but the module no longer exists; check whether the name is actually used). |
| `browser/components/asrouter/docs/targeting-attributes.md` | `### needsUpdate` section | remove with the attribute |
| `browser/components/asrouter/tests/browser/browser_asrouter_targeting.js` | `check_needsUpdate` test (~line 742) | remove |
| `browser/components/asrouter/tests/unit/ASRouter.test.js` | `needsUpdate: {}` (~line 183) | remove |
| `browser/components/BrowserContentHandler.sys.mjs` | `Services.policies.isAllowed("postUpdateCustomPage")` inside `getPostUpdateOverridePage` (the report only names the BrowserGlue use) | goes with the function |
| `toolkit/modules/UpdateUtils.sys.mjs` | uses policies `app-auto-updates-off/on`, `app-background-update-off/on` | goes with the recommended trim; when the trim is skipped, these policy feature names are still read by nobody that sets them (policy handlers removed), so `isAllowed` returns true. Harmless. |
| `toolkit/components/telemetry/app/TelemetryUtils.sys.mjs` | `Preferences.UpdatePing: "toolkit.telemetry.updatePing.enabled"` | remove the constant (no reader once the update ping is gone) |
| `browser/app/profile/m6e.js` line 40 | `pref("toolkit.telemetry.updatePing.enabled", false, locked)` | the pref has no reader after the deletion; remove the line (it is an M6E file, so this is ours) or keep as a belt-and-braces lock. Recommend removing, to avoid a lock on a pref nothing reads. The report said m6e.js has no `app.update.*` lock; this one is a related lock it did not mention. |
| `toolkit/components/telemetry/tests/marionette/harness/telemetry_harness/fog_ping_filters.py` | `FOG_BACKGROUND_UPDATE_PING = FOGDocTypePingFilter("background-update")` (no users in tree) | remove |
| `toolkit/components/telemetry/tests/browser/browser_UpdatePingSuccess.js`, `.../tests/unit/test_ThirdPartyModulesPing.js` (sets `app.update.url`) | pref only | the first is already in the delete list; the second is a harmless pref write, leave |
| `toolkit/components/messaging-system/lib/MessagingSystemBlocklists.sys.mjs` (`"app.update."` blocked prefix) and `test_MessagingSystemBlocklists.js` / `test_MessagingSystemAllowlists.js` | block list entry for messages that set prefs | leave: it is a deny list, and the timer prefs still start with `app.update.`. |
| `toolkit/components/backgroundtasks/BackgroundTasksUtils.sys.mjs` `readFirefoxMessagingSystemTargetingSnapshot`, `BackgroundTask_message.sys.mjs` (`--targeting-snapshot`), and the `scheduleFirefoxMessagingSystemTargetingSnapshotting` call in BrowserGlue's `BackgroundUpdate` idle task | the snapshot writer is `BackgroundUpdate.sys.mjs` (deleted); readers remain | leave the readers (generic background-task code with their own tests); note they now have no writer. |
| `toolkit/components/backgroundtasks/tests/xpcshell/test_backgroundtask_policies.js` + `BackgroundTask_policies.sys.mjs` | the report says delete; the test uses `AppUpdateURL` only as the sample policy | alternative: keep the test and switch the sample policy to a surviving one (for example `Homepage` or any simple string policy) so policy-in-background-task coverage is not lost. Judgement call; deletion is acceptable. |
| `toolkit/modules/ScheduledTask.sys.mjs` | `maxLogLevelPref: "app.update.compulsory_restart_log"` | `ScheduledTask` stays (used by `ShellService`, `aiwindow/.../Monitor.sys.mjs`). Optionally change the log pref name; it is only a console log-level pref. |
| `browser/components/BrowserGlue.sys.mjs` `_checkForOldBuildUpdates` | calls `nsIApplicationUpdateService.checkForBackgroundUpdates` and reads `app.update.checkInstallTime.days` | in the report. Confirmed; this is the only staying reader of `app.update.checkInstallTime*` besides firefox.js, so remove the prefs together. |
| `browser/components/urlbar/tests/browser-tips/README.txt` | tells readers to run `mach test toolkit/mozapps/update/tests/browser` | in the report as "README.txt"; the whole `browser-tips/head.js` import of update helpers must be rewritten, and `browser_searchTips*.js` use `UpdRootD` to find the update status files. |
| `browser/components/shell/test/browser_createWindowsShortcut.js` | uses `UpdRootD` | Windows-only; leave (confirms `UpdRootD` must stay). |
| `docs` | `toolkit/components/backgroundtasks/docs/index.md` (lines ~3, 94-107, 141: "first consumer will be checking for updates", `IsUpdatingTaskName`, a Gantt row `updater`), `toolkit/components/glean/docs/user/instrumentation_tests.md` line 373 (link to `toolkit/mozapps/update/tests/unit_background_update/...`), `intl/l10n/docs/migrations/legacy.md` (example path, leave), `toolkit/components/telemetry/docs/obsolete/collection/update-ping.md` (obsolete doc; leave or delete with the ping), `docs/config.yml` mentions only `tools/update-verify` (stays) | edit the first two; leave the rest |
| `toolkit/xre/test/test_launch_without_hang.js` | comment citing `toolkit\mozapps\update\tests\unit_service_updater\xpcshellUtilsAUS.js` | comment only; leave |
| `tools/@types/generated/tspaths.json` | the report says `tools/@types/generated/*`; confirmed entries for `AppUpdater`, `BackgroundUpdate`, `UpdateListener`, `UpdateLog`, `UpdateService`, `UpdateTelemetry`, `BackgroundTask_backgroundupdate`, `TaskScheduler*` | regenerate or hand-edit; also `lib.gecko.xpcom.d.ts` |

### Verification of the report's "must stay" and "earlier feature" claims

- `UpdateUtils.sys.mjs` consumers: confirmed (GMPInstallManager, GMPUtils, OSKeyStore, URLFormatter, TelemetryUtils, ClientEnvironment, XPIInstall, UITour, ReinstallCheck, ASRouterTelemetry, Troubleshoot, `browser.js`). `Troubleshoot.sys.mjs` lines 241-245 use `UpdateUtils.UpdateChannel` only inside the `if (AppConstants.MOZ_UPDATER)` block, so that import goes with the block (verified); the claim that UpdateUtils stays is still correct.
- Timer manager: confirmed users `Region`, `SearchService`, `PageThumbs`, `UntrustedModulesPing`, `XPIProvider`, `ContentRelevancyManager`, `RemoteSettingsExperimentLoader`, `SuggestBackendRust`. The report's list is complete for these; add `toolkit/components/nimbus/test/browser/browser_remotesettingsexperimentloader_remote_defaults.js` and several tests that read `app.update.lastUpdateTime.*` (all stay).
- `commonupdatedir`/`updatedefines.h` must be moved: confirmed. Users are `nsXREDirProvider.cpp` (two includes, one under `#  include` on Windows), `MultiInstanceLock.cpp` (`commonupdatedir.h` and `updatedefines.h`), `LauncherRegistryInfo.cpp` (Windows). Nothing else in `browser/`, `toolkit/` or `ipc/` includes the other files in `toolkit/mozapps/update/common/` (`EnterprisePolicies*`, `pathhash`, `readstrings`, `updatehelper`, ...) so deleting the rest is safe on macOS.
- `modules/libmar` has no first-party users outside `toolkit/toolkit.mozbuild`, `tools/update-*`, `build/sparse-profiles/taskgraph`: confirmed.
- `UpdRootD` callers: confirmed `TelemetryStorage`, `BrowserUsageTelemetry`, `UpdateUtils` and tests.
- `default.locale`: stays (`LocaleService.cpp`, `package-manifest.in`); not an updater artifact. The `#ifdef MOZ_UPDATER` assert in `LocaleService.cpp` is the only coupling.
- `acceptance.py` `REMOVED_CONSTANTS["Updater"] = "MOZ_UPDATER"`: the check is `not c["value"]`, so an `undefined` value passes. Open question 1 in the report ("confirm how the check treats undefined") is answered: no change needed. `"updates"` in `REMOVED_SETTINGS_GROUPS` still passes once the group is gone.
- Latent `BrowserContentHandler` bug (`lazy.UpdateManager` in `OVERRIDE_NEW_MSTONE`/`OVERRIDE_NEW_BUILD_ID`, wrapped in `try {} catch (ex) {}` at ~line 1062): confirmed.
- "No references from earlier features": the grep over `aiwindow`, `ipprotection`, `sync`, `fxaccounts` etc. for updater identifiers found none. Additional earlier-feature hits to ignore: `browser/components/AccountsGlue.sys.mjs` sends `fxa-needs-authentication` through `AppMenuNotifications` (fxaccounts, feature 6, already gone).
- "Nimbus is feature 9, handled here": confirmed. Nimbus-side references: `toolkit/components/nimbus/metrics.yaml` `background-update` in `send_in_pings` lists, `FeatureManifest.yaml` entries (`backgroundUpdateAutomaticRestart` ~line 3034, `backgroundUpdateCheckPolicy` ~3056-3070, `backgroundUpdate` ~4916), and the Nimbus WNP code in `BrowserContentHandler` plus `browser/components/tests/browser/whats_new_page/browser_whats_new_page_nimbus*.js`. No other Nimbus file (`ExperimentAPI`, `ExperimentStore`, `test_ExperimentManager_lifecycle.js`) references the updater.
- Crash reporter (feature 8, still present): `toolkit/components/crashes/metrics.yaml` appears in `background_update_metrics` index lists in `metrics_index.py`; only the index changes. `toolkit/crashreporter/crashping/src/lib.rs` hits are add-on `backgroundUpdateCheck` strings in a test fixture; unrelated.

### Items checked and found NOT to need action

`remote/` (only `RecommendedPreferences.sys.mjs` `app.update.disabledForTesting`, already listed), `devtools/` (no updater identifiers beyond the browser-toolbox test above), `services/settings` (the `app.update.*` strings are in `dumps/blocklists/addons.json`, data), `security/manager`, `testing/profiles/*`, `geckodriver`, `tps`, `raptor` (pref writes only), `testing/mochitest/ignorePrefs.json` (pref names), `browser/extensions/newtab/lib/PrefsFeed.sys.mjs` and `toolkit/components/glean/src/init/mod.rs` and `TelemetryControllerBase.sys.mjs` (read `app.update.channel`, stays), `toolkit/crashreporter/mozwer-rust/lib.rs` (reads `app.update.channel`, stays), Windows-only `python/mozbuild/.../nsis.py`, `test_nsis.py`, `test_nsis_stage.py` (`maintenanceservice_installer.nsi`), `build/rust/windows/Cargo.toml` (`Win32_System_TaskScheduler` feature of the windows crate).
