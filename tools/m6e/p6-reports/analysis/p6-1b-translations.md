# P6.1b Translations: deletion analysis

Base commit cb002d4874. Read-only analysis; nothing in the tree was changed. Integration order 2 (after vpn; genai, ml, sync, fxaccounts, updater, crashreporter, nimbus come later).

## Summary

- Translations is two directories (`toolkit/components/translations`, `browser/components/translations`, 523 tracked files, 253 of them tests) plus a spread of UI, prefs, policy, preferences-pane, Glean, Nimbus, taskcluster and test-harness references.
- No configure change, no Rust crate, no build flag. Hand-written C++ is not required to change; two optional C++ lines exist (both stale allowlist entries, see "Build-system edits"). Removing `toolkit/components/translations/metrics.yaml` regenerates the Glean FOG C++/Rust bindings, so the integrated build needs `./mach build` (not `build faster`).
- The deletion is NOT clean for one piece: `LanguageDetector.sys.mjs` + `cld2/` (CLD2 wasm language identification, local, no network) are used by staying code: the WebExtension APIs `i18n.detectLanguage` and `tabs.detectLanguage` (ExtensionContent.sys.mjs, parent/ext-i18n.js) and Reader Mode (`ReaderMode._assignLanguage`). Recommendation: keep `LanguageDetector.sys.mjs`, `cld2/`, `translations.d.ts` (trimmed) and `tests/unit/test_cld2.js` in `toolkit/components/translations`, delete everything else. Alternative (drop language ID) means stubbing two extension APIs and Reader Mode, and breaking `test_ext_i18n.js`, `test_ext_tabs_detectLanguage.html`; not recommended.
- Four couplings to code that belongs to LATER features must be fixed in this branch (all else just disappears):
  1. ml: `TranslationsParent.getMaxSupportedVersionRecords` (and `isBetterRecordVersion`) is used by `MLEngineParent.sys.mjs` and `ml/content/Utils.sys.mjs`. Move both functions into ml.
  2. ml: `EngineProcess.getTranslationsEngineParent` / `destroyTranslationsEngine` and the call in `Utils.sys.mjs` `uninstallAll`.
  3. ml tests: `ml/tests/browser/head.js`, `browser_eval/*` load translations `shared-head.js` (7.7k lines) and `browser/components/translations/tests/browser/head.js`.
  4. genai AI-controls UI: `OnDeviceModelManager.mjs` imports `TranslationsFeature`; `aiFeatures.mjs`, `main.js`, `languages.mjs`, `Policies.sys.mjs` (AIControls), `StartupTelemetry`, `UITour` carry a Translations feature entry.
- `javascript.options.wasm_moz_intgemm` and `js/src/intgemm` (SpiderMonkey wasm builtin used only by Bergamot) stay; they are privileged-context only. Proposed hardening: lock the pref false in `m6e.js`.

## Files to delete

Directories (git rm -r):

- `browser/components/translations/` (whole directory: `content/` 5 files, `jar.mn`, `moz.build`, `tests/` 168 files incl. `perftest.toml`, `browser_full.toml`, `browser_select.toml`, `browser_telemetry.toml`).
- `toolkit/components/translations/actors/` (6 actors + moz.build)
- `toolkit/components/translations/bergamot-translator/`
- `toolkit/components/translations/content/` (about-translations.*, Translator.mjs, translations-document, translations-engine, worker)
- `toolkit/components/translations/docs/`
- `toolkit/components/translations/tests/browser/`, `tests/scripts/`
- `browser/themes/shared/translations/` (panel.css)

Loose files, toolkit/components/translations: `README.md`, `TranslationsFeature.sys.mjs`, `TranslationsTelemetry.sys.mjs`, `TranslationsUtils.mjs`, `jar.mn`, `metrics.yaml`, `tsconfig.json` (or edit its include, see Must stay), `tests/unit/test_telemetry_sampling.js`.

Loose files elsewhere:

- `browser/components/preferences/translations.js`, `translations.d.ts`
- `browser/components/preferences/config/translations.mjs`
- `browser/components/preferences/dialogs/translations.js`, `translations.xhtml`
- `browser/locales/en-US/browser/translations.ftl`
- `toolkit/locales/en-US/toolkit/about/aboutTranslations.ftl`
- `browser/themes/shared/icons/translations-companion.svg` (KEEP `translations.svg`, see Must stay)
- `browser/components/enterprisepolicies/tests/browser/browser_policy_translateenabled.js`
- `browser/components/urlbar/tests/browser-quickactions/browser_translate.js`
- `services/settings/dumps/main/translations-models.json`, `translations-wasm.json`
- `taskcluster/kinds/fetch/translations-fetch.yml`
- `toolkit/components/ml/tests/browser_eval/browser_eval_translations_full_page_e2e.js`, `browser_eval_synthetic_translation.js` (they drive the Translations UI/engine; ml deletes the rest later)
- Optional: `python/l10n/fluent_migrations/bug_2007194_about_translations_label_attrs.py`, `bug_2020067_about_translations_message_bar_headings.py` (historic migration recipes, not built or loaded; harmless to leave).

Kept in `toolkit/components/translations` (see Must stay): `LanguageDetector.sys.mjs`, `cld2/` (59 files), `translations.d.ts`, `tests/unit/test_cld2.js`, `tests/unit/xpcshell.toml` (drop the telemetry test line), `moz.build` (reduced).

Not touched (vendored, D21): `third_party/application-services/components/remote_settings/dumps/main/translations-models.json`; `remote/test/puppeteer/.../firefox.ts` pref line.

## References from staying code

R = required for build/tests/runtime to keep working. C = cleanup of dangling names, safe to skip but should be done.

### Front end: browser window

| File | What | Action |
|---|---|---|
| `browser/components/moz.build:79` | `DIRS` "translations" | remove (R) |
| `toolkit/components/moz.build:87` | `DIRS` "translations" | KEEP (directory stays for LanguageDetector) |
| `browser/components/BrowserComponents.manifest:124` | category `browser-window-location-change` -> `TranslationsParent.onLocationChange` | remove line (R) |
| `toolkit/modules/ActorManagerParent.sys.mjs` | JSPROCESSACTORS `TranslationsEngine` (~l.97-107, with its comment); JSWINDOWACTORS `Translations` (~l.625-651, comment above it); `JSWINDOWACTORS.AboutTranslations` (~l.817-830) | remove all three blocks (R) |
| `browser/base/content/browser.js` | lazy getter `TranslationsParent` (l.114); `defineLazyScriptGetter` for `SelectTranslationsPanel`, `FullPageTranslationsPanel` (l.219-228); getters `_menuItemForTranslations`, `_moreToolsTranslateMenuItem` (l.2024-2035); block in `XULBrowserWindow._updateElementsForContentType` (l.2473-2484: disabled/hidden toggling of the two menu items) | remove all (R) |
| `browser/base/content/browser.js.globals` | `TranslationsParent` (l.141), `SelectTranslationsPanel`, `FullPageTranslationsPanel` (l.177-178) | remove (R for lint) |
| `browser/base/content/browser-init.js` | `_translationsEnabledStateObserver` (l.20-62); `onLoad`: two `addEventListener("TranslationsParent:...")`, TabSelect listener, `addTabsProgressListener(FullPageTranslationsPanel)` (l.299-310); `TranslationsParent.ensurePrefObservers()` + `addObserver(... "translations:enabled-state-changed")` (l.511-515); matching `removeObserver` in `onUnload` (l.1142-1145) | remove all (R) |
| `browser/base/content/browser-sets.js` | `lazy.TranslationsParent` getter (l.10); `case "cmd_translate"` and `case "cmd_openAboutTranslations"` (l.151-159) | remove (R) |
| `browser/base/content/browser-sets.inc.xhtml:57-58` | `<command id="cmd_translate">`, `cmd_openAboutTranslations` | remove (R) |
| `browser/base/content/appmenu-viewcache.inc.xhtml` | `#appMenu-translate-button` (l.171-174); `#appmenu-abouttranslations-button` in `appmenu-moreTools` (l.496-499) | remove (R) |
| `browser/base/content/browser-context.inc.xhtml` | `<menuitem id="context-translate-selection">` (l.393-394); attribute value `context-translate-selection` inside `intended-duplicate-accesskey` of `context-sendlinktodevice` (l.145) | remove item; edit attribute to `spell-undo-add-to-dictionary` only (R) |
| `browser/base/content/browser-context.js:215-217` | `case "context-translate-selection"` | remove (R) |
| `browser/base/content/nsContextMenu.sys.mjs` | lazy `TranslationsParent`, `TranslationsUtils` (l.36-38); `#translationsLangPairPromise` (l.118-125); call `this.showTranslateSelectionItem()` (l.927); methods `openSelectTranslationsPanel`, `localizeTranslateSelectionItem`, `#getTextToTranslate`, `showTranslateSelectionItem` (l.2670-2806) | remove all (R) |
| `browser/base/content/main-popupset.inc.xhtml` | `#include` of `selectTranslationsPanel.inc.xhtml`, `fullPageTranslationsPanel.inc.xhtml` (l.927-928); `<menupopup id="full-page-translations-panel-settings-menupopup">` and `select-translations-panel-settings-menupopup` (l.1049-1085) | remove (R, build breaks on missing include) |
| `browser/base/content/main-popupset.js` | cases for `translations-panel-*` and `select-translations-panel-*` command routing (l.461-487); `popupshown`/`popuphidden` cases for `full-page-translations-panel-settings-menupopup` (l.756-757, 777-778) | remove (R) |
| `browser/base/content/navigator-toolbox.inc.xhtml:398-406` | `<hbox id="translations-button">` and children | remove (R) |
| `browser/base/content/navigator-toolbox.js` | `#translations-button` in two selector lists (l.225, 357); two `case "translations-button"` (l.316, 432) | remove (R) |
| `browser/base/content/browser.xhtml` | `<link rel="stylesheet" href="chrome://browser/skin/translations/panel.css">` (l.49); `<link rel="localization" href="browser/translations.ftl">` (l.89) | remove (R) |
| `browser/base/content/webext-panels.xhtml:36` | localization link `browser/translations.ftl` | remove (R) |
| `browser/themes/shared/jar.inc.mn:190, 294` | `translations/panel.css`, `translations-companion.svg` | remove both lines; keep l.295 `translations.svg` (R) |
| `browser/themes/shared/urlbar.css:1098-1160` | "Translations button" rules incl. the `prefers-contrast` block | remove (C) |
| `browser/themes/shared/preferences/preferences.css:1439-1473` | `#translations-manage-*`, `.translations-manage-language`, `.translations-download-language-error` | remove (C) |
| `browser/components/urlbar/QuickActionsLoaderDefault.sys.mjs` | lazy `TranslationsParent` (l.18); `translate:` action (l.294-315) | remove (R) |
| `browser/components/uitour/UITour.sys.mjs:1684-1687` | `translations` key in `aiControls` reply | remove key (C); also `uitour/test/browser_UITour_aiControls.js` expectation |
| `browser/components/StartupTelemetry.sys.mjs:364` | `"browser.ai.control.translations": "translations"` in `AI_CONTROL_FEATURES` | remove (C); update `browser/components/tests/unit/test_startupTelemetry_aiControlBlocking.js` (l.13, 63, 85, 99, 108, 131, 153, 170) |

### Front end: preferences

| File | What | Action |
|---|---|---|
| `browser/components/preferences/jar.mn:11, 51` | `translations.js`, `config/translations.mjs` | remove (R) |
| `browser/components/preferences/dialogs/jar.mn:39` | `dialogs/translations.xhtml` (note: `dialogs/translations.js` has no jar line; it is not packaged) | remove (R) |
| `browser/components/preferences/main.inc.xhtml` | `<script ... translations.js>` (l.8); legacy `<vbox id="translationsGroup">` block (l.104-129); `<setting-group groupid="translations">` (l.132) | remove (R) |
| `browser/components/preferences/main.js` | lazy `TranslationsParent`, `TranslationsUtils` (l.54-56); settings `legacyTranslationsVisible` (l.309-315); setting `aiControlTranslations` (l.395-398) and pref row `browser.ai.control.translations` (l.118); `gMainPane.initTranslations()` call (l.625) and the whole method with the nested `TranslationsState`/`TranslationsView` (l.769-1196); `initSettingGroup("translations")` (l.640); `showTranslationsSettings` (l.1479-1483); `destroy()` cleanup of `_translationsView` (l.1546-1550) | remove (R) |
| `browser/components/preferences/preferences.js` | `languages.groupIds` entry `"translations"` (l.365); `translations:` pane (l.514-524) | remove (R). `iconSrc: translations.svg` at l.361 stays |
| `browser/components/preferences/preferences.xhtml` | `<link rel="localization" href="browser/translations.ftl">` (l.74); hidden `paneTranslations` nav button (l.210) | remove (R). `iconsrc=translations.svg` l.175 stays |
| `browser/components/preferences/config/languages.mjs` | pref row `browser.translations.automaticallyPopup` (l.22); settings `offerTranslations` (l.813-819), `translationsManageButton` (l.836-845); group `translations` (l.934-952) | remove (R) |
| `browser/components/preferences/config/LegacyPaneMappings.mjs:60-67` | hash redirects `general-fxtranslations`, `general-translations` | remove (C); then edit `tests/browser_sub_pane_navigation_selection.js` (~l.230-275, uses `#general-translations`, `#languages-translations`) |
| `browser/components/preferences/tests/languages/browser_languages_pane.js:10` | `"translations"` in the expected group list | remove entry (R for test) |

### Policies, prefs, remote, harness

| File | What | Action |
|---|---|---|
| `browser/components/enterprisepolicies/Policies.sys.mjs` | `AIControls` feature tuple `"Translations"` (l.165-169); whole `TranslateEnabled` policy (l.3858-3873); in `UserMessaging.FeatureRecommendations` the `mostRecentTargetLanguages` block (l.3886-3907, incl. its comment) | remove (R: pref no longer exists) |
| `browser/components/enterprisepolicies/schemas/policies-schema.json` | `Translations` in `AIControls` example (l.78) and `properties` (l.108); description text l.71 ("Translations,"); `TranslateEnabled` (l.4205-4215) | remove (R: schema/handler consistency tests) |
| `browser/locales/en-US/browser/policies/policies-descriptions.ftl:271` | `policy-TranslateEnabled` | remove (R) |
| `browser/components/enterprisepolicies/tests/browser/browser.toml:189` | `browser_policy_translateenabled.js` entry | remove (R) |
| `browser/components/enterprisepolicies/tests/xpcshell/test_simple_pref_policies.js:1364-1412` | `Translations` in two AIControls cases | remove those keys (R) |
| `browser/app/profile/firefox.js:2479-2488` | `browser.translations.enable`, `.select.enable`, `.quickAction.enabled` + comments | remove (R) |
| `browser/app/profile/firefox.js:3476-3479` | `extensions.translations.disabled` (NIGHTLY block + comment) | remove (C) |
| `browser/app/profile/m6e.js:134, 148-149` | `browser.ai.control.translations`, `browser.translations.enable`, `.select.enable` locked | remove 3 lines (R: otherwise orphan locked prefs). Edit the comment at l.130-131 only if it names Translations (it does not) |
| `modules/libpref/init/all.js:543-544` | `toolkit.telemetry.translations.logLevel` + comment | remove (R) |
| `modules/libpref/init/all.js:3577-3621` | all `browser.translations.*` prefs + comments, and `browser.ai.control.translations` (l.3621) | remove (R) |
| `remote/shared/RecommendedPreferences.sys.mjs:213-214` | `browser.translations.enable` false + comment | remove (C) |
| `testing/marionette/client/marionette_driver/geckoinstance.py:68-69` | same | remove (C) |
| `toolkit/components/nimbus/FeatureManifest.yaml:2544-2593` | features `fullPageTranslation`, `fullPageTranslationAutomaticPopup`, `selectTranslation`, `translationsQuickAction` | remove (R: they set prefs that no longer exist). Nimbus is a later feature; remove just these four entries |
| `tools/@types/subs/FeatureManifest.sys.d.mts:2455-2515` | generated types of those features | remove or regenerate (C) |
| `toolkit/components/messaging-system/lib/SpecialMessageActions.sys.mjs:15` | `"translations"` in `ALLOWED_ABOUT_PAGES` | remove (C; page is already unregistered) |
| `toolkit/components/glean/metrics_index.py:98` | `toolkit/components/translations/metrics.yaml` | remove (R) |
| `toolkit/components/glean/tags.yaml:400-401` | tag `Firefox :: Translations` | remove (C; only metrics.yaml uses it) |
| `tools/@types/generated/lib.gecko.glean.d.ts` (l.7620-7690), `lib.gecko.modules.d.ts` (7 lines; keep the `LanguageDetector` line), `tspaths.json` (l.457-458, 817-836, 895-896, 5642) | generated | regenerate or hand-edit (C) |
| `services/settings/dumps/main/moz.build:34-35` | the two dump files | remove (R) |
| `taskcluster/kinds/fetch/kind.yml:22` | include of `translations-fetch.yml` | remove (R) |
| `taskcluster/kinds/mochitest/kind.yml:652-681` | `mochitest-browser-translations` job (uses `tr8ns.*` fetches) | remove (R) |
| `taskcluster/test_configs/test-sets.yml:211, 244, 285, 366, 462, 501` | `- mochitest-browser-translations` | remove (R) |
| `taskcluster/kinds/perftest/{linux,macosx-aarch64,windows11-24h2}.yml` | jobs `tr8ns-perf-tiny`, `-basememory`, `-base` | remove (R) |
| `python/mozperftest/perfdocs/config.yml:66-71` and `testing/perfdocs/generated/mozperftest.md:276-340` | perf doc entries | remove (C) |
| `python/mozbuild/mozbuild/schedules.py:75` | `mochitest-browser-translations` | remove (C) |
| `testing/mozbase/moztest/moztest/resolve.py:263-274, 557`; `testing/mozharness/configs/unittests/{linux,mac,win}_unittest.py`; `testing/mozharness/mach_commands.py:97-101`; `testing/mozharness/mozharness/mozilla/testing/per_test_base.py:229-233`; `testing/mozharness/scripts/desktop_unittest.py:967`; `testing/runtimes/fetch-manifest-data.js:137`; `testing/testinfo.py:1128` | suite/subsuite plumbing for `mochitest-browser-translations` | remove (C; dead once no test uses `subsuite = "translations"`) |
| `tools/m6e/acceptance.py:130, 138-141, 204, 221` | `REMOVED_ELEMENTS["Translations"]`, `REMOVED_PREFS["Translations"]`, `"translations"` in `REMOVED_ABOUT` and `REMOVED_SETTINGS_GROUPS` | delete `REMOVED_PREFS["Translations"]` (R: the check requires the pref to exist and be locked; it will not exist). Keep `REMOVED_ELEMENTS`, `REMOVED_ABOUT`, `REMOVED_SETTINGS_GROUPS` entries (still valid: absent elements pass) |
| `tools/m6e/merge/nightly-prefs-reviewed.txt:51` | `extensions.translations.disabled` | remove line (C). Keep l.53 `javascript.options.wasm_moz_intgemm` |
| `tools/m6e/merge/check-helpers.py:512` | `"translations"` in `DORMANT_PAGES` | keep (page must stay unregistered) |
| `docshell/base/nsAboutRedirector.cpp:190`, `docshell/build/components.conf:44` | M6E comments "no about:translations" | keep (comment only; avoids a C++ rebuild) |

### Localization strings in staying FTL files

| File | Messages | Action |
|---|---|---|
| `browser/locales/en-US/browser/appmenu.ftl` | `appmenuitem-translate` (l.54), `appmenu-abouttranslations` (l.413) | remove (C) |
| `browser/locales/en-US/browser/browser.ftl` | `quickactions-translate`, `quickactions-cmd-translate` (l.385-387, with comment) | remove (C) |
| `browser/locales/en-US/browser/preferences/preferences.ftl` | `fx-translate-web-pages`, `translate-exceptions` (l.491-495, unused already), all `settings-translations-*` (l.497-583, 33 messages), `preferences-ai-controls-translations-control`, `-translations-more-link`, `preferences-ai-controls-block-confirmation-translations` (l.2622-2625, 2669) | remove (C) |
| `toolkit/locales/en-US/toolkit/branding/brandings.ftl:26` | `-translations-brand-name` | remove (C; only used by `fx-translate-web-pages`) |

FTL file ids from the deleted files that nothing else uses: prefixes `translations-panel-*`, `translations-manage-*`, `translations-settings-*`, `select-translations-panel-*`, `urlbar-translations-*`, `main-context-menu-translate-*`, `about-translations-*`.

### Lint, ignore and ownership entries for paths that are deleted

`.clang-format-ignore:196` (cld2: KEEP), `.gitignore:135`, `.hgignore:121` (bergamot thirdparty: remove), `.prettierignore` l.966-967 (remove), l.1231 and 1541 (cld2: KEEP), l.1542-1543 (bergamot: remove), `eslint-file-globals.config.mjs:130-131` (remove), `eslint-ignores.config.mjs:285` (cld2: KEEP), `eslint-rollouts.config.mjs:213-214, 363` (remove), `:454` (`toolkit/components/translations/**`: KEEP while the directory exists), `stylelint-rollouts.config.js:260` (remove), `tools/lint/extra-js-modules/allowlist.txt:520-522` (KEEP), `tools/rewriting/ThirdPartyPaths.txt:196` (cld2: KEEP), `:197-198` (remove), `mots.yaml:5008-5030` (module "translation": keep, or rename scope to the remaining LanguageDetector paths and drop `browser/components/translations/**/*`).

### Docs

`toolkit/docs/index.md:35` (toctree `components/translations/index`: remove, R for docs build), `toolkit/components/ml/docs/architecture.md:7-9` (link to translations doc: reword, ml-owned), `dom/docs/ipc/process_model.md:206` (mentions Bergamot: reword, C).

### From later features (must be handled in this branch)

| Owner | File | What | Action |
|---|---|---|---|
| ml | `toolkit/components/ml/actors/MLEngineParent.sys.mjs` | `@import WasmRecord` from `../../translations/translations.d.ts` (l.21); lazy `TranslationsParent` (l.30); comment l.212 "kept alive by TranslationsEngine"; call `getMaxSupportedVersionRecords` in `#getWasmArrayRecord` (l.612) | Move `getMaxSupportedVersionRecords` and `isBetterRecordVersion` (TranslationsParent l.3430-3548; they only need `chaosMode`, `VERIFY_SIGNATURES_FROM_FS`-equivalent `verifySignature` flag, `Services.vc`, the lazy console) into ml (suggest `RemoteSettingsManager` in `ml/content/Utils.sys.mjs`). Drop `chaosMode`; keep `syncIfEmpty: true`, `loadDumpIfNewer: false`. Move `Attachment` and `WasmRecord` typedefs into `toolkit/components/ml/ml.d.ts` and fix the `@import` (R) |
| ml | `toolkit/components/ml/content/Utils.sys.mjs` | lazy `TranslationsParent` (l.23); `getRemoteData` call (l.1022); `uninstallAll` calls `EngineProcess.destroyTranslationsEngine()` (l.1769) | switch to the moved helper; delete the destroy call (R) |
| ml | `toolkit/components/ml/content/EngineProcess.sys.mjs` | `@import TranslationsEngineParent` (l.438); `getTranslationsEngineParent` (l.1224-1228); `destroyTranslationsEngine` (l.1347-1355); doc comment l.1192 | remove (R) |
| ml | `toolkit/components/ml/AIFeature.sys.mjs:36` | doc comment names `TranslationsFeature` | reword (C) |
| ml tests | `ml/tests/browser/head.js` (l.6 reference path, l.50-55 `loadSubScript` of translations `shared-head.js`), `ml/tests/browser_eval/{head.js, eval.toml}` | ml only uses 4 names from translations `shared-head.js`: `_remoteSettingsMockId`, `createAttachmentMock`, `waitForCondition`, (`normalizePathForOS` and `pathExists` are already redefined locally). Copy those into `ml/tests/browser/shared-head.js`, remove the `loadSubScript` and the `<reference>` line. In `browser_eval/`: delete the two Translations eval tests, drop the translations lines from `eval.toml` and the `loadSubScript` of the translations `head.js` from `head.js` (and `clearDirtyPrefs` pref line) (R) |
| genai | `browser/components/preferences/OnDeviceModelManager.mjs` | lazy `TranslationsFeature` (l.23-24); enum `Translations` (l.34); `featurePrefs` row (l.57); `getAIFeature` and `getFeaturePref` cases (l.142-143, 168-169) | remove (R) |
| genai | `browser/components/preferences/config/aiFeatures.mjs` | `aiControlTranslationsSelect` references (l.68, 199 block-dialog bullet, 554-562 setting, 1420-1437 UI item incl. `#general-translations` link) | remove (R) |
| genai | `browser/components/preferences/tests/aiFeatures/{browser_aiFeatures.js, browser_aiFeatures_block_dialog.js, browser_aiFeatures_telemetry.js, browser_aiFeatures_visibility.js}` | Translations pref sets and assertions | edit out (R for tests) |
| genai | `browser/components/aiwindow/models/PreferencesNavMap.sys.mjs:84-88` and `scripts/generate_prefs_nav_map.py:53` | nav-map entry for `about:preferences#translations` | remove (C) |
| sync | `browser/base/content/test/browser-sync/browser_contextmenu_sendpage.js` (l.114-115, 144, 376-377, 406) | expects `context-translate-selection` | remove `expectTranslateSelection` logic (R for test) |

## References from earlier features (ignore)

vpn (order 1) is deleted before this branch. I found no reference from vpn code to Translations. `browser/components/preferences/dialogs/vpnSiteRule.js` and `ipprotection-button` only appear in shared lists (`navigator-toolbox.js`) next to the translations lines; those lines are edited here anyway.

## Prefs/metrics/strings to remove

Prefs (all in `modules/libpref/init/all.js` unless noted; none are in StaticPrefList.yaml):

`browser.translations.enable`, `.select.enable`, `.quickAction.enabled` (firefox.js), `.logLevel`, `.mostRecentTargetLanguages`, `.alwaysTranslateLanguages`, `.neverTranslateLanguages`, `.useHTML`, `.automaticallyPopup`, `.useLexicalShortlist`, `.simulateUnsupportedEngine`, `.chaos.errors`, `.chaos.timeoutMS`; `toolkit.telemetry.translations.logLevel`; `browser.ai.control.translations` (all.js + m6e.js); `extensions.translations.disabled` (firefox.js); `browser.settings-redesign.translations.enabled` (read lazily by `srdSectionEnabled("translations")`, never defined; vanishes with the pane). Also the site permission type `"translations"` (never-translate sites, stored in the permission DB; no registration to remove, existing rows are harmless).

Glean (file `toolkit/components/translations/metrics.yaml`): categories `translations`, `translations.panel`, `translations.select_translations_panel`, `translations.about_translations_page`, `translations.feature` (JS: `Glean.translations*`). No use outside the feature (checked `Glean.translations`, `Glean.translationsPanel`, etc. across the tree). No Events.yaml/Scalars.yaml/Histograms.json entries. No pings.

Remote Settings collections `translations-models-v2`, `translations-wasm-v2` (code) and `translations-models`, `translations-wasm` (bundled dumps): not in `M6E_ALLOWED_MAIN_COLLECTIONS` in `services/settings/Utils.sys.mjs`, so no allowlist edit. Delete the dumps only.

Strings: see the FTL tables above. about: name `translations`; actor names `Translations`, `AboutTranslations`, `TranslationsEngine`; commands `cmd_translate`, `cmd_openAboutTranslations`; ids `translations-button*`, `appMenu-translate-button`, `appmenu-abouttranslations-button`, `context-translate-selection`, `full-page-translations-panel-*`, `select-translations-panel-*`; notification topic `translations:enabled-state-changed`, `translations:pref-changed`; event names `TranslationsParent:LanguageState`, `TranslationsParent:OfferTranslation`; Nimbus features listed above; policy `TranslateEnabled`; CSS attribute `translationsactive`.

## Build-system edits

Front-end (jar.mn, FTL, JS, XHTML, CSS, prefs): `./mach build faster` is enough for these on their own.

moz.build/manifests (re-run of the build backend, not a re-configure):

- `browser/components/moz.build` DIRS; `toolkit/components/translations/moz.build` (reduce to `EXTRA_JS_MODULES.translations` of cld-worker.js, cld-worker.js.mem, LanguageDetector.sys.mjs plus `XPCSHELL_TESTS_MANIFESTS += ["tests/unit/xpcshell.toml"]`; drop `JAR_MANIFESTS`, `SPHINX_TREES`, `DIRS += ["actors"]`, `BROWSER_CHROME_MANIFESTS`, the docs `Files`).
- Delete `toolkit/components/translations/jar.mn`, `actors/moz.build`, `browser/components/translations/jar.mn`+`moz.build`.
- `browser/components/preferences/jar.mn`, `dialogs/jar.mn`, `browser/themes/shared/jar.inc.mn`.
- `services/settings/dumps/main/moz.build`.
- BROWSER_CHROME manifests: `browser/components/enterprisepolicies/tests/browser/browser.toml`, `browser/components/urlbar/tests/browser-quickactions/browser.toml` (`browser_translate.js`), `toolkit/components/ml/tests/browser_eval/eval.toml`.

Configure-time: none. `mozconfig` unchanged. (Optional, NOT recommended now: `ac_add_options --disable-wasm-moz-intgemm` would drop `js/src/intgemm` and `third_party/gemmology` from the build; it is a configure change that rebuilds everything, so batch it with another configure change if wanted.)

C++/Rust: no hand-written change required. The build must run `./mach build` (not `faster`) because deleting `metrics.yaml` regenerates the Glean FOG bindings (GleanMetrics C++/Rust). Optional C++ line edits in `dom/security/nsContentSecurityUtils.cpp`: l.1265 `chrome://browser/content/preferences/dialogs/translations.xhtml` in `sStyleSrcUnsafeInlineAllowList` (stale after deletion; harmless to leave). Keep l.608 `resource://gre/modules/translations/cld-worker.js` in `evalAllowlist` because the CLD2 worker stays.

StaticPrefList.yaml: untouched.

## Tests to delete or edit

Delete:

- `browser/components/translations/tests/` (168 files).
- `toolkit/components/translations/tests/browser/` (79 files incl. `shared-head.js`, `translations-test.mjs`, HTML fixtures), `tests/scripts/`, `tests/unit/test_telemetry_sampling.js`.
- `browser/components/enterprisepolicies/tests/browser/browser_policy_translateenabled.js`.
- `browser/components/urlbar/tests/browser-quickactions/browser_translate.js`.
- `toolkit/components/ml/tests/browser_eval/browser_eval_translations_full_page_e2e.js`, `browser_eval_synthetic_translation.js`.

Edit (remove Translations expectations):

- `browser/base/content/test/browser-contextMenu/browser_contextmenu.js` (const `hasSelectTranslations` l.59-61 and 15 spread uses), `contextmenu_common.js:98` (allowed duplicate access key list: drop `context-translate-selection`).
- `browser/base/content/test/browser-performance/browser_startup_content.js:70-72` (drop the comment and `TranslationsChild.sys.mjs`; leave `LanguageDetector.sys.mjs` since it stays and may still load in content).
- `browser/base/content/test/browser-sync/browser_contextmenu_sendpage.js`.
- `browser/components/preferences/tests/aiFeatures/*` (4 files), `languages/browser_languages_pane.js`, `browser_sub_pane_navigation_selection.js`.
- `browser/components/enterprisepolicies/tests/xpcshell/test_simple_pref_policies.js`.
- `browser/components/tests/unit/test_startupTelemetry_aiControlBlocking.js`; `browser/components/uitour/test/browser_UITour_aiControls.js`.
- `toolkit/components/ml/tests/browser/head.js`, `browser_eval/head.js`, `browser_eval/eval.toml`.
- `browser/components/sessionstore/test/unit/data/sessionstore_complete.json` has a saved form field `aiControlTranslationsSelect`: fixture data, no action.
- `taskcluster/test/params/*.yml`: recorded task graphs; no action.
- Python tests: `python/mozperftest/mozperftest/tests/test_eval.py` and `data/.../mochitest-metrics-sample.json` mention `TranslationsBleu` metric names from ml eval code; unrelated to the deletion, no action.

Tests that must keep passing (affected by the kept LanguageDetector): `toolkit/components/translations/tests/unit/test_cld2.js`, `toolkit/components/extensions/test/xpcshell/test_ext_i18n.js`, `toolkit/components/extensions/test/mochitest/test_ext_tabs_detectLanguage.html`, `test_ext_all_apis`, `toolkit/components/reader/tests`.

## Must stay (with proof)

1. `toolkit/components/translations/LanguageDetector.sys.mjs`, `cld2/`, `translations.d.ts` (only `DetectionResult`/`MultilingualSection` needed once `WasmRecord` moves to ml; remove the rest or leave as is), `tests/unit/test_cld2.js`, reduced `moz.build`, `tsconfig.json` (change `include` to `["LanguageDetector.sys.mjs", "translations.d.ts"]` or delete it, since the TS check currently covers `content/translations-document.sys.mjs`).
   - Why: real consumers, not a dormant feature: `ExtensionContent.handleDetectLanguage` (browser.tabs.detectLanguage), `parent/ext-i18n.js` (browser.i18n.detectLanguage), `ReaderMode._assignLanguage`.
   - Reachability: no network; the worker `resource://gre/modules/translations/cld-worker.js` and its `.mem` file are packaged locally; the only entry points are the two WebExtension API calls (an installed extension with the API, not web content) and Reader Mode parsing in the parent. No JSWindowActor, no about: page, no pref-gated UI, no Remote Settings. `LanguageDetector` has no dependency on any deleted file (`Timer.sys.mjs` only).
   - If M6E later decides to drop language ID: remove the three consumers and these files together; out of scope here.
2. `browser/themes/shared/icons/translations.svg` and its `jar.inc.mn` line: used as the icon of the Languages pane (`preferences.js:361`, `preferences.xhtml:175`, `config/languages.mjs:938` is removed with the group). Not feature code. Optionally rename later.
3. `js/src/intgemm/`, `third_party/gemmology`, `javascript.options.wasm_moz_intgemm`, `WebAssembly.mozIntGemm` (SpiderMonkey, `ENABLE_WASM_MOZ_INTGEMM` in `js/moz.configure`). Only Bergamot used it (`grep intgemm` finds consumers only in `toolkit/components/translations` and `js/`; nothing in `toolkit/components/ml`). It is first-party SpiderMonkey with its own jit-tests; deletion is a js/ C++ change with no user benefit.
   - Proof of unreachability: the wasm feature flag predicate is `IsPrivilegedContext(cx)` (`js/public/WasmFeatures.h`, entry `MozIntGemm`); `dom/tests/mochitest/integer-gemm/test_unavailable_for_webpage.html` asserts `"mozIntGemm" in WebAssembly` is false for web pages. After this change no chrome code calls it. Proposed hardening: add `pref("javascript.options.wasm_moz_intgemm", false, locked);` to `browser/app/profile/m6e.js` (the pref is `set_spidermonkey_pref: startup`, so it takes effect at startup). Keep the `nightly-prefs-reviewed.txt` line.
4. The `inference` remote type and process: still hosts MLEngine until ml is deleted. Only the `TranslationsEngine` JSProcessActor registration goes.
5. Remote Settings: nothing to keep or lock; the translations collections were never in the M6E allowlist (`M6E_ALLOWED_MAIN_COLLECTIONS`), so no sync or attachment download could happen even before deletion.
6. `docshell/base/nsAboutRedirector.cpp` and `docshell/build/components.conf` comments about `about:translations`, and `REMOVED_ABOUT`/`DORMANT_PAGES` in `tools/m6e`: keep as regression guards that the page stays unregistered.

## Risks and open questions

- Decision needed: keep `LanguageDetector`/`cld2` (recommended) or remove language ID and stub the two extension APIs plus Reader Mode. Keeping means `toolkit/components/translations` and the `mots.yaml` entry survive as a small directory.
- ml coupling (items 1-3 under "from later features") is the biggest risk. `getMaxSupportedVersionRecords` is used by the ML wasm and model fetch path; copying it wrongly breaks ML engine startup. ml is deleted next-but-one, so a straight copy with no refactor is the right level of effort. Tests in `ml/tests/browser` rely on 4 helpers from the 7.7k-line translations `shared-head.js`; verify by running the list of names against `ml/tests` before the copy (my scan found only `_remoteSettingsMockId`, `createAttachmentMock`, `waitForCondition`).
- AI-controls plumbing (genai-owned): minimum for a working build is `OnDeviceModelManager.mjs` + `aiFeatures.mjs`. The pref-name leftovers (UITour, StartupTelemetry, Policies AIControls, PreferencesNavMap) are harmless if skipped, but the Policies `AIControls.Translations` handler would create a pref nobody reads, and `test_simple_pref_policies.js` would still pass. I recommend doing all of them in this branch so the later genai branch has fewer conflicts; rebase conflicts in `aiFeatures.mjs` and `main.js` are likely.
- `main.js` removal (`initTranslations` ~430 lines) and `preferences.js` pane removal change the settings-pane structure; run `browser/components/preferences/tests` (languages, sub-pane navigation, search) after the build.
- The `mots.yaml`, lint-config and ignore-file edits overlap with the P6.2 stale-lint-entry cleanup (324 entries); entries for deleted paths listed above should be handled here or deduplicated against P6.2.
- Taskcluster/test-harness edits (suite `mochitest-browser-translations`, `tr8ns` fetches, perftest jobs) are not exercised by the local build but should go in the same commit so the repo has no dangling suite names. `taskcluster/test/params/*.yml` are recorded fixtures; leave.
- Generated type files under `tools/@types/generated/` need `./mach ts build` or hand edits; skipping them leaves stale paths but breaks nothing at runtime.
- Cannot confirm without a build: that deleting `main-popupset.inc.xhtml` includes and the two `*.inc.xhtml` files leaves no other `#include` of them (git grep shows only `main-popupset.inc.xhtml`).
- Existing profiles keep stale prefs (`browser.translations.*` user values) and "translations" permission rows; harmless, and M6E has no pref migration for removed features.

## Critic addendum

Method: re-searched with basenames, resource/chrome URLs, pref prefixes, FTL ids (all 106 ids of `translations.ftl` and `aboutTranslations.ftl` checked against the rest of the tree), element/command ids, taskcluster fetch names (`tr8ns.*`), Glean/tag names, docs and `tools/*`. Most of the report held up. Misses and corrections follow.

### Missed references (file, reference, action)

| File | Reference | Action |
|---|---|---|
| `taskcluster/kinds/test/misc.yml:166-171` (job `test-verify`, `fetch:` list) | `tr8ns.inference`, `tr8ns.T.esen.lex/.model/.vocab` | Remove the four `tr8ns.*` entries (keep `ort.jsep.wasm`). R: after `translations-fetch.yml` is deleted, taskgraph generation fails on an unknown fetch name. The report only listed `mochitest/kind.yml` and `perftest/*.yml`. |
| `tools/tryselect/selectors/perfselector/classification.py:749-770` | preset `"Translations"` with query `'perftest 'tr8ns` | Remove the preset (C; matches no job once the `tr8ns-perf-*` jobs go). |
| `tools/lint/fluent-lint.yml:21` | `generic-names: - Translations` | Remove together with `-translations-brand-name` in `brandings.ftl` (C). |
| `toolkit/components/ml/moz.build:36` and whole `toolkit/components/ml/tests/browser_eval/` | `eval.toml` listed in `BROWSER_CHROME_MANIFESTS`. The directory holds only `browser_eval_synthetic_translation.js`, `browser_eval_translations_full_page_e2e.js`, `eval.toml`, `head.js`, `example_page_es.html`. | The report says to delete the two tests and "drop lines" from `eval.toml`/`head.js`. That leaves a manifest with no tests and a `head.js` that only loads deleted files. Delete the whole directory and remove the `tests/browser_eval/eval.toml` line from `ml/moz.build` (R for a clean build; ml-owned file, so list it under "from later features"). `toolkit/components/ml/eval/mach_commands.py:58` talks about browser_eval folders generically: no change. |
| `toolkit/components/ml/eval/evals.py:105-300` | classes `TranslationsBleu`, `TranslationsChrf`, `TranslationsLlmJudge`, `_TranslationsSacreBleu` | Keep for now (generic eval metrics, no reference to deleted code; used by `python/mozperftest` tests). Leave to the ml branch. |
| `toolkit/components/messaging-system/schemas/SpecialMessageActionSchemas/index.md:77` | lists `translations` as an about page | Reword with the `ALLOWED_ABOUT_PAGES` edit (C). |
| `browser/components/ProfileDataUpgrader.sys.mjs:739-748` | migration `< 148` uninstalls `firefox-translations-addon@mozilla.org` | KEEP. It removes a legacy add-on id and calls nothing from the feature. |
| `toolkit/components/glean/tags.yaml` | `Firefox :: Translations` is only used by the translations `metrics.yaml`; kept `toolkit/components/translations/moz.build` still has `BUG_COMPONENT = ("Firefox","Translations")` | No conflict (moz.build bug components are independent of Glean tags). Removing the tag is safe. |
| `dom/security/nsContentSecurityUtils.cpp:608` and `:1265` | `cld-worker.js` eval allowlist (keep) and `dialogs/translations.xhtml` style allowlist (stale) | Confirmed as in the report. |
| `python/mozperftest/perfdocs/config.yml:66-71` | the report names this; note the generated doc `testing/perfdocs/generated/mozperftest.md` has the section header `## browser/components/translations/tests/browser` at l.276 | Same edit; already covered. |
| `M6E_IMPLEMENTATION_PLAN.md:575, 710`, `m6e_browser_agent_instructions.md` | decision log and plan mention `about:translations` and the P6.1b row | Update plan progress log and decision text at commit time (project convention). |
| `docs/code-quality/typescript/bestPractices.md:142` | example snippet `@import { LangTags } from "./translations.d.ts"` | No action: an illustrative snippet, not a path reference. |

### Corrections and weak points in the report

1. `toolkit/components/translations/tests/unit/xpcshell.toml` currently has `head = ""`, `firefox-appdir = "browser"` and no `support-files`; after dropping the telemetry test it is fine as stated. No further edit needed.
2. The report's claim "StaticPrefList.yaml: untouched" is right for the translations prefs, but the proposed hardening pref `javascript.options.wasm_moz_intgemm` IS defined in `modules/libpref/init/StaticPrefList.yaml:10257` (value `@IS_NIGHTLY_BUILD@` when `ENABLE_WASM_MOZ_INTGEMM`, `set_spidermonkey_pref: startup`). Locking it false in `m6e.js` works, but note it defaults true on a nightly-channel build, so the lock is not redundant. Also `tools/m6e/merge/new-nightly-prefs.py:126` lists `ENABLE_WASM_MOZ_INTGEMM`; keep it.
3. `browser/components/preferences/config/languages.mjs:816-818, 838-844` depend on setting id `aiControlTranslations` (defined in `main.js`). Deleting the `main.js` setting without removing both `languages.mjs` settings breaks the config. The report lists both edits, so no change, but they must land together.
4. `toolkit/components/ml/tests/browser/head.js` loads translations `shared-head.js` at l.50-55. The report's list of helpers actually used by ml tests (`_remoteSettingsMockId`, `createAttachmentMock`, `waitForCondition`, `pathExists`, `normalizePathForOS`) matches an independent scan; `click` matched only a JSON fixture. `waitForCondition` is already a global in mochitest (head.js use needs no copy); verify before copying.
5. `browser/base/content/test/browser-performance/browser_startup_content.js`: `LanguageDetector.sys.mjs` is loaded in content only by `ExtensionContent`; keep the line but reword the comment "Translations code which may be preffed on" (it will be wrong once the feature is gone).
6. Searches that found nothing beyond the report (so the report's "none" claims hold): `devtools/`, `remote/` (only `RecommendedPreferences`), `testing/profiles`, `testing/marionette` (only `geckoinstance.py`), `toolkit/components/telemetry` (no Events/Scalars/Histograms), `browser/installer`, `security/manager`, `services/settings/Utils.sys.mjs` (no translations collection in allowlist), `mobile/`, `dom/ipc`, `toolkit/components/extensions` outside the three LanguageDetector consumers, customizable-UI widget lists, `browser/themes/{linux,osx,windows}`, `Cargo.toml`/`Cargo.lock`, jar manifests other than those listed. Fluent ids from the two deleted FTL files are unused outside deleted/edited files.
7. Verified claims: LanguageDetector consumers (`ExtensionContent.sys.mjs:1412`, `parent/ext-i18n.js:41`, `ReaderMode.sys.mjs:450`), `LanguageDetector.sys.mjs` imports only `Timer.sys.mjs`, `dialogs/translations.js` has no jar line, `bergamot-translator/` holds only vendoring/build scripts plus the `.js`, `.prettierignore`/`.gitignore`/`.hgignore`/`ThirdPartyPaths.txt` entries, vpn has no translations reference.

### Totals

Added required items: 2 (`taskcluster/kinds/test/misc.yml` fetch list; whole `ml/tests/browser_eval/` directory plus `ml/moz.build` line). Added cleanups: 4. Wrong claims: none material; one incompleteness (browser_eval handling) and one unmentioned StaticPrefList definition.
