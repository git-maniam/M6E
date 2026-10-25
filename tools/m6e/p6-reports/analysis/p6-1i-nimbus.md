# P6.1i Nimbus experiments: analysis

Feature key `p6-1i-nimbus`, integration order 9 (last). Base commit cb002d4874. Read-only analysis; nothing was built or run.

## 1. Summary

Nimbus cannot be deleted outright. 77 staying files import `resource://nimbus/ExperimentAPI.sys.mjs` (urlbar, newtab, ASRouter, Messaging System, about:welcome, preferences, search, DoH, DAP, content relevancy, shell, telemetry, PiP, pdf.js, backup, ...), and 3 C++ files include `mozilla/browser/NimbusFeatures.h`. Most of them use Nimbus only as a pref reader: `NimbusFeatures.<id>.getVariable(...)` returns an enrolled value if there is one, otherwise the value of the variable's `fallbackPref`. M6E cannot enroll anything in practice (the `nimbus-desktop-experiments` collection is blocked by the Remote Settings allowlist and the rollouts and studies prefs are locked off; see section 9), so every one of those calls already returns the pref value.

Recommended approach: hollow out Nimbus and keep a stub.

1. Delete the whole enrolment machinery: ExperimentManager, ExperimentStore, Enrollments, RemoteSettingsExperimentLoader, SharedDataMap, Migrations, PrefFlipsFeature, TargetingContextRecorder, Telemetry, FirefoxLabs, NimbusFeatures.cpp/.h, the 6571-line `FeatureManifest.yaml` and its generator, the schemas, all Nimbus Glean metrics and pings, all docs and all tests (85 tracked files, about 25,000 lines). One small new xpcshell test for the stub is added (section 8).
2. Keep `toolkit/components/nimbus/ExperimentAPI.sys.mjs`, rewritten as a stub of about 120 lines, and `jar.mn` with one line. The stub exports `ExperimentAPI`, `NimbusFeatures` and `EnrollmentType`. It holds a hand-written map of the 103 `fallbackPref` variables (only about 20 of the 214 manifest features have any). Every `NimbusFeatures.<id>` returns an object where:
   - `getVariable` / `getAllVariables` read the fallback pref (with `defaultValues` merged in, as today);
   - `getEnrollmentMetadata` returns null; `getAllEnrollments` and `getAllEnrollmentMetadata` return `[]`;
   - `recordExposureEvent` does nothing; `ready()` resolves at once;
   - `onUpdate` / `offUpdate` fire on changes to the fallback prefs only (what `_ExperimentFeature` does today for `pref-updated`).
   `ExperimentAPI.enabled`, `labsEnabled`, `rolloutsEnabled` and `studiesEnabled` are constant false; `init()`, `ready()`, `updateRecipesForFeatures()` resolve immediately. There is no `manager`, `_rsLoader` or `profileId`; every caller of those is edited (section 3, group E).
3. This keeps all `resource://nimbus/ExperimentAPI.sys.mjs` imports, the ~250 `NimbusFeatures` call sites, ASRouter, urlbar and newtab untouched, so the D21 rule (small first-party edits) holds and the diff stays reviewable. About 40 staying files need real edits (group E, about 25 of them non-test), all listed below.
4. C++ side: delete `NimbusFeatures.cpp/.h` and `dom/chrome-webidl/Nimbus.webidl`; three one-line C++ edits (AboutRedirector, HttpBaseChannel, BounceTrackingProtection) plus the `Preferences.cpp` allowlist entry. This needs `./mach build`, not `build faster`. No configure-time change.

Doing the full removal instead (rewriting about 250 call sites to direct pref reads across urlbar, newtab, ASRouter and others) is possible later, but it is a large behaviour-risk change for no gain in unreachability and is not recommended in this step.

Net result after the step: no code can enroll, fetch recipes, store enrolments, record Nimbus telemetry or set prefs from a recipe. The `nimbus-desktop-experiments` collection is also blocked by the M6E Remote Settings allowlist, which stays.

## 2. Files to delete

Directories and files under `toolkit/components/nimbus/` (git tracked count 85; `generate/__pycache__` is untracked):

- `lib/` (all 11 files): `Enrollments.sys.mjs`, `ExperimentManager.sys.mjs`, `ExperimentStore.sys.mjs`, `Migrations.sys.mjs`, `NimbusFeatures.cpp`, `NimbusFeatures.h`, `PrefFlipsFeature.sys.mjs`, `RemoteSettingsExperimentLoader.sys.mjs`, `SharedDataMap.sys.mjs`, `TargetingContextRecorder.sys.mjs`, `Telemetry.sys.mjs`
- `generate/` (`generate_feature_manifest.py`)
- `schemas/` (all 8 files: the JSON schemas, `LICENSE`, `moz.yaml`)
- `docs/` (`index.md`)
- `test/` (everything now there: `NimbusTestUtils.sys.mjs`, `unit/` 29 files, `unit/shutdown/` 9, `browser/` 11, `python/` 3, `gtest/` 3); then add back only `test/unit/xpcshell.toml` and `test/unit/test_stub.js` for the stub
- `FeatureManifest.yaml`, `FirefoxLabs.sys.mjs`, `metrics.yaml`, `metrics-normandy.yaml`, `pings.yaml`

Kept and rewritten: `ExperimentAPI.sys.mjs` (stub), `jar.mn` (one line: `res/nimbus/ExperimentAPI.sys.mjs`, keep the `% resource nimbus %res/nimbus/` registration), `moz.build` (see section 6).

Other files to delete outside the directory:

| Path | Why |
|---|---|
| `dom/chrome-webidl/Nimbus.webidl` | Only `NimbusFeatures.cpp` includes the generated `NimbusBinding.h`. Also drop the `Files("Nimbus.webidl")` block and the `"Nimbus.webidl"` entry in `dom/chrome-webidl/moz.build`. |
| `browser/components/preferences/config/firefoxLabs.mjs` | Firefox Labs pane; its only data source is Nimbus opt-in recipes. |
| `browser/components/preferences/tests/experimental_features/` (whole directory) | Tests for the Labs pane. Remove its `include`/manifest entry from the parent preferences test manifest. |
| `toolkit/locales/en-US/toolkit/firefoxlabs/features.ftl` | Strings for Labs opt-in recipes (loaded only by `preferences.xhtml`). Check the toolkit locales `jar.mn`/`l10n` listing, if any. |
| `browser/themes/shared/icons/labs-16.svg` plus its line in `browser/themes/shared/jar.inc.mn` | Only the Labs pane/nav button uses it (`preferences.js`, `preferences.xhtml`). Verify with `git grep labs-16` before deleting. |
| `toolkit/components/backgroundtasks/BackgroundTask_message.sys.mjs` | The `message` background task exists only to fetch Nimbus `messaging-experiments` recipes (`_rsLoader.remoteSettingsClients`). Also remove its entry in `toolkit/components/backgroundtasks/moz.build`. |
| `toolkit/components/backgroundtasks/tests/xpcshell/test_backgroundtask_experiments.js`, `experiment.json` | Tests for that task (remove from `xpcshell.toml`). |
| `tools/@types/subs/FeatureManifest.sys.d.mts` | Type stub for the deleted generated module (also adjust `tools/ts/mach_commands.py`). |

Nimbus-only test files in staying components (delete, and remove from their manifests) are in section 7.

## 3. References from staying code

Legend: S = the stub covers it, no edit needed. E = needs an edit. O = optional dead-code cleanup (not required for the build or tests to pass).

### 3.1 Group E: edits required

| File | What | Action |
|---|---|---|
| `browser/components/about/AboutRedirector.cpp` | `#include "mozilla/browser/NimbusFeatures.h"`; in the `welcome` branch `NimbusFeatures::RecordExposureEvent("aboutwelcome"_ns, true)` and `NimbusFeatures::GetBool("aboutwelcome"_ns, "enabled"_ns, true)` | Remove the include and the exposure call; replace the `GetBool` with `mozilla::Preferences::GetBool("browser.aboutwelcome.enabled", true)` (the fallback pref of `aboutwelcome.enabled`; `Preferences` is already used in the file). C++. |
| `netwerk/protocol/http/HttpBaseChannel.cpp` | include (line 39); `NimbusFeatures::RecordExposureEvent("opaqueResponseBlocking"_ns, true)` at the top of `BlockOrFilterOpaqueResponse` | Remove both. C++. |
| `toolkit/components/antitracking/bouncetrackingprotection/BounceTrackingProtection.cpp` | include (line 48); the comment block and `(void)NimbusFeatures::RecordExposureEvent("bounceTrackingProtection"_ns, false)` in the purge-result handler | Remove include, comment and call. C++. |
| `modules/libpref/Preferences.cpp` | `PREF_LIST_ENTRY("nimbus.syncdatastore.")` in the content-process shared-pref allowlist (line ~7258) | Remove the entry. C++. Also check the comment at line ~1334 ("pref was not defined before Nimbus set it") and leave it. |
| `toolkit/modules/FirstStartup.sys.mjs` | Lines ~137-160: `ExperimentAPI.getAndClearFirstStartupTimestamps()` and four `Glean.firstStartup.nimbus*` sets | Remove the block and the `ExperimentAPI` lazy import. |
| `toolkit/modules/metrics.yaml` | `nimbus_store_init_time` (with the `&nimbus_startup_metric` anchor), `nimbus_manager_init_time`, `nimbus_loader_init_time`, `nimbus_init_time` in `first_startup` | Delete the four metrics. |
| `toolkit/modules/Troubleshoot.sys.mjs` | The `normandy` snapshot provider (already a no-op because it returns early without `MOZ_NORMANDY`) reads `ExperimentAPI.manager.store` | Delete the whole provider. `ReportBrokenSiteParent.#getActiveExperiments` already returns `[]` when `troubleshootingInfo.normandy` is missing, so it needs no edit (optional: drop it and the `experiments` field). |
| `toolkit/content/aboutSupport.js` | `normandy(data)` handler filling `remote-features-tbody` / `remote-experiments-tbody` | Delete the handler. |
| `toolkit/content/aboutSupport.xhtml` | `remote-features` section (line ~311) and the `#ifdef MOZ_NORMANDY` experiments section (lines ~597-618) | Delete both sections. |
| `toolkit/locales/en-US/toolkit/about/aboutSupport.ftl` | `support-remote-features-*` (3), `support-remote-experiments-*` (4) strings | Delete. |
| `browser/components/backup/resources/PreferencesBackupResource.sys.mjs` | `addPrefsToIgnoreInBackup` skips all `nimbus.*` prefs; `backup()` wraps `Services.prefs.backupPrefFile` in `ExperimentAPI._rsLoader.withUpdateLock(...)` and passes `ExperimentAPI.manager.store.getOriginalPrefValuesForAllActiveEnrollments()` | Drop the `nimbus.` loop and `kNimbus*` constants; call `backupPrefFile(prefsDestFile, PreferencesBackupResource.addPrefsToIgnoreInBackup({}))` directly (check the parameter of `addPrefsToIgnoreInBackup`, it takes the original-values map); drop the `ExperimentAPI` lazy import. Also drop `"nimbus"` wording from `browser/components/backup/metrics.yaml` (lines ~706, ~721, docs only). |
| `browser/components/backup/BackupService.sys.mjs` | `NimbusFeatures.backupService` killswitch reads and `onUpdate`/`offUpdate` at ~4866/4891 | S (stub: `getVariable` returns undefined, `onUpdate` is pref-driven). O: remove the `archiveKillswitch`/`restoreKillswitch` checks and the `internalReason: "nimbus"` branches. |
| `browser/components/profiles/SelectableProfileService.sys.mjs` | Profile deletion runs `DELETE FROM NimbusEnrollments ...` and `DELETE FROM NimbusSyncTimestamps ...` using `ExperimentAPI.profileId` (~1786-1800) | Delete both statements and the lazy import (no `profileId` in the stub). Leave the tables in `toolkit/profile/ProfilesDatastoreService.sys.mjs` (shared DB schema, see section 8). |
| `toolkit/components/messaging-system/lib/SpecialMessageActions.sys.mjs` | `submitOnboardingOptOutPing` calls `ExperimentAPI.manager.createTargetingContext()` and sets `Glean.onboardingOptOut.activeExperiments/activeRollouts/enrollmentsMap` | Remove those four statements (keep the ping submit), remove the `ExperimentAPI` import. In `toolkit/components/telemetry/metrics.yaml` delete the three `onboarding_opt_out` metrics that only this code sets and, in the same file, the `'nimbus-targeting-context'` entry of the `&client_id_pings` list (line ~51) and the comment at ~1147. Check tests under `toolkit/components/telemetry/tests` for `onboarding_opt_out`. |
| `browser/components/defaultlaunchonlogin/DefaultLaunchOnLogin.sys.mjs` | `waitForNimbusReady()` calls `ExperimentAPI.init()` and `_rsLoader.finishedUpdating()` and the caller awaits it (~93-120) | Remove the method and its `await`; keep the `NimbusFeatures` feature read if present (S). |
| `toolkit/components/backgroundtasks/BackgroundTasksUtils.sys.mjs` | `enableNimbus()` (~299-330) initialises `ExperimentAPI` and sets `nimbus.debug` | Delete the function. Its only other caller is `BackgroundTask_backgroundupdate.sys.mjs` (updater, earlier feature). |
| `toolkit/components/backgroundtasks/defaults/backgroundtasks_browser.js` | `nimbus.appId` pref (line 47) and the `messaging-experiments` provider pref (lines ~22-23) and `app.normandy.run_interval_seconds` (43, Normandy leftover) | Delete the `nimbus.appId` and `messaging-experiments` lines. Leave the Normandy line (not this feature). |
| `toolkit/components/backgroundtasks/BackgroundTasks.cpp` | `aName.EqualsLiteral("message")` in the dev-only allowlist (line ~256) | Remove the clause. C++. |
| `browser/components/preferences/preferences.js` | `experimental` pane entry (`firefoxLabsFeatures`, `labs-16.svg`, `firefoxLabs.mjs`, lines ~336-341); `ExperimentAPI.labsEnabled` block restoring the nav button (~599-609) | Delete both; remove the `ExperimentAPI` import if unused. Also `NimbusFeatures.moreFromMozilla` reads at ~496/657-661: S. |
| `browser/components/preferences/preferences.xhtml` | `<link ... toolkit/firefoxlabs/features.ftl>` (line 50) and `moz-page-nav-button id="category-experimental"` (lines ~188-192) | Delete both. |
| `browser/components/preferences/jar.mn` | `content/browser/preferences/config/firefoxLabs.mjs` (line 37) | Delete the line. |
| `browser/components/preferences/config/permissions-data.mjs` | `{ id: "nimbus.rollouts.enabled", type: "bool" }` (line 70), the `enableNimbusRollouts` setting (~446-455) and its use in a group (~845) | Delete all three. |
| `browser/locales/en-US/browser/preferences/preferences.ftl` | `nimbus-rollouts` (~2238), `settings-pane-labs-title2`, `settings-pane-labs-header` (~103-107) | Delete. |
| `browser/locales/en-US/browser/browser.ftl` | `quickactions-labs`, `quickactions-cmd-labs` | Delete. |
| `browser/components/urlbar/QuickActionsLoaderDefault.sys.mjs` | `labs` quick action with `isUnsupported: () => !lazy.ExperimentAPI.labsEnabled` and `onPick: about:preferences#experimental` (~337-342) | Delete the `labs` entry and the `ExperimentAPI` lazy import. |
| `toolkit/locales/en-US/toolkit/branding/brandings.ftl` | `-firefoxlabs-brand-name` term | Delete once the strings above are gone (verify with `git grep firefoxlabs-brand-name`). |
| `browser/app/profile/firefox.js` | See section 5 for the pref list | Edit. |
| `browser/app/profile/m6e.js` | `pref("nimbus.rollouts.enabled", false, locked)` and its comment (lines 375-376) | Delete: nothing reads it any more. Keep `app.shield.optoutstudies.enabled` (line 58), see section 8. |
| `browser/components/enterprisepolicies/Policies.sys.mjs` | `DisableRemoteImprovements` policy only calls `manager.disallowFeature("NimbusRollouts")` (~1392-1399); `UserMessaging` handler has `FirefoxLabs` branch (~3926) | Delete the `DisableRemoteImprovements` handler and the `FirefoxLabs` branch. |
| `browser/components/enterprisepolicies/schemas/policies-schema.json` | `DisableRemoteImprovements` (line ~1534) and `UserMessaging.FirefoxLabs` (properties and the example, ~4235-4260) | Delete. Check `browser/components/enterprisepolicies/tests` (xpcshell is in `run_tests.sh`) for both names; none found by grep in `tests/*/*.js`. Also check enterprise policy docs under `browser/components/enterprisepolicies/docs` if present. |
| `toolkit/components/telemetry/app/TelemetryEnvironment.sys.mjs` | `["nimbus.debug", { what: RECORD_PREF_VALUE }]` (line ~370) | Delete the entry. Check `toolkit/components/telemetry/docs/data/environment.md` and the unit test expecting it. |
| `toolkit/components/telemetry/Events.yaml` | The whole `normandy:` events category (lines ~112-386; objects include `nimbus_experiment`) | Delete the category. Its only emitter was `lib/Telemetry.sys.mjs`. Verify with `git grep '"normandy"' toolkit/components/telemetry` and telemetry unit tests for event definitions. |
| `toolkit/components/glean/metrics_index.py` | `toolkit/components/nimbus/metrics.yaml` and `metrics-normandy.yaml` (lines 157-158), `toolkit/components/nimbus/pings.yaml` (lines 235 and 254) | Delete the four entries. The list order is checked by `toolkit/components/glean/tests/pytest/test_yaml_indices.py`. |
| `toolkit/components/glean/tags.yaml` | `'Firefox :: Nimbus Desktop Client'` | Leave (unused tag is harmless; the Normandy tag was left in P4.7b). O. |
| `tools/m6e/run_tests.sh` | `run nimbus toolkit/components/nimbus/test/unit` (line 33) | Keep the line: it now runs the new stub test. |
| `tools/m6e/acceptance.py` | `"Nimbus experiments": {"nimbus.rollouts.enabled": False}` (line 185); `"firefoxLabsFeatures"` in `REMOVED_SETTINGS_GROUPS` (still true, keep) | Replace the pref row with a check that `resource://nimbus/lib/ExperimentManager.sys.mjs` cannot be imported (and, if easy, that `ExperimentAPI.enabled` is false). Check how the "15 of 15" total is counted before changing rows. |
| `testing/profiles/xpcshell/user.js` | `nimbus.profilesdatastoreservice.*` prefs and comments (lines ~64-78) | Delete. |
| `testing/profiles/perf/user.js` | `nimbus.rollouts.enabled` (line 108) | Delete (leave `app.shield.optoutstudies.enabled`). |
| `.prettierignore`, `eslint-rollouts.config.mjs`, `tools/rewriting/Generated.txt` | Entries for `toolkit/components/nimbus/schemas/*.json` and `toolkit/components/nimbus/**` (prettierignore 1796-1798, eslint-rollouts 283 and 433, Generated.txt 232-234) | Delete those entries (only paths this step deletes; the `browser/components/nimbus/**` stale entries belong to P6.2). |
| `toolkit/docs/index.md` | `components/nimbus/docs/index` (line 20) | Delete the line. |
| `browser/docs/BrowserStartup.md` | Paragraphs on Nimbus early-startup (lines ~62-126) | Edit or trim; docs only, not build-relevant. |
| `browser/base/content/test/browser-static/browser_all_files_referenced.js` | "Nimbus schemas are referenced programmatically" allowlist entries | Remove the entries for `resource://nimbus/schemas/...`. |

### 3.2 Group S: covered by the stub, no edit needed

All of these import `resource://nimbus/ExperimentAPI.sys.mjs` and use only `NimbusFeatures.<id>` methods that the stub implements (`getVariable`, `getAllVariables`, `getEnrollmentMetadata`, `getAllEnrollments`, `recordExposureEvent`, `onUpdate`, `offUpdate`, `ready`) or `ExperimentAPI.enabled/ready/init/updateRecipesForFeatures/getAllBranches`. Full file list is in Appendix A. The important ones and their dead branches:

- `browser/components/asrouter/modules/ASRouter.sys.mjs`: `messaging-experiments` provider returns `[]` (`getAllEnrollments()` is empty); `nimbus:enrollments-updated` observer never fires; `ExperimentAPI.getAllBranches` is only called per enrolment. `ASRouterTargeting.sys.mjs`: `nimbusExperimentsLoaded` uses `ExperimentAPI.enabled` (false: "loaded"); `_rsLoader?._hasUpdatedOnce` is only reached when enabled; `updateRecipesForFeatures` resolves immediately. `ASRouterTriggerListeners` `nimbusUpdate` trigger and `TriggerActionSchemas` entry: never fire (O: remove, plus `browser/components/asrouter/tests/browser/browser_trigger_nimbusUpdate.js`).
- `browser/components/aboutwelcome/actors/AboutWelcomeParent.sys.mjs`: `shouldGateNimbusForAboutWelcome()` returns false when `ExperimentAPI.enabled` is false, so `ExperimentAPI.init()` and `_rsLoader` are never reached. About:welcome is also disabled (`browser.aboutwelcome.enabled` locked false in `m6e.js`). O: remove `AWPage:WAIT_FOR_NIMBUS`, `AWWaitForNimbus`, `spotlight.js`, `aboutwelcome.jsx`, `MultiStageProtonScreen.jsx` and the prebuilt bundles (`aboutwelcome.bundle.js`, `asrouter-newtab-multistage.bundle.js`), which would need regenerating; skip.
- `browser/components/newtab/AboutNewTabResourceMapping.sys.mjs`: train-hop add-on logic reads `newtabTrainhopAddonDeployment` / `newtabTrainhopFirstStartup` features, which return no values. O.
- `browser/extensions/newtab/lib/*` (`ActivityStream`, `PrefsFeed`, `TopSitesFeed`, `DiscoveryStreamFeed`, `TelemetryFeed`), `browser/modules/AboutNewTab.sys.mjs`: S (`getAllEnrollments` empty, `ready()` resolves).
- `browser/components/urlbar/*` (`UrlbarPrefs`, `QuickSuggest`, `AmpSuggestions`, `UrlbarProviderSemanticHistorySearch`, ...): `UrlbarPrefs._nimbus` calls `NimbusFeatures.urlbar.getAllVariables({ defaultValues: NIMBUS_DEFAULTS })`, which the stub keeps returning fallback prefs plus defaults. The `onNimbusChanged` observer hook keeps working through the pref-driven `onUpdate`.
- `toolkit/components/telemetry/app/TelemetryReportingPolicy.sys.mjs` (`preonboarding` feature fallback prefs), `TelemetryControllerParent`, `TelemetrySend`, `TelemetrySession` (`legacyTelemetry`, `pingsender`), `StartupTelemetry.sys.mjs` (`glean`, `gleanInternalSdk` `onUpdate` / `getAllEnrollments`): S.
- `toolkit/components/search/ConfigSearchEngine.sys.mjs` (`searchConfiguration.ready().then(...)`), `SERPCategorization.sys.mjs`, `SearchUIUtils.sys.mjs`, `toolkit/components/doh/DoHController.sys.mjs`, `toolkit/components/dap/*`, `toolkit/components/contentrelevancy/ContentRelevancyManager.sys.mjs`, `toolkit/actors/PictureInPictureChild.sys.mjs`, `toolkit/components/pdfjs/content/PdfJsParent.sys.mjs`, `toolkit/components/passwordmgr/LoginManagerParent.sys.mjs`, `toolkit/mozapps/handling/ContentDispatchChooser.sys.mjs`, `uriloader/exthandler/ExtHandlerService.sys.mjs`, `browser/components/shell/ShellService.sys.mjs`, `browser/components/protocolhandler/WebProtocolHandlerRegistrar.sys.mjs`, `browser/components/screenshots/ScreenshotsUtils.sys.mjs`, `browser/components/sidebar/SidebarManager.sys.mjs`, `browser/components/customizableui/CustomizableWidgets.sys.mjs`, `browser/components/sessionstore/SessionStore.sys.mjs`, `browser/components/BrowserGlue.sys.mjs`, `BrowserContentHandler.sys.mjs`, `browser/modules/{BrowserDOMWindow,BrowserUsageTelemetry,PermissionUI}.sys.mjs`, `browser/actors/{AboutNewTabChild,AboutPrivateBrowsingChild}.sys.mjs`, `browser/base/content/{browser.js,browser-siteProtections.js,browser-trustPanel.js,nsContextMenu.sys.mjs}`, `browser/components/preferences/{main.js,config/moreFromMozilla.mjs,config/search.mjs,config/privacy.mjs}` (use `window.NimbusFeatures`, set in `browser.js`): S.
- `toolkit/components/asrouter` helpers `ToastNotification.sys.mjs`, `BookmarksBarButton.sys.mjs`, `PreonboardingSplash.sys.mjs`, `ASRouterTelemetry.sys.mjs`: S.

### 3.3 Optional cleanup of dead Nimbus-only paths (not needed to pass build or tests)

- `BrowserContentHandler.sys.mjs` "What's New" Nimbus override (`startup.homepage_override_url_nimbus`, `..._nimbus_maxVersion/minVersion/disable_wnp`, `NimbusFeatures.whatsNewPage`; prefs in `firefox.js` ~3590-3595). `browser.startup.homepage_override.mstone` is locked to `ignore`, so this never runs.
- `browser/components/newtab` / `browser/extensions/newtab` `nimbusDebug` display in `DiscoveryStreamAdmin.jsx`, `PrefsFeed.sys.mjs` (`nimbus.debug` read with default false), and `mach_commands.py` train-hop recipe generator (dev tool).
- `browser/components/asrouter/bin/import-rollouts.js`, `yamscripts.yml` entry, `tests/NimbusRolloutMessageProvider.sys.mjs` (dev tool and test fixture).
- `browser/components/messagepreview/actors/AboutMessagePreviewParent.sys.mjs` comment on nimbus-devtools.

## 4. References from earlier features (ignore)

These live in code that is deleted before this feature is integrated. If the file survives in the earlier branch, the stub covers it anyway.

- VPN: `toolkit/components/ipprotection/IPPNimbusHelper.sys.mjs`, `IPProtectionActivator.sys.mjs`, `IPProtectionService.sys.mjs`, `moz.build`, `docs/*`; `browser/components/ipprotection/*` (tests too).
- Translations: `fullPageTranslation*`, `selectTranslation`, `translationsQuickAction` manifest features only.
- GenAI / AI window: `browser/components/genai/{GenAI,LinkPreview}.sys.mjs`, `browser/components/aiwindow/**` (`ai-window.mjs`, `firstrun.js`, `AIWindow.sys.mjs`, `AutoTabGroupingSuggestions`), `chatbot`, `linkPreviews`, `smartWindow` features, `browser/components/tabbrowser/SmartTabGrouping.sys.mjs` and `browser-allTabsMenu.js` (if in that branch).
- ML: `toolkit/components/ml/content/nlp/EmbeddingsGenerator.sys.mjs`, `toolkit/components/formautofill/shared/FormAutofillML.sys.mjs` (if deleted there), `browser/extensions/formautofill/test/browser/browser_ml_nimbus.js`.
- Sync / Mozilla accounts: `browser/base/content/browser-sync.js` (`fxaAppMenuItem`, `fxaAvatarMenuItem`, `fxaButtonVisibility`, `expandSignInButton`), `services/fxaccounts/FxAccountsCommands.sys.mjs` (`remoteTabManagement`), their tests (`browser/base/content/test/browser-sync`, `services/fxaccounts/tests/xpcshell/test_commands_closetab.js`), `fxaClientAssociation`.
- Updater: `toolkit/mozapps/update/{BackgroundUpdate,BackgroundTask_backgroundupdate,UpdateListener}.sys.mjs` and `tests/unit_background_update/*` (these also use `BackgroundTasksUtils.enableNimbus`, which this branch deletes).
- Crash reporter: `toolkit/crashreporter/CrashAnnotations.yaml` `NimbusEnrollments` entry, `toolkit/crashreporter/crashping/generated_metrics.yaml` `nimbus_enrollments`, `crashping/src/lib.rs` test `test_nimbus_enrollments`. If any of these files survives the crash reporter step, delete the `NimbusEnrollments` annotation here too (the stub no longer sets it).

## 5. Prefs, metrics and strings to remove

Prefs in `browser/app/profile/firefox.js`:

- Delete (lines ~2112-2146): `messaging-system.rsexperimentloader.collection_id`, `nimbus.debug`, `nimbus.validation.enabled`, `nimbus.firstUpdateComplete`, `nimbus.profilesdatastoreservice.enabled`, `.read.enabled`, `.sync.enabled`, `nimbus.telemetry.targetingContextEnabled` (both preprocessor branches), `nimbus.rollouts.enabled`, `nimbus.qa.pref-1`, `nimbus.qa.pref-2`, and the "Experiment Manager" comment block. Keep `messaging-system.log` and `messaging-system.profile.singleProfileMessaging.disable`.
- Delete: `services.sync.prefs.sync.nimbus.rollouts.enabled` (line ~1709) unless Sync already removed it.
- Delete: `browser.preferences.experimental.hidden` (line ~1411) after the Labs pane is gone.
- Optional: the `browser.aboutwelcome.experimentsGate.*` prefs (lines ~2091-2101) and `startup.homepage_override_*nimbus*` (~3590-3595), only if the O cleanups above are done.
- Keep: `app.shield.optoutstudies.enabled` and its `m6e.js` lock (still read by `PreferencesBackupResource`, `permissions-data.mjs`, `ASRouterTriggerListeners`, Policies and the Normandy remnants), `app.normandy.*` (not this feature).

Other prefs: `nimbus.profileId` (written by the old ExperimentAPI constructor; stale values in existing profiles are harmless).

Glean (all in `toolkit/components/nimbus`, removed with the directory): categories `nimbus_targeting_environment`, `nimbus_targeting_context`, `nimbus_events`, `normandy`, `heartbeat`; pings `nimbus-targeting-context` and `heartbeat`; `toolkit/modules/metrics.yaml` `first_startup.nimbus_*` (4); `toolkit/components/telemetry/metrics.yaml` `onboarding_opt_out.{active_experiments,active_rollouts,enrollments_map}` and the ping-list entry. No code outside the directory and the files above uses `Glean.nimbusEvents`, `Glean.nimbusTargetingContext`, `Glean.nimbusTargetingEnvironment`, `Glean.normandy` or `GleanPings.nimbusTargetingContext` (checked with `git grep`). Tests that do are in section 7.

Telemetry: `Events.yaml` category `normandy` (section 3.1). No Scalars/Histograms entries named for Nimbus were found.

Crash annotation `NimbusEnrollments`: see section 4.

FTL strings: `nimbus-rollouts`, `settings-pane-labs-*`, `quickactions-labs`, `quickactions-cmd-labs`, `-firefoxlabs-brand-name`, `toolkit/firefoxlabs/features.ftl`, `support-remote-*` (section 3.1). No `l10n.toml` or `jar.mn` entries name Nimbus FTL files.

## 6. Build-system edits

Front-end only (`./mach build faster` is enough for these on their own):

- `toolkit/components/nimbus/jar.mn`: keep only `res/nimbus/ExperimentAPI.sys.mjs`.
- `browser/components/preferences/jar.mn`, `browser/themes/shared/jar.inc.mn`, `preferences.xhtml`, FTL files, `firefox.js`, `m6e.js`, JS files in section 3.1.
- `toolkit/components/backgroundtasks/moz.build` (task list), preferences test manifests.

Need `./mach build` (C++, WebIDL, Glean codegen; no configure-time change, no full rebuild expected):

- `toolkit/components/nimbus/moz.build`: reduce to the BUG_COMPONENT block and `JAR_MANIFESTS += ["jar.mn"]`. Remove `EXPORTS.mozilla.browser`, `UNIFIED_SOURCES`, `TEST_DIRS` (gtest), `BROWSER_CHROME_MANIFESTS`, `PYTHON_UNITTEST_MANIFESTS`, `SPHINX_TREES`, the `test/unit/shutdown` entry of `XPCSHELL_TESTS_MANIFESTS` (keep `test/unit/xpcshell.toml` for the new stub test), `TESTING_JS_MODULES` (both), both `GeneratedFile(...)` calls and `FINAL_LIBRARY = "xul"`. Keep the `DIRS += ["nimbus"]` line in `toolkit/components/moz.build` (the directory stays).
- `dom/chrome-webidl/moz.build` and `Nimbus.webidl` deletion (WebIDL binding regeneration).
- C++ edits: `AboutRedirector.cpp`, `HttpBaseChannel.cpp`, `BounceTrackingProtection.cpp`, `Preferences.cpp`, `BackgroundTasks.cpp`.
- `toolkit/components/glean/metrics_index.py` and the metric/ping YAML changes: Glean codegen changes the generated `*Metrics.h` headers and the metric lookup tables, so some C++ units recompile. This is the main compile cost; `NimbusMetrics.h` has no other includer.
- `toolkit/components/telemetry/Events.yaml`: Telemetry event definitions are generated into C++/JS (`TelemetryEventData.h`), small rebuild.

Configure-time: none. No `moz.configure`, `mozconfig`, `confvars.sh` or `MOZ_*` flag is tied to Nimbus.

Rust / Cargo: no edits proposed. `third_party/application-services/components/nimbus` (excluded from the workspace) and `components/support/nimbus-fml` / `nimbus-cli` are vendored (D21) and the desktop build never builds them. `services/app-services-tools/nimbus-fml` is a host Rust program that `services/moz.build` does not include in `DIRS` (only `embedded-uniffi-bindgen`); it is an Android Gradle tool. Removing its workspace member would force a `Cargo.lock` regeneration and a vendor check. Leave `Cargo.toml`, `Cargo.lock`, `build/workspace-hack/Cargo.toml`, `build/sparse-profiles/taskgraph`, `python/mozbuild/mozbuild/vendor/sbom_cargo.py`, `supply-chain/config.toml`, `toolkit/components/uniffi-bindgen-gecko-js/mach_commands.py` and the `taskcluster/` entries alone. If the user wants them gone, do it as a separate `cargo vendor` step.

Generated type files (`tools/@types/generated/*`, `tools/ts/mach_commands.py` line 147): regenerate with `./mach ts` outside the shared objdir, or hand-delete the `FeatureManifest` and `FirefoxLabs` entries. Not build-relevant.

## 7. Tests to delete or edit

M6E regression suites (`tools/m6e/run_tests.sh`) affected:

| Test | Action |
|---|---|
| `toolkit/components/nimbus/test/unit` (run as `nimbus`, 33 tests today) | Delete the 29 tests, head files and `shutdown/` (they test the machinery); keep the `run nimbus` line and replace the content with the single new stub test (section 8). |
| `browser/components/preferences/tests/etp/browser_etp_status.js` | Uses `NimbusTestUtils.enrollWithFeatureConfig` around line 115 for the `trackerCountFeatureGate` variable; set its fallback pref `browser.urlbar.trackerCount.featureGate` to false with `SpecialPowers.pushPrefEnv` instead. |
| `browser/components/urlbar/tests/browser-trustPanel/browser_trust_panel_icon.js` | Same, around line 531; replace with a pref set (see the table of fallback prefs in the stub map). Also drop the `ExperimentAPI` and `NimbusTestUtils` lazy imports. |
| `toolkit/modules/tests/xpcshell/test_firstStartup.js` | Remove the `Glean.firstStartup.nimbus*` assertions (lines ~43-54 and ~88-99). |
| `toolkit/modules/tests/browser/browser_Troubleshoot.js` | Remove the `nimbusExperiments` expected data (it validates the `normandy` provider). |
| `services/settings/test/unit/test_m6e_allowed_collections.js`, `test_remote_settings_startup_bundle.js` | No change: `nimbus-desktop-experiments` is only used as a collection name (the blocked-list example and a startup-bundle id). Keep. |
| `browser/components/enterprisepolicies/tests` | No Nimbus test found in `xpcshell`; the browser test `browser_policy_usermessaging.js` references `category-experimental` and needs the FirefoxLabs/`category-experimental` assertions removed. |

Other deletions (Nimbus-only test files; remove their manifest entries too):

- `toolkit/components/nimbus/test/**`, `browser/components/preferences/tests/experimental_features/**`
- `browser/components/aboutwelcome/tests/browser/browser_aboutwelcome_nimbus_gate.js`, `browser_aboutwelcome_multistage_experimentAPI.js`
- `browser/components/asrouter/tests/browser/browser_trigger_nimbusUpdate.js`
- `browser/components/newtab/test/xpcshell/test_nimbus_newtabTrainhopAddon*.js` and `head_nimbus_trainhop.js` (manifest `xpcshell.toml` entries)
- `browser/components/privatebrowsing/test/browser/browser_privatebrowsing_about_nimbus_*.js` (4 files)
- `browser/components/search/test/browser/browser_defaultPrivate_nimbus.js`
- `browser/components/sidebar/tests/browser/browser_sidebar_nimbus.js`
- `browser/components/taskbartabs/test/browser/browser_taskbarTabs_nimbus.js`
- `browser/components/tests/browser/whats_new_page/browser_whats_new_page_nimbus*.js` and the two `.toml` includes
- `browser/modules/test/browser/browser_PermissionUI_notification_nimbus.js`
- `toolkit/components/antitracking/bouncetrackingprotection/test/browser/browser_bouncetracking_nimbus.js`, `toolkit/components/antitracking/test/browser/browser_nimbus_3pcb.js`
- `toolkit/components/contentrelevancy/tests/browser/browser_contentrelevancy_nimbus.js`
- `toolkit/components/passwordmgr/test/unit/test_LoginStorageMigrator_nimbus.js`
- `toolkit/components/pictureinpicture/tests/browser_nimbusDisplayDuration.js`
- `toolkit/components/search/tests/xpcshell/test_defaultEngine_experiments.js`, `test_getSubmission_params_prefNimbus.js`, `test_getSubmission_params_prefNimbus_invalid.js`
- `browser/components/urlbar/tests/unit/test_frecency_alternative_nimbus.js`
- `toolkit/components/backgroundtasks/tests/xpcshell/test_backgroundtask_experiments.js` + `experiment.json`

Shared test helpers that call `NimbusTestUtils` and need the Nimbus parts removed (they are imported by many urlbar/preferences/asrouter tests): `browser/components/urlbar/tests/UrlbarTestUtils.sys.mjs` (`initNimbusFeature`, `withExperiment`), `browser/components/urlbar/tests/quicksuggest/QuickSuggestTestUtils.sys.mjs`, `browser/components/urlbar/tests/browser/head.js`, `browser/components/preferences/tests/head-common.js`, `browser/components/asrouter/tests/browser/head.js`, `browser/components/privatebrowsing/test/browser/head.js`. Tests that call those Nimbus helpers (urlbar quicksuggest and weather tests, asrouter, newtab, shell, search telemetry, dap, passwordmgr, backup, ...) are listed in Appendix B: delete the ones whose point is an experiment, edit the others to set the fallback pref. Because none of these suites is in the M6E regression list, the pragmatic rule is: delete tests whose title mentions a Nimbus experiment or enrollment; for the rest, replace `NimbusTestUtils.enrollWithFeatureConfig({featureId, value})` with setting the matching fallback pref. Jest/unit mocks under `browser/extensions/newtab/test/jest` and `browser/components/asrouter/tests/unit` that fake `NimbusFeatures` need no change (they never import the real module).

Telemetry unit tests that import `NimbusTestUtils`: `toolkit/components/telemetry/tests/unit/test_{MainPingDisablement,PrefMigrationForTOU,TOUNotificationFlow,TelemetryController_pingDisablement,TelemetryReportingPolicy,canUpload_truth_table}.js` (6; telemetry reporting is off in M6E, so edit to pref-based setup or delete).

## 8. Must stay (with proof)

| Item | Why it stays | Proof of unreachability |
|---|---|---|
| `toolkit/components/nimbus/ExperimentAPI.sys.mjs` stub, `jar.mn`, `moz.build`, the `resource://nimbus/` registration | 77 staying modules import it and use it as a typed pref reader. | The stub has no enrolment code path: it imports nothing but `XPCOMUtils`, has no `RemoteSettings`, `fetch`, `Services.prefs.set*`, timer, IndexedDB/JSON storage, Glean or telemetry call, and no observer except pref observers on fallback prefs. Verify with `git grep -n "RemoteSettings\|fetch(\|setBoolPref\|setStringPref\|setIntPref\|Glean\|Telemetry\|ChromeUtils.import" toolkit/components/nimbus` returning nothing. `getEnrollmentMetadata()` is null and `getAllEnrollments()` is `[]` for every feature, so no caller can take an "enrolled" branch. Add one xpcshell test (`toolkit/components/nimbus/test/unit/test_stub.js`, with a minimal `xpcshell.toml`) asserting those return values and that a fallback pref change fires `onUpdate`. |
| Remote Settings allowlist (`services/settings/Utils.sys.mjs`, M6E) and `test_m6e_allowed_collections.js` | Not Nimbus code; defence in depth. | `nimbus-desktop-experiments` is in the blocked list of the test; `RemoteSettings("nimbus-desktop-experiments")` never fetches. |
| `pref("app.shield.optoutstudies.enabled", false, locked)` in `m6e.js` | Studies prefs and UI remnants still read it. | Locked false; there is no study mechanism left once Nimbus and Normandy are gone. |
| `toolkit/profile/ProfilesDatastoreService.sys.mjs` tables `NimbusEnrollments`, `NimbusSyncTimestamps` (+ CREATE/DELETE statements) | Part of the shared profile-group SQLite schema (also used by Selectable Profiles); dropping tables needs a schema migration and has tests (`toolkit/profile/tests`). | Nothing writes to them after this step (the only writers were `ExperimentStore`/`SelectableProfileService`, both edited). Schema-only; no code path, no network. |
| `Telemetry`/Glean experiment APIs (`TelemetryEnvironment.setExperimentActive`, `Services.fog.setExperimentActive`, `GleanServerKnobs`) | Platform telemetry code. | Nimbus was their only caller; telemetry upload is off in M6E (`MOZ_TELEMETRY_REPORTING` off, `datareporting.*` locked). |
| `toolkit/components/utils/{ClientEnvironment,FilterExpressions,mozjexl,Sampling}.sys.mjs` | Used by Remote Settings and ASRouter targeting. | Not Nimbus. Keep with their tests in the `modules` suite. |
| ASRouter `messaging-experiments` provider pref, `nimbusUpdate` trigger, `TriggerActionSchemas` entry | ASRouter stays (not a P6 feature). | Provider resolves to `[]`: `NimbusFeatures[id].getAllEnrollments()` is empty. O: set the provider pref to `null` as `testing/profiles/common/user.js` does. |
| `third_party/application-services/components/{nimbus,support/nimbus-fml,support/nimbus-cli}`, `services/app-services-tools/nimbus-fml`, Cargo.lock/Cargo.toml entries | Vendored (D21) and Android-only; not built for desktop. | `services/moz.build` DIRS lists only `embedded-uniffi-bindgen`; `third_party/application-services/components/nimbus` is in the workspace `exclude` list; no gkrust dependency on `nimbus-sdk` (only the unbuilt `megazord` crate). Check with `cargo tree -p gkrust` outside the shared objdir if in doubt. |
| `taskcluster/` and `mots.yaml` Nimbus entries | CI and governance metadata, not built or shipped. | Not referenced by the M6E build. |

## 9. Risks and open questions

Current reachability (why the stub is safe):

- `nimbus.rollouts.enabled` and `app.shield.optoutstudies.enabled` are locked false, so `ExperimentAPI.studiesEnabled` and `rolloutsEnabled` are already false.
- `labsEnabled` is `Services.policies.isAllowed("FirefoxLabs")`, which is true by default, so `ExperimentAPI.enabled` is true today. The Labs pane (`firefoxLabs.mjs`) and `ExperimentAPI.init()` callers (`AboutWelcomeParent`, `DefaultLaunchOnLogin`, `BackgroundTasksUtils.enableNimbus`) could start the loader, but the Remote Settings allowlist blocks the `nimbus-desktop-experiments` collection. That means Labs recipes could never load, and removing the code makes this explicit.
- I did not find a startup hook that initialises Nimbus (no call in `BrowserGlue`, no `components.conf`, no JSWindowActor/JSProcessActor, no XPCOM contract, no about: module, no process type). Initialisation is lazy via `ExperimentAPI.ready()`/`manager`, and via the three `init()` callers above.

Risks and open questions:

1. Decision needed: stub (recommended) vs full removal of all call sites. Full removal means rewriting about 250 `getVariable`/`onUpdate` call sites in ~75 files, including the large newtab and urlbar modules, plus prebuilt bundles for about:welcome and ASRouter admin. I judge that too risky for this step.
2. `UrlbarPrefs`, `QuickSuggest` and newtab `PrefsFeed` rely on `onUpdate` firing on fallback-pref changes (real behaviour today). The stub must implement that or the urlbar and New Tab will stop reacting to about:config changes. Cover with the new stub test and with the existing urlbar `test_UrlbarPrefs.js` (it tests `onNimbusChanged`).
3. Hand-writing the 103-entry fallback map from `FeatureManifest.yaml` must happen before the YAML is deleted; do it with a one-off script run on the base commit (ruby `yaml` works; system python has no PyYAML) and prune to features that surviving code references (`git grep "NimbusFeatures\.\(id\)"`, `NimbusFeatures\[`). Features used through string constants need care: `kFireButtonNimbusFeature`, `PERMISSION_UI_FEATURE_ID`, `TRAINHOP_NIMBUS_*`, `NIMBUS_FEATURE_SMART_WINDOW`, `password-autocomplete` (hyphenated id), and ASRouter's `featureIds` from `MessagingExperimentConstants.sys.mjs`. The stub's `NimbusFeatures` should be a `Proxy` returning a pref-less feature for unknown ids so none of these throw. Also keep the `json` type parse for `aboutwelcome.screens` and `preonboarding.screens`.
4. `Preferences.cpp`, `AboutRedirector.cpp`, `HttpBaseChannel.cpp`, `BounceTrackingProtection.cpp` and the Glean/WebIDL codegen mean this branch needs an incremental `./mach build`, not `build faster`, and will recompile many Glean-dependent units. No configure change.
5. `NimbusMetrics.h` and `NimbusBinding.h` disappear. Nothing else includes them (checked), but run a full `./mach build` before the test run.
6. Dependency on other branches: `BackgroundTasksUtils.enableNimbus` has a second caller in the updater (`BackgroundTask_backgroundupdate.sys.mjs`); if the updater branch is not integrated first the build is fine but the updater would call a deleted function (the order says it is integrated earlier, so ignore).
7. Policies: removing `DisableRemoteImprovements` and `UserMessaging.FirefoxLabs` changes the accepted policy schema; a policy.json using them would now be reported as an unknown policy. Acceptable for M6E (no enterprise users) but note it. Alternative: keep both as no-ops.
8. Generated bundles (`aboutwelcome.bundle.js`, `asrouter-admin.bundle.js`, `asrouter-newtab-multistage.bundle.js`) mention `AWWaitForNimbus`; leave them, they work with the stub (`AWWaitForNimbus` is only defined if the actor exports it; the code checks `window.AWWaitForNimbus`).
9. `tools/m6e/acceptance.py` row for Nimbus: confirm the "15 of 15" counting before editing.
10. Stale lint entries: `eslint-rollouts.config.mjs` also lists `browser/components/nimbus/**` and `browser/components/nimbus/tests/...` (already stale since P4.7b); leave those to P6.2.
11. Test cost: the shared helpers (`UrlbarTestUtils.sys.mjs`, `QuickSuggestTestUtils.sys.mjs`, `head.js` files) are imported by hundreds of tests; editing them is mechanical but should be reviewed against the P6.0 baseline so a test that passed before and fails now is a real regression, not a missing helper.

## Appendix A: staying files that import `resource://nimbus/ExperimentAPI.sys.mjs` (non-test)

- `browser/actors/AboutNewTabChild.sys.mjs`
- `browser/actors/AboutPrivateBrowsingChild.sys.mjs`
- `browser/base/content/browser-sync.js`
- `browser/base/content/browser-trustPanel.js`
- `browser/base/content/browser.js`
- `browser/base/content/nsContextMenu.sys.mjs`
- `browser/components/BrowserContentHandler.sys.mjs`
- `browser/components/BrowserGlue.sys.mjs`
- `browser/components/StartupTelemetry.sys.mjs`
- `browser/components/aboutwelcome/actors/AboutWelcomeParent.sys.mjs`
- `browser/components/aiwindow/ui/components/ai-window/ai-window.mjs`
- `browser/components/aiwindow/ui/content/firstrun.js`
- `browser/components/aiwindow/ui/modules/AIWindow.sys.mjs`
- `browser/components/asrouter/modules/ASRouter.sys.mjs`
- `browser/components/asrouter/modules/ASRouterTargeting.sys.mjs`
- `browser/components/asrouter/modules/ASRouterTelemetry.sys.mjs`
- `browser/components/asrouter/modules/BookmarksBarButton.sys.mjs`
- `browser/components/asrouter/modules/PreonboardingSplash.sys.mjs`
- `browser/components/asrouter/modules/ToastNotification.sys.mjs`
- `browser/components/backup/BackupService.sys.mjs`
- `browser/components/backup/resources/PreferencesBackupResource.sys.mjs`
- `browser/components/customizableui/CustomizableWidgets.sys.mjs`
- `browser/components/defaultlaunchonlogin/DefaultLaunchOnLogin.sys.mjs`
- `browser/components/genai/GenAI.sys.mjs`
- `browser/components/genai/LinkPreview.sys.mjs`
- `browser/components/newtab/AboutNewTabResourceMapping.sys.mjs`
- `browser/components/preferences/config/firefoxLabs.mjs`
- `browser/components/preferences/config/moreFromMozilla.mjs`
- `browser/components/preferences/main.js`
- `browser/components/preferences/preferences.js`
- `browser/components/profiles/SelectableProfileService.sys.mjs`
- `browser/components/protocolhandler/WebProtocolHandlerRegistrar.sys.mjs`
- `browser/components/screenshots/ScreenshotsUtils.sys.mjs`
- `browser/components/search/SERPCategorization.sys.mjs`
- `browser/components/sessionstore/SessionStore.sys.mjs`
- `browser/components/shell/ShellService.sys.mjs`
- `browser/components/sidebar/SidebarManager.sys.mjs`
- `browser/components/tabbrowser/content/browser-allTabsMenu.js`
- `browser/components/urlbar/QuickActionsLoaderDefault.sys.mjs`
- `browser/components/urlbar/QuickSuggest.sys.mjs`
- `browser/components/urlbar/UrlbarPrefs.sys.mjs`
- `browser/components/urlbar/UrlbarProviderSemanticHistorySearch.sys.mjs`
- `browser/components/urlbar/private/AmpSuggestions.sys.mjs`
- `browser/extensions/newtab/lib/ActivityStream.sys.mjs`
- `browser/extensions/newtab/lib/DiscoveryStreamFeed.sys.mjs`
- `browser/extensions/newtab/lib/PrefsFeed.sys.mjs`
- `browser/extensions/newtab/lib/TelemetryFeed.sys.mjs`
- `browser/extensions/newtab/lib/TopSitesFeed.sys.mjs`
- `browser/modules/AboutNewTab.sys.mjs`
- `browser/modules/BrowserDOMWindow.sys.mjs`
- `browser/modules/BrowserUsageTelemetry.sys.mjs`
- `browser/modules/PermissionUI.sys.mjs`
- `services/fxaccounts/FxAccountsCommands.sys.mjs`
- `toolkit/actors/PictureInPictureChild.sys.mjs`
- `toolkit/components/backgroundtasks/BackgroundTask_message.sys.mjs`
- `toolkit/components/backgroundtasks/BackgroundTasksUtils.sys.mjs`
- `toolkit/components/contentrelevancy/ContentRelevancyManager.sys.mjs`
- `toolkit/components/dap/DAPIncrementality.sys.mjs`
- `toolkit/components/dap/DAPTelemetrySender.sys.mjs`
- `toolkit/components/dap/DAPVisitCounter.sys.mjs`
- `toolkit/components/doh/DoHController.sys.mjs`
- `toolkit/components/ipprotection/IPPNimbusHelper.sys.mjs`
- `toolkit/components/messaging-system/lib/SpecialMessageActions.sys.mjs`
- `toolkit/components/passwordmgr/LoginManagerParent.sys.mjs`
- `toolkit/components/pdfjs/content/PdfJsParent.sys.mjs`
- `toolkit/components/search/ConfigSearchEngine.sys.mjs`
- `toolkit/components/telemetry/app/TelemetryControllerParent.sys.mjs`
- `toolkit/components/telemetry/app/TelemetryReportingPolicy.sys.mjs`
- `toolkit/components/telemetry/app/TelemetrySend.sys.mjs`
- `toolkit/components/telemetry/pings/TelemetrySession.sys.mjs`
- `toolkit/modules/FirstStartup.sys.mjs`
- `toolkit/modules/Troubleshoot.sys.mjs`
- `toolkit/mozapps/handling/ContentDispatchChooser.sys.mjs`
- `toolkit/mozapps/update/BackgroundTask_backgroundupdate.sys.mjs`
- `toolkit/mozapps/update/BackgroundUpdate.sys.mjs`
- `toolkit/mozapps/update/UpdateListener.sys.mjs`
- `uriloader/exthandler/ExtHandlerService.sys.mjs`

## Appendix B: test files that use `NimbusTestUtils` (95) or mention Nimbus/ExperimentAPI (other, 121)

### B1. Uses NimbusTestUtils (93)

- `browser/base/content/test/browser-about/browser_aboutSupport.js`
- `browser/base/content/test/browser-sync/browser_appmenu_signin_cta_variants.js`
- `browser/base/content/test/browser-sync/browser_avatar_menu_signin_cta_variants.js`
- `browser/base/content/test/browser-sync/browser_signed_out_avatar_variants.js`
- `browser/base/content/test/browser-sync/browser_sync.js`
- `browser/components/aboutwelcome/tests/browser/browser_aboutwelcome_configurable_ui.js`
- `browser/components/aboutwelcome/tests/browser/browser_aboutwelcome_layout.js`
- `browser/components/aboutwelcome/tests/browser/browser_aboutwelcome_logo.js`
- `browser/components/aboutwelcome/tests/browser/browser_aboutwelcome_multistage_experimentAPI.js`
- `browser/components/aboutwelcome/tests/browser/browser_aboutwelcome_multistage_transitions.js`
- `browser/components/aboutwelcome/tests/browser/browser_aboutwelcome_navigation.js`
- `browser/components/aboutwelcome/tests/browser/browser_aboutwelcome_theming.js`
- `browser/components/aboutwelcome/tests/xpcshell/test_AboutWelcomeTelemetry_exposure.js`
- `browser/components/aiwindow/ui/test/browser/browser_smartwindow_nimbus.js`
- `browser/components/aiwindow/ui/test/xpcshell/test_AutoTabGrouping.js`
- `browser/components/asrouter/tests/browser/browser_asrouter_experimentsAPILoader.js`
- `browser/components/asrouter/tests/browser/browser_asrouter_targeting.js`
- `browser/components/asrouter/tests/browser/browser_bookmarks_bar_button.js`
- `browser/components/asrouter/tests/browser/browser_coenrolling_messaging_features.js`
- `browser/components/asrouter/tests/browser/browser_trigger_messagesLoaded.js`
- `browser/components/asrouter/tests/browser/browser_trigger_nimbusUpdate.js`
- `browser/components/asrouter/tests/browser/head.js`
- `browser/components/asrouter/tests/xpcshell/test_PreonboardingSplash.js`
- `browser/components/backup/tests/xpcshell/test_BackupService_enabled.js`
- `browser/components/backup/tests/xpcshell/test_PreferencesBackupResource_Nimbus.js`
- `browser/components/customizableui/test/browser_fireButton.js`
- `browser/components/defaultlaunchonlogin/tests/test_DefaultLaunchOnLogin.js`
- `browser/components/genai/tests/browser/browser_chat_nimbus.js`
- `browser/components/genai/tests/browser/browser_link_preview_nimbus.js`
- `browser/components/ipprotection/tests/browser/head.js`
- `browser/components/newtab/test/browser/abouthomecache/browser_experiments_api_control.js`
- `browser/components/newtab/test/xpcshell/head_nimbus_trainhop.js`
- `browser/components/newtab/test/xpcshell/test_nimbus_newtabTrainhopAddon.js`
- `browser/components/newtab/test/xpcshell/test_nimbus_newtabTrainhopAddon_firstStartup.js`
- `browser/components/preferences/tests/browser_windows_launch_on_login.js`
- `browser/components/preferences/tests/browser_windows_launch_on_login_msix.js`
- `browser/components/preferences/tests/etp/browser_etp_status.js`
- `browser/components/preferences/tests/experimental_features/browser_experimental_features_filter.js`
- `browser/components/preferences/tests/experimental_features/head.js`
- `browser/components/preferences/tests/head-common.js`
- `browser/components/privatebrowsing/test/browser/browser_privatebrowsing_redesign_telemetry.js`
- `browser/components/privatebrowsing/test/browser/head.js`
- `browser/components/reportbrokensite/test/browser/browser_experiment_data_sent.js`
- `browser/components/screenshots/tests/browser/browser_screenshots_test_toolbar_button.js`
- `browser/components/search/test/browser/browser_defaultPrivate_nimbus.js`
- `browser/components/search/test/browser/telemetry/browser_search_telemetry_categorization_experiment_info.js`
- `browser/components/search/test/browser/telemetry/browser_search_telemetry_visualSearch.js`
- `browser/components/shell/test/browser_doesAppNeedPin.js`
- `browser/components/shell/test/browser_setDefaultBrowser.js`
- `browser/components/shell/test/browser_setDefaultPDFHandler.js`
- `browser/components/sidebar/tests/browser/browser_sidebar_nimbus.js`
- `browser/components/tabbrowser/test/browser/tabs/browser_tabs_openURI_after_current.js`
- `browser/components/taskbartabs/test/browser/browser_taskbarTabs_nimbus.js`
- `browser/components/tests/browser/browser_browserGlue_upgradeDialog_trigger.js`
- `browser/components/tests/browser/browser_default_browser_prompt.js`
- `browser/components/tests/browser/browser_default_webprotocol_handler_mailto.js`
- `browser/components/urlbar/tests/UrlbarTestUtils.sys.mjs`
- `browser/components/urlbar/tests/browser-quickactions/browser_commands.js`
- `browser/components/urlbar/tests/browser-trustPanel/browser_trust_panel_icon.js`
- `browser/components/urlbar/tests/browser/head.js`
- `browser/components/urlbar/tests/quicksuggest/QuickSuggestTestUtils.sys.mjs`
- `browser/components/urlbar/tests/quicksuggest/unit/test_merinoClient.js`
- `browser/extensions/formautofill/test/browser/browser_ml_nimbus.js`
- `browser/extensions/formautofill/test/browser/browser_storage_rust_nimbus.js`
- `browser/extensions/newtab/test/browser/browser_SmartShortcuts.js`
- `browser/extensions/newtab/test/browser/browser_activation_window.js`
- `browser/extensions/newtab/test/browser/browser_newtab_spaces.js`
- `browser/extensions/newtab/test/browser/browser_ohttpImages.js`
- `browser/extensions/newtab/test/xpcshell/test_AboutHomeStartupCacheWorker.js`
- `browser/modules/test/browser/browser_PermissionUI_notification_nimbus.js`
- `services/fxaccounts/tests/xpcshell/test_commands_closetab.js`
- `toolkit/components/antitracking/bouncetrackingprotection/test/browser/browser_bouncetracking_nimbus.js`
- `toolkit/components/antitracking/test/browser/browser_nimbus_3pcb.js`
- `toolkit/components/antitracking/test/browser/browser_nimbus_etp_features.js`
- `toolkit/components/backgroundtasks/tests/xpcshell/test_backgroundtask_experiments.js`
- `toolkit/components/contentrelevancy/tests/browser/browser_contentrelevancy_nimbus.js`
- `toolkit/components/dap/tests/xpcshell/test_dap_incrementality.js`
- `toolkit/components/dap/tests/xpcshell/test_dap_visit_counter.js`
- `toolkit/components/messaging-system/schemas/SpecialMessageActionSchemas/test/browser/browser_sma_submit_onboarding_opt_out_ping.js`
- `toolkit/components/passwordmgr/test/browser/browser_autocomplete_import.js`
- `toolkit/components/passwordmgr/test/unit/test_LoginStorageMigrator_nimbus.js`
- `toolkit/components/pictureinpicture/tests/browser_nimbusDisplayDuration.js`
- `toolkit/components/pictureinpicture/tests/browser_nimbusFirstTimeStyleVariant.js`
- `toolkit/components/pictureinpicture/tests/browser_nimbusMessageFirstTimePip.js`
- `toolkit/components/pictureinpicture/tests/browser_nimbusShowIconOnly.js`
- `toolkit/components/telemetry/tests/unit/test_MainPingDisablement.js`
- `toolkit/components/telemetry/tests/unit/test_PrefMigrationForTOU.js`
- `toolkit/components/telemetry/tests/unit/test_TOUNotificationFlow.js`
- `toolkit/components/telemetry/tests/unit/test_TelemetryController_pingDisablement.js`
- `toolkit/components/telemetry/tests/unit/test_TelemetryReportingPolicy.js`
- `toolkit/components/telemetry/tests/unit/test_canUpload_truth_table.js`
- `toolkit/mozapps/update/tests/unit_background_update/test_backgroundupdate_glean.js`
- `uriloader/exthandler/tests/mochitest/browser_mailto_handler_picker.js`

### B2. Other test and test-adjacent files that mention Nimbus or ExperimentAPI (95; includes manifests, mocks and files that only mention the word)

- `browser/base/content/test/browser-static/browser_all_files_referenced.js`
- `browser/components/aboutwelcome/tests/browser/browser.toml`
- `browser/components/aboutwelcome/tests/browser/browser_aboutwelcome_nimbus_gate.js`
- `browser/components/aboutwelcome/tests/browser/browser_aboutwelcome_screen_targeting.js`
- `browser/components/aboutwelcome/tests/unit/unit-entry.js`
- `browser/components/aiwindow/models/tests/browser_eval/tests/basic_quality.sys.mjs`
- `browser/components/aiwindow/ui/test/browser/browser_smartwindow.toml`
- `browser/components/asrouter/tests/NimbusRolloutMessageProvider.sys.mjs`
- `browser/components/asrouter/tests/browser/browser.toml`
- `browser/components/asrouter/tests/unit/ASRouter.test.js`
- `browser/components/asrouter/tests/unit/ASRouterTargeting.test.js`
- `browser/components/asrouter/tests/unit/MomentsPageHub.test.js`
- `browser/components/asrouter/tests/unit/content-src/components/MultiStageAWProton.test.jsx`
- `browser/components/asrouter/tests/unit/unit-entry.js`
- `browser/components/asrouter/tests/unit/utils.js`
- `browser/components/asrouter/tests/xpcshell/test_ASRouterTelemetry.js`
- `browser/components/extensions/test/browser/browser_ext_browserAction_experiment.js`
- `browser/components/extensions/test/xpcshell/test_ext_settings_overrides_defaults.js`
- `browser/components/genai/tests/browser/browser.toml`
- `browser/components/genai/tests/browser/browser_link_preview_aifeature_lifecycle.js`
- `browser/components/ipprotection/tests/browser/browser_IPProtectionService.js`
- `browser/components/newtab/test/xpcshell/test_nimbus_newtabTrainhopAddon_onBrowserReady.js`
- `browser/components/newtab/test/xpcshell/test_nimbus_newtabTrainhopAddon_shutdown.js`
- `browser/components/newtab/test/xpcshell/xpcshell.toml`
- `browser/components/preferences/tests/experimental_features/browser_experimental_features_hidden_when_not_public.js`
- `browser/components/preferences/tests/experimental_features/browser_experimental_features_resetall.js`
- `browser/components/preferences/tests/moreFromMozilla/head.js`
- `browser/components/preferences/tests/search/browser_search_firefoxSuggest.js`
- `browser/components/preferences/tests/search/head.js`
- `browser/components/privatebrowsing/test/browser/browser.toml`
- `browser/components/privatebrowsing/test/browser/browser_privatebrowsing_about_nimbus_impressions.js`
- `browser/components/search/test/browser/browser.toml`
- `browser/components/sidebar/tests/browser/browser.toml`
- `browser/components/tabbrowser/test/browser/smarttabgrouping/browser_tab_grouping_engine_init.js`
- `browser/components/taskbartabs/test/browser/browser.toml`
- `browser/components/tests/browser/whats_new_page/browser.toml`
- `browser/components/tests/browser/whats_new_page/browser_whats_new_page_nimbus.js`
- `browser/components/tests/browser/whats_new_page/browser_whats_new_page_nimbus.toml`
- `browser/components/tests/browser/whats_new_page/browser_whats_new_page_nimbus_disable.js`
- `browser/components/tests/browser/whats_new_page/browser_whats_new_page_nimbus_disable.toml`
- `browser/components/topsites/test/unit/test_top_sites.js`
- `browser/components/urlbar/tests/browser-engagementTelemetry/browser_glean_telemetry_keyword_exposure.js`
- `browser/components/urlbar/tests/browser-engagementTelemetry/browser_glean_telemetry_record_preferences.js`
- `browser/components/urlbar/tests/browser-quickactions/browser_general.js`
- `browser/components/urlbar/tests/browser-searchMode/browser_searchModeSwitcher_basic.js`
- `browser/components/urlbar/tests/quicksuggest/browser/browser_quicksuggest_ping_offline.js`
- `browser/components/urlbar/tests/quicksuggest/browser/browser_quicksuggest_ping_online.js`
- `browser/components/urlbar/tests/quicksuggest/browser/browser_quicksuggest_yelp.js`
- `browser/components/urlbar/tests/quicksuggest/browser/browser_weather.js`
- `browser/components/urlbar/tests/quicksuggest/unit/head.js`
- `browser/components/urlbar/tests/quicksuggest/unit/test_quicksuggest.js`
- `browser/components/urlbar/tests/quicksuggest/unit/test_quicksuggest_addons.js`
- `browser/components/urlbar/tests/quicksuggest/unit/test_quicksuggest_amp.js`
- `browser/components/urlbar/tests/quicksuggest/unit/test_quicksuggest_dynamicSuggestions.js`
- `browser/components/urlbar/tests/quicksuggest/unit/test_quicksuggest_exposures.js`
- `browser/components/urlbar/tests/quicksuggest/unit/test_quicksuggest_exposures_locales.js`
- `browser/components/urlbar/tests/quicksuggest/unit/test_quicksuggest_mdn.js`
- `browser/components/urlbar/tests/quicksuggest/unit/test_quicksuggest_yelp.js`
- `browser/components/urlbar/tests/quicksuggest/unit/test_weather.js`
- `browser/components/urlbar/tests/quicksuggest/unit/test_weather_keywords.js`
- `browser/components/urlbar/tests/unit/test_UrlbarNewTabComponentRegistrant.js`
- `browser/components/urlbar/tests/unit/test_UrlbarPrefs.js`
- `browser/components/urlbar/tests/unit/test_UrlbarProviderSemanticHistorySearch.js`
- `browser/components/urlbar/tests/unit/xpcshell.toml`
- `browser/extensions/formautofill/test/browser/browser.toml`
- `browser/extensions/newtab/test/jest/lib/ActivityStream.test.js`
- `browser/extensions/newtab/test/jest/lib/DiscoveryStreamFeed.test.js`
- `browser/extensions/newtab/test/jest/lib/PrefsFeed.test.js`
- `browser/extensions/newtab/test/jest/lib/SectionsManager.test.js`
- `browser/extensions/newtab/test/xpcshell/test_AdsFeed.js`
- `browser/extensions/newtab/test/xpcshell/test_TelemetryFeed.js`
- `browser/extensions/newtab/test/xpcshell/test_TopSitesFeed.js`
- `browser/extensions/newtab/test/xpcshell/test_TopSitesFeed_glean.js`
- `browser/modules/test/browser/browser.toml`
- `services/settings/test/unit/test_m6e_allowed_collections.js`
- `services/settings/test/unit/test_remote_settings_startup_bundle.js`
- `testing/mozbase/mozsystemmonitor/tests/test_resource_monitor.py`
- `testing/profiles/perf/user.js`
- `testing/profiles/xpcshell/user.js`
- `toolkit/components/antitracking/bouncetrackingprotection/test/browser/browser.toml`
- `toolkit/components/antitracking/test/browser/browser.toml`
- `toolkit/components/contentrelevancy/tests/browser/browser.toml`
- `toolkit/components/extensions/test/xpcshell/test_ext_experiments.js`
- `toolkit/components/extensions/test/xpcshell/test_ext_runtime_ports_gc.js`
- `toolkit/components/messaging-system/schemas/SpecialMessageActionSchemas/test/browser/browser_sma_closed_browser_notifications.js`
- `toolkit/components/passwordmgr/test/unit/xpcshell.toml`
- `toolkit/components/pictureinpicture/tests/browser.toml`
- `toolkit/components/search/tests/xpcshell/test_defaultEngine_experiments.js`
- `toolkit/components/search/tests/xpcshell/test_getSubmission_params_prefNimbus.js`
- `toolkit/components/search/tests/xpcshell/test_getSubmission_params_prefNimbus_invalid.js`
- `toolkit/modules/tests/browser/browser_Troubleshoot.js`
- `toolkit/modules/tests/xpcshell/test_firstStartup.js`
- `toolkit/mozapps/extensions/test/xpcshell/test_remote_pref_telemetry.js`
- `toolkit/mozapps/update/tests/unit_background_update/test_backgroundupdate_reason_update.js`
- `tools/m6e/run_tests.sh`

## Critic addendum

Reviewer: independent critic, read-only, base cb002d4874. Method: tree-wide `git grep -I -i` for nimbus / FeatureManifest / ExperimentAPI / firefoxlabs / category-experimental / heartbeat / normandy, then per-file checks of every unlisted hit. The analysis is mostly sound. The stub design, the C++ list (3 files plus `Preferences.cpp`), the `aboutwelcome.enabled` fallback pref and the manifest counts (214 features, 103 fallbackPref variables in 19 features) all check out. The items below are what it missed or got wrong.

### A. Blocking: build would break (must fix in this branch)

| File | Reference | Action |
|---|---|---|
| `toolkit/components/nimbus/pings.yaml` (deleted) -> `toolkit/mozapps/extensions/metrics.yaml` (~line 96) | `send_in_pings` lists `heartbeat` | The `heartbeat` ping is defined only in `toolkit/components/nimbus/pings.yaml` (second entry, after `nimbus-targeting-context`). The report deletes the file and says the heartbeat ping is only a "Nimbus" ping. Deleting it makes glean_parser fail with an unknown ping name. Either remove `- heartbeat` from the metric's `send_in_pings`, or keep a one-ping `pings.yaml` (then keep its `metrics_index.py` entries). Removing the reference is the cleaner choice: Normandy Heartbeat is gone (P4.7b). |
| `toolkit/profile/metrics.yaml` (~lines 199 and 271) | `send_in_pings` lists `heartbeat` on two metrics | Same fix: remove `- heartbeat` from both. |
| `toolkit/components/telemetry/metrics.yaml`, category `onboarding_opt_out` | Report says "delete the three metrics" | These are the only three metrics in the category. Delete the whole `onboarding_opt_out:` key and its leading comment, not just the children, otherwise the YAML category is null and the Glean parser rejects it. The `onboarding-opt-out` ping in `toolkit/components/telemetry/pings.yaml` (`send_if_empty: true`) can stay. |
| `toolkit/components/messaging-system/schemas/SpecialMessageActionSchemas/test/browser/browser_sma_submit_onboarding_opt_out_ping.js` | Enrols with `NimbusTestUtils` and asserts the three deleted `Glean.onboardingOptOut.*` metrics | Not mentioned in section 3.1 or 7 (only listed in Appendix B as a NimbusTestUtils user). Rewrite to only assert the ping is submitted (`GleanPings.onboardingOptOut.testBeforeNextSubmit`) or delete the task; keep its entry in `browser.toml` consistent. |

### B. Wrong claims and corrections

1. Section 3.1 `PreferencesBackupResource.sys.mjs`: "call `backupPrefFile(prefsDestFile, PreferencesBackupResource.addPrefsToIgnoreInBackup({}))`". Wrong. `addPrefsToIgnoreInBackup` calls `prefsOverrideMap.addEntry(...)`, so `{}` throws. The argument must be an `nsIPrefOverrideMap`: `Cc["@mozilla.org/pref-override-map;1"].createInstance(Ci.nsIPrefOverrideMap)`. That is what `ExperimentStore.getOriginalPrefValuesForAllActiveEnrollments` does today, and its code can be inlined. Also check `browser/components/backup/tests/xpcshell/test_PreferencesBackupResource*.js` for callers that pass a mock map.
2. Section 3.1 `backgroundtasks_browser.js`: "Leave the Normandy line (`app.normandy.run_interval_seconds`, not this feature)". It is dead after Nimbus is gone and its only readers are Nimbus tests (`test_RemoteSettingsExperimentLoader*.js`, nimbus `xpcshell.toml`/`browser.toml`, plus `preferences/tests/experimental_features/head.js` and `urlbar/tests/browser-quickactions/browser_commands.js`). Remove it with the `nimbus.appId` line (and its comment "Don't refresh experiments while a background task is running"). Also remove the `Remote Settings and Nimbus` wording in the comment in `toolkit/components/backgroundtasks/defaults/backgroundtasks.js` (~line 68), comment only.
3. Section 7: "`browser_privatebrowsing_about_nimbus_*.js` (4 files)". There are 2: `browser_privatebrowsing_about_nimbus_dismiss.js` and `..._impressions.js`.
4. Section 2 / 3.1: "Remove its `include`/manifest entry from the parent preferences test manifest" for `tests/experimental_features`. The entry is `"tests/experimental_features/browser.toml"` in the `BROWSER_CHROME_MANIFESTS` of `browser/components/preferences/moz.build` (line ~14), not a `.toml` include.
5. Section 8 / 9 call `nimbus-desktop-experiments` a "blocked list" entry. `services/settings/Utils.sys.mjs` is an allowlist (`M6E_ALLOWED_BUCKETS`, `M6E_ALLOWED_MAIN_COLLECTIONS`): the collection is not allowed because it is not listed. The conclusion holds; the test `test_m6e_allowed_collections.js` should be checked for its wording only.
6. Section 3.1 says `labsEnabled` etc. are "constant false" in the stub; today `ExperimentAPI.enabled` is true in M6E (Labs is policy-allowed). Consumers that change behaviour when `enabled` flips to false: `ASRouterTargeting.sys.mjs` (`experimentsLoaded` returns true at once, intended), `PreonboardingSplash.sys.mjs` (`if (!ExperimentAPI.enabled)` branch), `AboutWelcomeParent.sys.mjs` (`shouldGateNimbusForAboutWelcome`). All three are on unreachable or safe paths in M6E (about:welcome locked off), but state it in the commit.
7. Stub must also keep `getSetPrefName`, `getSetPref`, `getFallbackPrefName`, `applications`, `allowCoenrollment` only if a staying non-test caller uses them. I found none outside `toolkit/components/nimbus`, so they can go. `aiFeaturesEnabled`, `getRecipe`, `optInToExperiment`, `ENROLLMENTS_UPDATED`, `STUDIES_ENABLED_CHANGED` have no staying non-test callers after group E (the only users are `firefoxLabs.mjs`, `BackgroundTasksUtils.enableNimbus`, both deleted). `EnrollmentType` is imported by `AboutPrivateBrowsingChild`, `AboutWelcomeParent`, `ASRouterTelemetry`, `ToastNotification`; the stub must export it.

### C. Missed references (file, reference, action)

Front-end, required:

| File | Reference | Action |
|---|---|---|
| `browser/locales/en-US/browser/preferences/preferences.ftl` (~lines 108-114) | `pane-experimental-description4`, `pane-experimental-reset` (used only by `firefoxLabs.mjs`) | Delete with the `settings-pane-labs-*` strings. |
| `browser/themes/shared/preferences/preferences.css` | `.firefoxLabs-description` (~195), `#pane-experimental-featureGates > .featureGate...` and `#pane-experimental-featureGates moz-fieldset::part(label)` (~1095-1101), `#experimentalCategory-reset`, `.featureGateDescription` (~1103-1108) | Delete these rules (all only matched by `firefoxLabs.mjs`). |
| `browser/locales/en-US/browser/policies/policies-descriptions.ftl` (~line 114) | `policy-DisableRemoteImprovements` | Delete together with the policy handler and schema entry. |
| `browser/components/preferences/findInPage.js` (~line 786) | `child.classList.contains("featureGate")` | Optional: becomes dead; harmless. |
| `browser/components/urlbar/tests/browser-quickactions/browser_commands.js` | Builds Labs opt-in recipes (`setupLabsTest`, `firefoxLabsTitle`, `ExperimentAPI._rsLoader`, `NimbusTestUtils`) for the `labs` quick action | Delete the Labs tests and the Nimbus imports (the `labs` quick action is removed). It is only in Appendix B as a plain NimbusTestUtils user. |
| `browser/components/enterprisepolicies/tests/browser/browser_policy_usermessaging.js` | Already noted; also `browser_experimental_features_studies_disabled.js` uses `UserMessaging: { FirefoxLabs: false }` (deleted with the directory) | No new action. |

Tests that are Nimbus-only and exist but the report only lists in a glob or in Appendix B (confirm they are deleted with their `browser.toml` entries): `toolkit/components/pictureinpicture/tests/browser_nimbusFirstTimeStyleVariant.js`, `browser_nimbusMessageFirstTimePip.js`, `browser_nimbusShowIconOnly.js` (section 7 names only `browser_nimbusDisplayDuration.js`; entries at `toolkit/components/pictureinpicture/tests/browser.toml` lines ~131-137); `toolkit/components/antitracking/test/browser/browser_nimbus_etp_features.js` (section 7 names only `browser_nimbus_3pcb.js`; `browser.toml` ~line 116); `browser/extensions/formautofill/test/browser/browser_storage_rust_nimbus.js` (`browser.toml` ~line 168; `browser_ml_nimbus.js` is ML).
Also `browser/extensions/newtab/test/browser/browser_SmartShortcuts.js` and `browser/components/aboutwelcome/tests/unit/unit-entry.js` use the Nimbus manager or mock it; check them when the helpers change.

Docs with Sphinx `{doc}` links to the deleted `toolkit/components/nimbus/docs/index` (would warn in the docs build):
- `browser/components/asrouter/docs/messaging-glossary.md` line 91;
- `docs/contributing/pocket-guide-shipping-firefox.md` line 202 (also the `(nimbus)=` label at line 364).
Fix by turning them into plain text. Other docs only mention Nimbus in prose (`asrouter/docs/targeting-attributes.md`, `first-run.md`, `antitracking/docs/*`, `glean/docs/dev/preferences.md`, `telemetry/docs/data/environment.md` lines 263-264 for `nimbus.qa.pref-1/2`); optional.

Comment-only mentions that need no action: `widget/cocoa/nsMenuBarX.mm` (reads `browser.macAppMenu.setAsDefaultShown` pref, no Nimbus API), `modules/libpref/Preferences.cpp` lines 1334 and 5178 (the `nsIPrefOverrideMap` / `backupPrefFile` machinery is platform code and stays, now with `PreferencesBackupResource` as its only caller), `modules/libpref/init/all.js`, `StaticPrefList.yaml`, `ReportBrokenSiteParent.sys.mjs` (already safe), `PdfStreamConverter.sys.mjs` (`pdfjs-getNimbus` goes through `PdfJsParent`, covered by the stub), `devtools/client/performance-new` (`perftools-thread-jvm-nimbus` is an Android thread name), `browser/components/*/metrics.yaml` Nimbus wording, `taskcluster/config.yml`.

### D. Build, CI and metadata files not covered (all non-blocking unless noted)

| File | Reference | Action |
|---|---|---|
| `taskcluster/kinds/source-test/python.yml` (~532-570) | `nimbus` python-test job: `toolkit/components/nimbus/test/python/**`, `metrics.yaml`, `TargetingContextRecorder.sys.mjs` | Section 6 says "leave `taskcluster/` alone". The job refers to deleted paths; remove it if CI configs are in scope, otherwise list as known stale. |
| `taskcluster/kinds/source-test/vendor-verify.yml` (~186) | `toolkit/components/nimbus/schemas/moz.yaml` | Same: stale after `schemas/` is deleted. |
| `mots.yaml` (~3911-3923) | `toolkit/components/nimbus/**/*` and `nimbus-reviewers` | Leave or trim; `browser/components/nimbus/**/*` is already stale (P6.2). |
| `stylelint-rollouts.config.js` (line 64), `eslint-rollouts.config.mjs` (line 809 `browser_privatebrowsing_about_nimbus.js`, 633) | Stale entries for already-deleted `browser/components/nimbus` and a non-existent test name | P6.2 stale-lint list; no action here. The deleted-path entries this step creates (`toolkit/components/nimbus/**`, eslint 283 and 433) are already in the report. |
| `toolkit/components/glean/tags.yaml` | `'Firefox :: Nimbus Desktop Client'` also referenced as the BUG_COMPONENT in the kept `moz.build` | No action; leave both. |
| `services/app-services-tools/nimbus-fml/{Cargo.toml,moz.build,src/bin/nimbus-fml.rs}` | `HOST_RUST_PROGRAMS = ["nimbus-fml"]` | Confirmed not built: `services/moz.build` DIRS lists only `embedded-uniffi-bindgen` (and `Cargo.toml` workspace member + `sbom_cargo.py` entry stay). The report's claim holds. |
| `Cargo.lock` `megazord` -> `nimbus-sdk` | The `megazord` crate depends on `nimbus-sdk` | Verified `toolkit/library/rust/shared/Cargo.toml` has no `megazord`/`nimbus` dependency, so gkrust does not pull it in. Claim holds. |

### E. Verified claims (sample)

- Three C++ includers of `NimbusFeatures.h` and the `Nimbus.webidl` entries in `dom/chrome-webidl/moz.build` (`Files("Nimbus.webidl")` and the list entry): correct; no other C++/Rust/IDL/ipdl/moz.build includer of `NimbusFeatures`, `NimbusBinding`, `NimbusMetrics` or `NimbusFeatureManifest.h`.
- Glean category names (`nimbus_targeting_environment`, `nimbus_targeting_context`, `nimbus_events`, `normandy`, `heartbeat`) are defined only in the nimbus yaml files; `Glean.nimbus*`/`Glean.normandy` users outside the directory are only the tests the report lists (add `browser_bouncetracking_nimbus.js`, `test_AboutWelcomeTelemetry_exposure.js` and `browser_fireButton.js` to the "rewrite or delete" group, they are already in Appendix B).
- `toolkit/modules/metrics.yaml`: `nimbus_*` metrics use the `&nimbus_startup_metric` anchor only among themselves, so deleting all four is safe.
- `nimbus.profilesdatastoreservice.*`, `nimbus.syncdatastore.*` have no readers outside the deleted code, `Preferences.cpp` allowlist and `testing/profiles/xpcshell/user.js`; the `NimbusEnrollments` / `NimbusSyncTimestamps` tables are touched only by `ProfilesDatastoreService.sys.mjs` (schema) and `SelectableProfileService.sys.mjs`.
- `OnboardingMessageProvider`, `MessagingExperimentConstants.sys.mjs` mention `FeatureManifest.yaml` in comments only.
- No hits for Nimbus in `browser/installer`, `browser/confvars.sh`, `moz.configure`, `remote/`, `devtools` (other than the Android thread name), `testing/marionette`, `tools/lint`, `python/` (only `sbom_cargo.py`), `build/` (only workspace-hack / sparse-profiles for `nimbus-fml`).
