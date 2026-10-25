# P6.1b Translations: preparation notes

Branch m6e-p6/translations, 5 commits on base 4110e90366 (analysis base cb002d4874), head 0c76391f26. 428 files changed, 517 insertions, 110108 deletions. Nothing built or tested; static checks only (node --check, python py_compile, JSON/YAML parse, tree-wide search).

## Deleted
- browser/components/translations/ (whole), browser/themes/shared/translations/, icons/translations-companion.svg
- toolkit/components/translations: actors, bergamot-translator, content, docs, tests/browser, tests/scripts, README.md, TranslationsFeature/Telemetry/Utils, jar.mn, metrics.yaml, tests/unit/test_telemetry_sampling.js
- preferences: translations.js, translations.d.ts, config/translations.mjs, dialogs/translations.js + .xhtml
- FTL: browser/translations.ftl, toolkit/about/aboutTranslations.ftl
- services/settings/dumps/main/translations-{models,wasm}.json, taskcluster fetch translations-fetch.yml
- tests: enterprisepolicies browser_policy_translateenabled.js, urlbar browser_translate.js, whole toolkit/components/ml/tests/browser_eval/ (critic addendum)
- python/l10n fluent migrations bug_2007194 and bug_2020067 (about:translations)

## Kept in toolkit/components/translations (must stay)
LanguageDetector.sys.mjs, cld2/, tests/unit/{test_cld2.js,xpcshell.toml}, translations.d.ts (reduced to the exported DetectionResult and MultilingualSection), tsconfig.json (include now LanguageDetector.sys.mjs + translations.d.ts), moz.build (reduced to the XPCSHELL manifest and EXTRA_JS_MODULES.translations). Consumers: ExtensionContent (tabs.detectLanguage), parent/ext-i18n.js (i18n.detectLanguage), ReaderMode._assignLanguage. No network, no actor, no about: page, no pref-gated UI. toolkit/components/moz.build still lists "translations". The CSP evalAllowlist entry for cld-worker.js stays. browser/themes/shared/icons/translations.svg stays (Languages pane icon).
javascript.options.wasm_moz_intgemm: locked false in browser/app/profile/m6e.js (js/src/intgemm and third_party/gemmology untouched; privileged contexts only).

## Edited
- ML (later feature): RemoteSettingsManager.getMaxSupportedVersionRecords and isBetterRecordVersion in ml/content/Utils.sys.mjs (chaosMode dropped, verifySignature false); MLEngineParent uses it; Attachment/WasmRecord moved to ml.d.ts; EngineProcess getTranslationsEngineParent/destroyTranslationsEngine removed; Utils.uninstallAll no longer destroys the Translations engine; ml/moz.build drops the browser_eval manifest; ml/tests/browser/head.js carries its own _remoteSettingsMockId, createAttachmentMock, waitForCondition and imports EngineProcess itself; AIFeature.sys.mjs and ml docs reworded.
- Window: ActorManagerParent (3 actors), BrowserComponents.manifest, browser.js/.globals, browser-init.js, browser-sets.*, appmenu-viewcache, browser-context.*, nsContextMenu, main-popupset.*, navigator-toolbox.*, browser.xhtml, webext-panels.xhtml, urlbar.css, preferences.css, jar.inc.mn, QuickActionsLoaderDefault, UITour, StartupTelemetry, browser/components/moz.build.
- Preferences: jar.mn x2, main.inc.xhtml, main.js (initTranslations and settings), preferences.js/.xhtml, config/languages.mjs, LegacyPaneMappings.mjs, OnDeviceModelManager.mjs and aiFeatures.mjs (genai-owned), PreferencesNavMap + generator (genai-owned).
- Policies: Policies.sys.mjs (AIControls Translations, TranslateEnabled, mostRecentTargetLanguages), schema, descriptions FTL.
- Prefs and registries: firefox.js, all.js, m6e.js, RecommendedPreferences, geckoinstance.py, nimbus FeatureManifest.yaml (4 features) + FeatureManifest.sys.d.mts, glean metrics_index.py + tags.yaml, SpecialMessageActions + index.md, nsContentSecurityUtils.cpp (stale dialogs/translations.xhtml entry).
- FTL: appmenu, browser, preferences, brandings (-translations-brand-name), fluent-lint.yml.
- CI/harness: taskcluster mochitest/test/perftest/test-sets, moztest resolve, mozharness (4 files + 3 configs), schedules.py, testinfo.py, fetch-manifest-data.js, perfdocs config + generated md, tryselect Translations preset.
- Lint/ignore: .gitignore, .hgignore, .prettierignore, ThirdPartyPaths.txt, eslint-file-globals, eslint-rollouts, stylelint-rollouts, mots.yaml (translation module now covers only toolkit/components/translations, description reworded).
- Generated types hand-edited: tools/@types/generated/{lib.gecko.glean.d.ts,lib.gecko.modules.d.ts,tspaths.json}.
- Tests edited: browser_contextmenu.js + contextmenu_common.js, browser_startup_content.js, browser-sync browser_contextmenu_sendpage.js, preferences aiFeatures x4, browser_languages_pane.js, browser_sub_pane_navigation_selection.js (uses #languages-spellCheck), test_simple_pref_policies.js (PDFAltText replaces Translations as the unlocked-override case), test_startupTelemetry_aiControlBlocking.js (pdfjsAltText replaces translations), enterprisepolicies browser.toml.
- tools/m6e: acceptance.py REMOVED_PREFS["Translations"] removed; nightly-prefs-reviewed.txt extensions.translations.disabled removed, intgemm note reworded.

## integrationTodos
- ./mach build (not faster): deleting toolkit/components/translations/metrics.yaml regenerates the Glean FOG bindings; nsContentSecurityUtils.cpp changed.
- No configure change, no Cargo change, no Cargo.lock change.
- Run at integration: xpcshell toolkit/components/translations/tests/unit, extensions test_ext_i18n.js and test_ext_tabs_detectLanguage.html, reader tests, enterprisepolicies xpcshell test_simple_pref_policies.js and the policy schema/handler consistency tests, browser/components/tests/unit/test_startupTelemetry_aiControlBlocking.js, preferences tests (languages, aiFeatures, browser_sub_pane_navigation_selection, search), browser-contextMenu, browser-performance startup content, UITour aiControls, ml tests (browser/head.js changed; check browser_ml_engine_e2e, browser_ml_native, xpcshell), tools/m6e/acceptance.py.
- Update M6E_IMPLEMENTATION_PLAN.md (P6.1b row, progress log) and m6e_browser_agent_instructions.md at integration; not touched here.
- Optionally regenerate the type files with ./mach ts build instead of the hand edits.

## notDone / deliberately kept
- LanguageDetector + cld2 (see above).
- js/src/intgemm, javascript.options.wasm_moz_intgemm (locked off), tools/m6e/merge/new-nightly-prefs.py ENABLE_WASM_MOZ_INTGEMM.
- docshell/base/nsAboutRedirector.cpp and docshell/build/components.conf comments, tools/m6e REMOVED_ABOUT / REMOVED_SETTINGS_GROUPS / REMOVED_ELEMENTS and merge/check-helpers.py DORMANT_PAGES: regression guards.
- ml/eval/evals.py Translations* metric classes and the python/mozperftest tests that use them (ml-owned, generic).
- ProfileDataUpgrader migration < 148 (uninstalls a legacy add-on id only).
- Vendored: third_party/application-services dumps translations-models.json, remote/test/puppeteer firefox.ts pref line.
- Stale lint entries for non-existent paths (browser/extensions/translations, mobile geckoview translations) belong to P6.2. eslint-rollouts "toolkit/components/translations/**", allowlist.txt, eslint-ignores, .clang-format-ignore and the ThirdPartyPaths cld2 line are kept.
- taskcluster/test/params fixtures and sessionstore_complete.json still mention tr8ns / aiControlTranslationsSelect (recorded data).
- Existing profiles keep stale browser.translations.* user prefs and "translations" permission rows (harmless).
