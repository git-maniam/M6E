# p6-1i-nimbus: preparation notes

Branch `m6e-p6/nimbus`, base 4110e90366 (cb002d4874 plus the P6 checkpoint commit). Nothing was built or run; checks were `node --check`, JSON/YAML/Python parses and `git grep`.

## Approach

Nimbus is hollowed out, not removed. `toolkit/components/nimbus/ExperimentAPI.sys.mjs` is now a stub (about 150 lines) that exports `ExperimentAPI`, `NimbusFeatures` (a Proxy that returns a feature object for any id) and `EnrollmentType`. Features read only their fallback prefs (hand-written map of the 13 features that still have callers). No enrolment, RemoteSettings, fetch, pref writes, Glean or telemetry in the stub. `ExperimentAPI.enabled`, `labsEnabled`, `rolloutsEnabled`, `studiesEnabled` are false (`enabled` was true before because Labs was policy-allowed; the three consumers that branch on it are on unreachable paths: ASRouterTargeting `experimentsLoaded` returns true at once, PreonboardingSplash skips, AboutWelcomeParent gate is off).

Dropped from the report's map: features whose only callers belong to earlier features (smartWindow, remoteTabManagement, backgroundUpdate*, updatePrompt, testFeature). Unknown features still resolve (no fallback prefs, `getVariable` returns undefined).

## Deleted

- `toolkit/components/nimbus/{lib,generate,schemas,docs,test}`, `FeatureManifest.yaml`, `FirefoxLabs.sys.mjs`, `metrics.yaml`, `metrics-normandy.yaml`, `pings.yaml`
- `dom/chrome-webidl/Nimbus.webidl`
- `browser/components/preferences/config/firefoxLabs.mjs`, `browser/components/preferences/tests/experimental_features/`, `toolkit/locales/en-US/toolkit/firefoxlabs/features.ftl`, `browser/themes/shared/icons/labs-16.svg`
- `toolkit/components/backgroundtasks/BackgroundTask_message.sys.mjs` and its test and `experiment.json`
- `tools/@types/subs/FeatureManifest.sys.d.mts`
- About 45 Nimbus-only test files (see the commit diffs) and the Labs/killswitch/exposure tasks inside shared tests

## Added (tests only)

- `toolkit/components/nimbus/test/unit/{xpcshell.toml,test_stub.js}`: asserts the stub (never enabled, no enrolments, fallback prefs, onUpdate on pref change, override helper). `tools/m6e/run_tests.sh` already runs this directory as `nimbus`.
- `toolkit/components/nimbus/test/FeatureOverride.sys.mjs` (`TESTING_JS_MODULES`, `resource://testing-common/FeatureOverride.sys.mjs`): `overrideFeature({featureId, value})` makes a feature report given variable values. Used by the telemetry unit tests that used `NimbusTestUtils.enrollWithFeatureConfig`.
- `UrlbarTestUtils.initNimbusFeature` and `QuickSuggestTestUtils.enrollExperiment/withExperiment` now set the variables' fallback prefs (via `feature.fallbackPrefs`); the many urlbar tests that call them are unchanged.
- `privatebrowsing/test/browser/head.js` `setupMSExperimentWithMessage` stubs `NimbusFeatures.pbNewtab.getAllEnrollments` instead of enrolling.

## Edited (staying code)

C++: AboutRedirector.cpp (reads `browser.aboutwelcome.enabled`), HttpBaseChannel.cpp, BounceTrackingProtection.cpp, Preferences.cpp (`nimbus.syncdatastore.` allowlist entry), BackgroundTasks.cpp, dom/chrome-webidl/moz.build.

JS: FirstStartup, Troubleshoot (normandy provider), aboutSupport.{js,xhtml,ftl}, PreferencesBackupResource (inlines the `nsIPrefOverrideMap`; the report's `{}` argument would have thrown), BackupService (killswitch and onUpdate removed), SelectableProfileService, SpecialMessageActions, BackgroundTasksUtils (`enableNimbus`), DefaultLaunchOnLogin, QuickActionsLoaderDefault (labs action), preferences.js/.xhtml/jar.mn/moz.build/permissions-data.mjs/CSS, Policies.sys.mjs + schema + policies-descriptions.ftl (`DisableRemoteImprovements`, `UserMessaging.FirefoxLabs`), TelemetryEnvironment (`nimbus.debug`).

Config: firefox.js (nimbus.*, rsexperimentloader, experimental.hidden, sync pref), m6e.js (`nimbus.rollouts.enabled` lock), testing/profiles xpcshell and perf, backgroundtasks defaults, metrics_index.py, toolkit/modules/metrics.yaml, toolkit/components/telemetry/{metrics.yaml,Events.yaml}, `heartbeat` removed from `send_in_pings` in toolkit/mozapps/extensions and toolkit/profile metrics.yaml (the ping was defined only in the deleted pings.yaml), .prettierignore, tools/rewriting/Generated.txt, eslint-rollouts, tools/lint extra-js-modules allowlist, tools/ts/mach_commands.py, toolkit/docs/index.md, taskcluster python.yml (nimbus job) and vendor-verify.yml, two doc links, tools/m6e/acceptance.py (the pref row is replaced by a check that `ExperimentManager.sys.mjs` cannot be imported and `ExperimentAPI.enabled` is false).

## integrationTodos

- `./mach build` (not `build faster`): C++ edits, WebIDL removal, Glean codegen changes (metrics_index, pings, metrics.yaml), Telemetry Events.yaml. No configure change.
- No Cargo or Cargo.lock change.
- Run at integration: `./mach xpcshell-test toolkit/components/nimbus/test/unit`, `toolkit/modules/tests/xpcshell/test_firstStartup.js`, `toolkit/components/glean/tests/pytest` (test_yaml_indices), `toolkit/components/telemetry/tests/unit` (the four converted files plus test_PrefMigrationForTOU, test_TelemetryReportingPolicy), `browser/components/backup/tests/xpcshell`, `browser/components/asrouter/tests/xpcshell/test_PreonboardingSplash.js`, urlbar `test_UrlbarPrefs.js` and quicksuggest unit tests, `tools/m6e/run_tests.sh`, `tools/m6e/acceptance.py`, browser tests under preferences, enterprisepolicies, privatebrowsing, customizableui (fireButton), toolkit/modules (Troubleshoot) and browser-about (aboutSupport).
- Optionally regenerate `tools/@types/generated/*` with `./mach ts` outside the shared objdir.

## notDone (kept, with reason)

- Stub and its jar.mn/moz.build entry: 77 staying modules import `resource://nimbus/ExperimentAPI.sys.mjs`. Proof: the stub imports only XPCOMUtils, has no RemoteSettings/fetch/pref-set/Glean/telemetry code, `getEnrollmentMetadata()` is null and `getAllEnrollments()` is `[]` for every feature.
- `app.shield.optoutstudies.enabled` lock in m6e.js, Remote Settings allowlist (`nimbus-desktop-experiments` stays unlisted), `NimbusEnrollments`/`NimbusSyncTimestamps` tables in ProfilesDatastoreService, ASRouter `messaging-experiments` provider, `nimbusUpdate` trigger, `FireButtonExposure`, `AboutWelcomeParent` gate, `browser.aboutwelcome.experimentsGate.*` prefs, `startup.homepage_override_*nimbus*` prefs and What's New Nimbus override: all dead behind the stub; removing them is optional cleanup.
- Vendored third_party/application-services nimbus crates, services/app-services-tools/nimbus-fml, Cargo.toml/Cargo.lock, mots.yaml, `Firefox :: Nimbus Desktop Client` tag: unchanged (D21, not built).
- `tools/@types/generated/*` still list deleted modules (generated, already stale).
- Comment-only mentions of FeatureManifest.yaml (UrlbarPrefs, StartupTelemetry, MessagingExperimentConstants, firefox.js, ActivityStream) and prose docs (BrowserStartup.md, asrouter docs, newtab docs, antitracking docs) left as is.
- Tests belonging to earlier features left for their branches: browser-sync tests, aiwindow, genai, ipprotection head.js, formautofill `browser_ml_nimbus.js`, fxaccounts `test_commands_closetab.js`, update `test_backgroundupdate_glean.js`, plus the `BackgroundTasksUtils.enableNimbus` caller in BackgroundTask_backgroundupdate.
- `NimbusEnrollments` crash annotation (crashreporter step) not touched.
- Tests deleted rather than converted because their subject is an enrolment or a Nimbus-only variable: aboutwelcome navigation/theming/transitions/exposure, windows launch-on-login, shell setDefaultBrowser/setDefaultPDFHandler, default browser prompt, screenshots toolbar button, newtab trainhop browser tests, DAP tests, mailto handler picker, search categorization experiment info, report-broken-site experiment data.
- Unverified: none of the edited tests were run. The aboutwelcome layout/logo/configurable_ui tests now pass screens through `openAboutWelcome(JSON...)` instead of enrolling.
