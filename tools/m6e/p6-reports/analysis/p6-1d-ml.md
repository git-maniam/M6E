# P6.1d analysis: the ML engine and its remaining consumers

Feature key: `p6-1d-ml`. Integration order 4 (after VPN, translations, genai; before sync, fxaccounts, updater, crashreporter, nimbus). Base commit cb002d4874. Read-only analysis; nothing was built or changed.

## 1. Summary

The ML engine is not only `toolkit/components/ml`. It has four layers, and the removal has to take all of them or the tree will not build or will carry dead entry points.

1. **The module itself**: `toolkit/components/ml` (201 files, 13 MB: engine actors, model hub, ONNX and wasm pipelines, vendored transformers.js, onnx runtime wasm and openai.mjs, llama.cpp runner and the text-generation IPDL actor, the `HWInference` utility process, XPCOM `nsIMLUtils` / `nsIMLModelHub` / `nsIMLModelResolver`, gtests, 87 browser-test files and docs).
2. **C++ and process plumbing outside the directory** (needs `./mach build`): the `inference` content process type (`RemoteType::Kind::Inference`), the `HW_INFERENCE` utility process (IPDL, `UtilityProcess*`, Mac sandbox profile, `ContentParent`/`PContent`), `dom/onnx` + `ONNX.webidl`, `LlamaRunner.webidl`, `TextGenerator.webidl`, `libmozinference` (`config/external/mozinference`), `third_party/llama.cpp`, `third_party/parakeet.cpp`.
3. **Web speech recognition** (`dom/media/webspeech/recognition` and nine `Speech*` WebIDL files, `SpeechRecognitionErrorEvent`, a permission type, a permissions-policy feature, use counters). It runs only in the `HWInference` process and downloads models through `nsIMLModelHub`, so it cannot survive the engine removal. It is a web-exposed API and the largest single item after the module itself. It must go in the same step.
4. **JS consumers in staying code**: smart tab groups, URL bar ML Suggest, semantic history search, form autofill ML, pdf.js alt text, the add-ons manager "On-device AI" category and `ModelHubProvider`, the `trial.ml` WebExtension API, `about:inference`, the AI Controls preferences wiring, `URLKeywordAnalyzer` (uses `CountVectorizer` from the ML bundle), and a long tail of prefs, strings, metrics, manifests, test-harness entries and docs.

Recommended internal split of the single branch `m6e-p6/ml`, because the two halves have very different build cost:

- **Commit A, front-end only** (`./mach build faster` is enough): layers 4 plus the JS/ftl/css/moz.build/jar.mn parts of layer 1. It leaves C++ untouched, so the C++ that still includes ML headers keeps compiling only if commit B follows. Because `toolkit/components/ml/moz.build` carries the C++ DIRS, commit A cannot delete the whole directory alone. Commit A therefore deletes only the JS pieces and removes the JS entries from `ml/jar.mn`, `ml/moz.build` (`XPIDL_SOURCES`, `MOZ_SRC_FILES`, `XPCOM_MANIFESTS`, `BROWSER_CHROME_MANIFESTS`). Simpler and safer: do A and B as one integration and run one full build.
- **Commit B, C++/IPDL/WebIDL** (`./mach build`): layers 1 (C++ part), 2 and 3.
- **Configure-time** (`toolkit/moz.configure`, `--with-onnx-runtime`): see section 6. Not needed for the build to pass; defer it to the batched configure change at the end of the phase.

Estimated size: about 700 files deleted (ml 201, speech recognition 79 + 9 webidl + events 2, llama.cpp 299, parakeet.cpp 96, dom/onnx 8, aboutinference 11, smart-tab-grouping tests, ML tests elsewhere), about 90 staying files edited.

Process-type removal (section 7) is the riskiest part. Two process kinds go: the `inference` content process and the `HW_INFERENCE` utility process. Translations (integrated earlier) was the only other user of the `inference` content process.

## 2. Files to delete

### 2.1 Directories (git rm -r)

| Path | Notes |
|---|---|
| `toolkit/components/ml/` | Everything, including `vendor/` (transformers.js, ort wasm, openai), `backends/llama`, `ipc` (HWInference), `textgeneration`, `actors`, `content`, `eval`, `tools`, `tests` (gtest, browser, browser_eval, browser_models, xpcshell, tools), `docs`, `components.conf`, `jar.mn`, `metrics.yaml`, `ml.d.ts`, `nsIML*.idl`, `MLUtils.*`, `MLProfilerMarkers.h` |
| `toolkit/components/aboutinference/` | about:inference (Nightly only; 11 files incl. tests) |
| `config/external/mozinference/` | `libmozinference` shared lib (llama + parakeet symbols) |
| `dom/onnx/` | native ONNX runtime binding (`InferenceSession`, `Tensor`, vendored `onnxruntime_*.h`). Not a vendored lib in the D21 sense: first-party glue plus three vendored headers that only it uses |
| `third_party/llama.cpp/` | 299 files. Only consumers: `ml/backends/llama`, `ml/textgeneration`, `mozinference`. Vendored, but whole-directory removal is fine because nothing else uses it; D21 only forbids editing inside it |
| `third_party/parakeet.cpp/` | 96 files. Only consumer: `dom/media/webspeech/recognition` |
| `dom/media/webspeech/recognition/` | 79 files incl. `test/`, `models.yaml`, `gen_speech_models.py`, `components.conf` (contract `@mozilla.org/ml/model-resolver;1?task=speech-recognition`) |
| `browser/components/tabbrowser/test/browser/smarttabgrouping/` | incl. `performance/` |
| `browser/components/tabbrowser/test/xpcshell/smarttabgrouping/` | |
| `browser/components/urlbar/tests/browser-ml/` | perf tests for MLSuggest and semantic history |

### 2.2 Loose files

JS and resources:

- `browser/components/tabbrowser/SmartTabGrouping.sys.mjs`
- `browser/locales-preview/smartTabGroups.ftl`
- `browser/components/urlbar/private/MLSuggest.sys.mjs`
- `browser/components/urlbar/private/SuggestBackendMl.sys.mjs`
- `browser/components/urlbar/UrlbarProviderSemanticHistorySearch.sys.mjs`
- `toolkit/components/places/PlacesSemanticHistoryManager.sys.mjs`
- `toolkit/components/places/PlacesSemanticHistoryDatabase.sys.mjs`
- `toolkit/components/formautofill/shared/FormAutofillML.sys.mjs`
- `toolkit/components/pdfjs/content/PdfJsAIFeature.sys.mjs`
- `dom/media/webspeech/metrics.yaml` (only holds `media.speech_recognition`)
- `toolkit/components/extensions/parent/ext-trial-ml.js`, `toolkit/components/extensions/schemas/trial_ml.json`
- `toolkit/mozapps/extensions/internal/ModelHubProvider.sys.mjs`
- `toolkit/mozapps/extensions/content/components/`: `addon-mlmodel-details.{css,mjs}`, `mlmodel-card-header-additions.{css,mjs}`, `mlmodel-card-list-additions.{css,mjs}`, `mlmodel-list-intro.{css,mjs}` (jar.mn uses globs, so no jar edit)
- `browser/components/preferences/OnDeviceModelManager.mjs`, but only when nothing remains in it (see 3.5)
- `dom/media/docs/SpeechRecognition.md`

C++, IPDL, WebIDL:

- `dom/webidl/ONNX.webidl`, `dom/webidl/LlamaRunner.webidl`, `dom/chrome-webidl/TextGenerator.webidl`
- `dom/webidl/SpeechGrammar.webidl`, `SpeechGrammarList.webidl`, `SpeechRecognition.webidl`, `SpeechRecognitionAlternative.webidl`, `SpeechRecognitionErrorEvent.webidl`, `SpeechRecognitionEvent.webidl`, `SpeechRecognitionPhrase.webidl`, `SpeechRecognitionResult.webidl`, `SpeechRecognitionResultList.webidl`
- `dom/events/SpeechRecognitionErrorEvent.{h,cpp}`
- `dom/media/gtest/TestSpeechModelMapping.cpp`
- `security/sandbox/mac/SandboxPolicyHWInference.h`

Tests outside the module:

- `toolkit/components/extensions/test/browser/browser_ext_trial_ml.js`
- `toolkit/mozapps/extensions/test/browser/browser_html_mlmodel_addons.js`, `toolkit/mozapps/extensions/test/xpcshell/test_ModelHubProvider.js`
- `toolkit/components/pdfjs/test/`: `browser_pdfjs_aifeature_settings.js`, `browser_pdfjs_alttext_load_engine.js`, `browser_pdfjs_alttext_perf.js`, `browser_pdfjs_alttext_telemetry.js`, `browser_pdfjs_alttext_two_tabs.js`, and `perftest.toml` if it only lists those
- `toolkit/components/places/tests/unit/`: `test_PlacesSemanticHistoryDatabase.js`, `test_PlacesSemanticHistoryManager.js`, `test_PlacesSemanticHistoryModelSwitch.js`
- `browser/components/urlbar/tests/`: `unit/test_UrlbarProviderSemanticHistorySearch.js`, `browser/browser_MLSuggest_integration.js`, `quicksuggest/unit/test_quicksuggest_yelp_ml.js`, `quicksuggest/unit/test_suggestBackendMl.js`, `browser-engagementTelemetry/browser_glean_telemetry_{abandonment,engagement}_available_semantic.js`
- `browser/extensions/formautofill/test/browser/`: `browser_ml_heuristics.js`, `browser_ml_ignore_field_types.js`, `browser_ml_mldata_features.js`, `browser_ml_native_onnx_availability.js`, `browser_ml_nimbus.js`, `performance/browser_formautofill_ml_perf.js` (+ perftest.toml entry); `test/unit/test_ml_regex_hint.js`
- `browser/base/content/test/browser-permissions/browser_speech_recognition_model_download_prompt.js`
- `browser/components/preferences/tests/aiFeatures/browser_aiFeatures_speech_recognition.js`
- `browser/components/tests/unit/test_profileDataUpgrade_smartwindow_semanticHistory.js` (smartwindow gate; genai owns the pref but the test file goes with semantic history; check at integration whether genai already removed it)
- `dom/media/webspeech/recognition/test/*` (inside the directory above)

## 3. References from staying code

Action key: **E** edit, **R** remove (the line or block), **D** delete the file.

### 3.1 Front-end and JS (commit A)

| File | What | Action |
|---|---|---|
| `toolkit/modules/ActorManagerParent.sys.mjs` | `MLEngine` JSProcessActor entry (remoteTypes `inference`, `moz-src:///toolkit/components/ml/actors/*`); the `TranslationsEngine` entry below it is already gone | R the `MLEngine` block and its comment |
| `docshell/build/components.conf` | `about_pages.append('inference')` under `NIGHTLY_BUILD` | R |
| `docshell/base/nsAboutRedirector.cpp` | `{"inference", "chrome://global/content/aboutInference.html", ...}` under `#if defined(NIGHTLY_BUILD)` | R (C++ file, commit B) |
| `dom/security/nsContentSecurityUtils.cpp` | `"about:inference"_ns` in `sImgSrcDataBlobAllowList` | R (C++, commit B) |
| `toolkit/components/moz.build` | `if CONFIG["NIGHTLY_BUILD"]: DIRS += ["aboutinference"]` | R |
| `toolkit/locales/jar.mn` | `preview/aboutInference.ftl (../components/aboutinference/content/aboutInference.ftl)` | R |
| `toolkit/content/aboutLogging/aboutLogging.mjs` (line ~171) | profiler/log preset string with `GeckoMLEngineParent:4,GeckoMLEngineChild:4,GeckoMLUtils...` | R the ML log modules from the preset |
| `devtools/shared/performance-new/prefs-presets.sys.mjs` (2 places, lines ~89 and ~196) | thread names `"onnx_worker"`, `"llama.cpp"` in profiler presets | R both entries in both lists |
| `toolkit/modules/URLKeywordAnalyzer.sys.mjs` (line 44, 156) | lazy `CountVectorizer` from `chrome://global/content/ml/NLPUtils.sys.mjs` (class lives in `ml/content/nlp/Utils.sys.mjs` lines ~81-182). It is the only staying user | E: move `CountVectorizer` (about 100 lines, no ML deps; check it does not use `cosSim`/`matMul`) into `toolkit/modules` as its own `.sys.mjs` (add to `toolkit/modules/moz.build` EXTRA_JS_MODULES) or inline it in `URLKeywordAnalyzer.sys.mjs`; update the lazy getter and the comment in `URLKeywordStopWords.sys.mjs` line 8. Keep or port its unit tests |
| `tools/ts/config/fixed_paths.js` (lines 15-19) | maps NLPUtils and ml Utils chrome URLs | R both entries |
| `toolkit/mozapps/extensions/extensions.manifest` | `category addon-provider-module ModelHubProvider ...` | R |
| `toolkit/mozapps/extensions/internal/moz.build` | `"ModelHubProvider.sys.mjs"` | R |
| `toolkit/mozapps/extensions/content/aboutaddons.html` | 4 tags for `addon-mlmodel-details.css/mjs` and `mlmodel-card-header-additions`, `-card-list-additions`, `-list-intro` | R |
| `toolkit/mozapps/extensions/content/aboutaddons.mjs` | lazy `recordDetailsViewTelemetry` from ml Utils (line 27); `addon.type === "mlmodel"` in the `detail` view (line ~117) | R both |
| `toolkit/mozapps/extensions/content/components/addon-card.mjs` | lazy imports of `recordListItemManageTelemetry`, `recordRemoveConfirmationTelemetry`, `recordRemoveInitiatedTelemetry` (lines 34-36); `<mlmodel-card-header-additions>` (165), `<mlmodel-card-list-additions>` (174), `mlmodel` branches (389, 396, 414), `updateMlModelAdditions` block (640-652) | R every `mlmodel` path |
| `toolkit/mozapps/extensions/content/components/addon-details.mjs` | `<div class="addon-detail-mlmodel"><addon-mlmodel-details>` (95-96); `mlModelDetails` block (490-494) | R |
| `toolkit/mozapps/extensions/content/components/addon-list.mjs` | `recordListViewTelemetry` import (20); `type == "mlmodel"` at 135, 491-493, 521-522 (`mlmodel-list-intro`), 535-536 | R, keeping the non-mlmodel behaviour |
| `toolkit/mozapps/extensions/content/components/categories-box.mjs` | `mlmodel` category object (lines 94-101) | R |
| `browser/base/content/browser-addons.js` (line ~1877) | `addon.type === "mlmodel"` removal prompt body | R |
| `browser/locales/en-US/browser/addonNotifications.ftl` (line 81) | `addon-mlmodel-removal-body` | R |
| `toolkit/locales/en-US/toolkit/about/aboutAddons.ftl` (lines ~613-646) | `mlmodel-*`, `addon-category-mlmodel*` strings | R the whole mlmodel block |
| `toolkit/components/extensions/ext-toolkit.json` (line 170) | `trial_ml` API entry | R |
| `toolkit/components/extensions/jar.mn` (line 51) | `parent/ext-trial-ml.js` | R |
| `toolkit/components/extensions/schemas/jar.mn` (line 54) | `trial_ml.json` | R |
| `toolkit/components/extensions/ExtensionPermissionMessages.sys.mjs` (line 56) | `"trialML"` | R |
| `toolkit/locales/en-US/toolkit/global/extensionPermissions.ftl` (line 32) | `webext-perms-description-trialML` | R |
| `toolkit/components/extensions/test/browser/browser.toml` (lines 106-107) | `browser_ext_trial_ml.js` entry and `support-files = [ "!/toolkit/components/ml/tests/browser/head.js"]` | R |
| `toolkit/components/extensions/test/xpcshell/test_ext_permissions_api.js` (line 71), `test_ext_permissions_optional_only.js` (many) | use `trialML` as the optional-only permission example | E: switch to `userScripts` (also optional-only) or drop the cases; verify the expected permission lists |
| `toolkit/components/pdfjs/jar.mn` (line 31) | `content/PdfJsAIFeature.sys.mjs` | R |
| `toolkit/components/pdfjs/content/PdfJsParent.sys.mjs` | lazy `createEngine`, `MLUninstallService`, `MultiProgressAggregator`, `Progress`, `PdfJsGuessAltTextFeature` (24-32); constants `ML_ENGINE_MAX_TIMEOUT`, `ML_GUESS_KEYS`, `MAX_ML_GUESS_CHANNELS`, `stubAltTextInference`, `validateMLGuessRequest`; message cases `PDFJS:Parent:mlGuess`, `loadAIEngine`, `mlDelete` (235-242); `_mlGuess`, `_loadAIEngine`, `_mlDelete`, `#createAIEngine` (596-790); pref reads with `browser.ml.enable` (537-549) | E: remove all of the above. Keep the other `PDFJS:Parent:*` messages. The pref snapshot sent to the viewer at 537-549 must report alt-text-AI as off (hard-code `false`) |
| `toolkit/components/pdfjs/content/PdfStreamConverter.sys.mjs` | prefs map entries `pdfjs.enableGuessAltText`, `pdfjs.enableAltTextModelDownload`, `browser.ml.enable` (229-260); `mlDelete`, `mlGuess`, `loadAIEngine` handlers (446-472) | E: remove the three handlers and their registration in the message dispatch table. Keep the pref map entries only if the vendored viewer reads them with a missing-pref error; safest is to keep the three entries (the viewer needs the keys) and let the locked prefs (section 5) provide the values |
| `toolkit/components/pdfjs/PdfJsDefaultPrefs.js`, `PdfJsOverridePrefs.js` | `pdfjs.enableGuessAltText`, `pdfjs.enableAltTextModelDownload` | Keep (vendored viewer inputs); lock to false in `m6e.js` (section 8) |
| `toolkit/components/formautofill/FormAutofillParent.sys.mjs` | lazy `FormAutofillML` (42); `#mlFeature`; `runMLInference()` (562-581) and its call in `onFieldsDetected` (~line 606) | E: remove `runMLInference` and the call; remove the static `#mlFeature` |
| `toolkit/components/formautofill/FormAutofillChild.sys.mjs` (line ~301) | `if (!FormAutofillUtils.useMLInference)` skip of regex heuristics in the child | E: make the regex path unconditional |
| `toolkit/components/formautofill/shared/FormAutofillHeuristics.sys.mjs` (lines ~1100, 1150-1170, 1239-1256) | `useMLInference` branch; `mlData` hint rewriting | E: remove the branch; `mlData` plumbing can go (also `FieldScanner.sys.mjs` lines 104, 144) or stay as inert data; remove if simple |
| `toolkit/components/formautofill/shared/FormAutofillUtils.sys.mjs` | `AUTOFILL_ML_NATIVE_ONNX_PREF` (37), `isMLAutofillEnabled`/`useMLInference` getters (273-305), setter `setNativeOnnxRuntimeAvailable`, lazy pref getters for `extensions.formautofill.useml*` (1640-1736) | R |
| `toolkit/components/formautofill/shared/AutofillTelemetry.sys.mjs` (lines 10, 287) | `FormAutofillML.getModelVersion()` for the `mlversion` extra | E: drop the `mlversion` assignment; remove the key from the Glean `address.*` event extra definition if declared (check `toolkit/components/formautofill/metrics.yaml`) |
| `toolkit/components/formautofill/moz.build` (line 25) | `shared/FormAutofillML.sys.mjs` | R |
| `browser/extensions/formautofill/api.js` | lazy `EngineProcess` (13); `refreshNativeOnnxRuntimeAvailability()` (48-70) and its call | R |
| `toolkit/components/places/moz.build` (lines 91-92) | two Semantic history modules | R |
| `toolkit/components/places/PlacesDBUtils.sys.mjs` (lines ~1130-1140) | `databaseSemanticHistoryFilesize` entry | R |
| `toolkit/components/places/metrics.yaml` (lines ~276-340) | `database_semantic_history_*` metrics (filesize, wasted_percentage, defragment_time, reindex_time, num_entries) | R |
| `tools/lint/extra-js-modules/allowlist.txt` (lines 5, 454-455) | the two Places modules and `!dom/media/webspeech/recognition/SpeechRecognitionModelDisplayInfo.sys.mjs` | R |
| `browser/components/urlbar/moz.build` (lines 52, 76, 80, 106) | `UrlbarProviderSemanticHistorySearch.sys.mjs`, `private/MLSuggest.sys.mjs`, `private/SuggestBackendMl.sys.mjs`, `tests/browser-ml/perftest.toml` | R |
| `browser/components/urlbar/UrlbarProvidersManager.sys.mjs` (line 181) | provider registration block | R |
| `browser/components/urlbar/UrlbarParentController.sys.mjs` | lazy provider (40); `#getAvailableSemanticSources()` (2227-2255) and its use at 2041-2042 (`available_semantic_sources`) | R both; drop the extra |
| `browser/components/urlbar/metrics.yaml` (lines ~132, 167, 191, 571, 609, 727, 762, 786) | `history_semantic`, `history_semantic_serp`, `tab_semantic*` result-type docs and the `available_semantic_sources` extra key | E: remove the extra key (2 events) and the semantic result-type words; Glean extras not sent are harmless, but the doc text should stay accurate |
| `browser/components/urlbar/UrlbarMuxerStandard.sys.mjs` | `SEMANTIC_HISTORY_PROVIDER_NAME` (27), `nonSemanticDupeKeys` state (112-116, 167-182, 305), `#isSuppressedSemanticDupe` (757-785) and uses at 854-858, 1261-1311 | E: remove the dedupe-for-semantic machinery |
| `browser/components/urlbar/UrlbarPrefs.sys.mjs` | defaults `suggest.semanticHistory.separateGroup` (205), `suggest.semanticHistory.minLength` (643), `quicksuggest.mlEnabled` (385), `yelp.mlEnabled` (747); `separateSemanticGroup` params in two result-group builders (872-932, 1023-1044, 1304-1339, 1424) | E: remove the pref defaults and the `separateSemanticGroup` branches; `RESULT_GROUP.SEMANTIC_HISTORY` goes with them |
| `browser/components/urlbar/content/UrlbarShared.mjs` | `RESULT_GROUP.SEMANTIC_HISTORY` (290), `getResultGroup` branch (1166-1174), group sort case (1224-1230), `_semantic` telemetry suffix (1297-1298) | R |
| `browser/components/urlbar/content/UrlbarView.mjs` (line 2599) | `semantic-history` item type attribute | R; also remove matching CSS selector `[type="semantic-history"]` in `browser/themes/shared/urlbar-searchbar.css` if present (grep at integration) |
| `browser/components/urlbar/QuickSuggest.sys.mjs` | lazy `SuggestBackendMl` (284); `#featuresByName.get("SuggestBackendMl")` (373); `mlIntent` comments (518, 557) | E |
| `browser/components/urlbar/private/YelpSuggestions.sys.mjs` | ML branch around line 130; `#normalizeMlSuggestion` (390) | E: remove the ML-sourced suggestion path |
| `browser/components/urlbar/private/SuggestFeature.sys.mjs` (line ~210) | `mlIntent` doc, and any `get mlIntent` accessors | E |
| `browser/components/urlbar/tests/browser/browser.toml` (lines 24-26) | `browser_MLSuggest_integration.js` entry and `"!/toolkit/components/ml/tests/browser/head.js"` | R |
| `browser/components/urlbar/tests/quicksuggest/unit/xpcshell.toml` (lines 90, 96), `tests/unit/xpcshell.toml` (line 30), `tests/browser-engagementTelemetry/browser.toml` (lines 20, 56) | manifest entries of deleted tests | R |
| `browser/components/urlbar/tests/unit/test_muxer.js`, `browser-engagementTelemetry/head.js`, `head-groups.js`, `browser_glean_telemetry_engagement_selected_result.js` | reference semantic provider/group | E: remove those cases |
| `browser/components/tabbrowser/moz.build` (lines 25, 38, 39, 45) | `SmartTabGrouping.sys.mjs`, two browser manifests, one xpcshell manifest | R |
| `browser/components/tabbrowser/content/tabgroup-menu.js` | the whole smart-suggestion feature: statics `AI_ICON`, `suggestionsSection`, `suggestionsButton`, `loadingSection`, `loadingActions`, `optinSection`; the `markup` that embeds them (227-290); `State` enum entries; fields 336-362; pref observers (364-397); `smartTabGroupsEnabled/PrefEnabled`, `#initSmartTabGroupsOptin`, `#initSuggestions`, `#initMlGroupLabel`, `mlLabel`, `hasSuggestedMlTabs`, suggestion state machine and `#renderSuggestionState`, `#handleSmartSuggest`, `#handleFirstDownloadAndSuggest`, `#handleMlTelemetry`, `#handleMLOptinTelemetry`, `#createRow`, `terminateProcess()` calls (625, 639, 1068) | E: remove it all, keep the standard create/edit group panel. About 800 of 1542 lines. See risk R4 |
| `browser/themes/shared/tabbrowser/tabs.css` (lines 7-8, 2388, 3480-3490 and related) | `--tab-group-suggestions-*` vars and `#tab-group-suggestions*` selectors | R |
| `browser/components/tabbrowser/metrics.yaml` (lines ~494-640) | `tabgroup.smart_tab_optin`, `smart_tab_topic`, `smart_tab_suggest`, `smart_tab`, `smart_tab_enabled` | R |
| `browser/locales/jar.mn` (line 18), `browser/base/content/browser.xhtml` (108), `browser/components/preferences/preferences.xhtml` (54) | `preview/smartTabGroups.ftl` | R |
| `browser/components/preferences/config/tabs-browsing.mjs` (lines 255-283) | `smartTabGroups` and `tabGroupSuggestions` settings, and their entry in the tab group setting group | R (also remove the markup entry that uses `tabGroupSuggestions`) |
| `browser/locales/en-US/browser/preferences/preferences.ftl` | `preferences-ai-controls-tab-group-suggestions-control` (2631), `...block-confirmation-tab-group-suggestions` (2671), and the equivalent PDF alt text and speech recognition strings (2639-2640, 2674) | R |
| `browser/components/preferences/OnDeviceModelManager.mjs` | lazy `PdfJsGuessAltTextFeature`, `SmartTabGroupingManager`, `SpeechRecognitionFeature`; `OnDeviceModelFeatures.TabGroups/PdfAltText/SpeechRecognition`, their `FeaturePrefs`, `getAIFeature`/`getPrefName` cases; `@import AIFeature` | E, or D when genai, translations and these three are the only entries. The remaining entries (`KeyPoints`, `Translations`, `SidebarChatbot`, `SmartWindow`) belong to features integrated earlier, so after those branches the module is empty: delete it and the wiring below |
| `browser/components/preferences/config/aiFeatures.mjs` (settings at 568-575, 598-604; block-dialog markup at ~202, ~1447-1454, ~1478; `extensions.ml.enabled` toggle at 130), `jar.mn` (lines 27, 36), `preferences.js` (291-294, 391, 434), `main.js` (120-122, 404-406: `browser.ai.control.pdfjsAltText/smartTabGroups/speechRecognition` pref registrations), `preferences.xhtml` (57) | AI Controls pane wiring | Coordinate with genai: if the genai branch removed the AI Controls pane, nothing to do. If it left it, remove the ML entries here (or the whole pane, since `browser.preferences.aiControls` is locked false) |
| `browser/components/preferences/tests/aiFeatures/` | tests of the three ML controls (`browser_aiFeatures.js` lines 15, 53, 105-146; `_block_dialog.js`; `_telemetry.js` 26, 73-82; `_visibility.js` 113, 254; `_speech_recognition.js`) and `browser.toml` entry (34) | E/D together with the pane decision |
| `browser/modules/PermissionUI.sys.mjs` | `MODEL_DOWNLOAD_*` consts (2447-2450), class `SpeechRecognitionModelDownloadPermissionPrompt` (2452-2815, uses `@mozilla.org/ml-modelhub;1`), export (2827) | R |
| `browser/components/permissions/ContentPermissionPrompt.sys.mjs` (lines 72-76) | `case "speech-recognition-model-download"` | R |
| `browser/base/content/popup-notifications.inc.xhtml` (lines 16-35) | `speech-recognition-model-download-notification` and `...-progress-notification` | R |
| `browser/themes/shared/browser-shared.css` (lines ~989-1040) | `#speech-recognition-model-download-*` rules | R |
| `browser/locales/en-US/browser/permissions.ftl` (lines 7-26) | `speech-recognition-model-download-*` | R |
| `browser/modules/test/browser/browser_PermissionUI.js` | speech model download tests (lines 35-56, 153-500) | R those tasks |
| `browser/base/content/test/browser-permissions/browser.toml` (line 52) | test entry | R |
| `browser/base/content/test/browser-static/browser_all_files_referenced.js` (lines 109-110) | whitelist for `chrome://global/content/ml/` | R |
| `browser/extensions/webcompat/` (`experiment-apis/aboutConfigPrefs.js` line 16, `data/interventions/2018892-museumofpragueai.com.json`, `injections/js/define_SpeechRecognition.js`, `intervention_schema.json` line 505) and `testing/webcompat/interventions/tests/test_2018892_museumofpragueai_com.py` | intervention that sets `media.webspeech.recognition.enable` false and injects a stub | Keep the stub injection (it defines `window.SpeechRecognition` when missing, still valid). Optional: R the pref override line. Low priority |
| `testing/profiles/base/user.js` (28), `unittest-required/user.js` (156), `xpcshell/user.js` (82), `remote/shared/RecommendedPreferences.sys.mjs` (138, 379), `testing/marionette/client/marionette_driver/geckoinstance.py` (681, 739), `tools/m6e/acceptance.py` (143-144), `tools/m6e/merge/nightly-prefs-reviewed.txt` (50), `testing/mochitest/ignorePrefs.json` | test-profile entries for `browser.ml.enable`, `extensions.formautofill.useml`, `places.semanticHistory.featureGate` | R the entries for prefs that no longer exist. Keep `browser.ml.enable` only if it stays defined (section 5). `acceptance.py` should keep asserting the locked value for any pref that stays |
| `python/mach/mach/command_util.py` (lines 113-114) | `eval` and `eval-tools` pointing to `toolkit/components/ml/eval/mach_commands.py` | R both, or `mach` will fail when it loads command modules |
| `python/mozperftest/mozperftest/metrics/eval.py`, `python/mozperftest/perfdocs/config.yml` (lines 61, 86-102), `testing/perfdocs/generated/*.md`, `testing/performance/adding-tests/perfdocs/index.md` | reference ML eval and perf test dirs | R the ML sections |
| `testing/mochitest/runtests.py` (parakeet model server: lines 1058, 1452-1505, 1648-1650, 1717-1723), `testing/mochitest/moz.build` (227-230, `serve_model.py`), `testing/tools/serve_model.py`, `testing/mochitest/mach_test_package_commands.py` (72, 81), `testing/mozbase/moztest/moztest/resolve.py` (235-245, 563), `testing/mozharness/configs/unittests/{linux,mac,win}_unittest.py`, `testing/mozharness/mach_commands.py` (102-105), `testing/mozharness/scripts/desktop_unittest.py` (969-970), `python/mozbuild/mozbuild/schedules.py` (74) | `mochitest-speech-recognition` and `mochitest-browser-chrome-ml-models` suites | R. Low risk, but `moz.build` TEST_HARNESS_FILES line must go together with `serve_model.py` |
| `python/mozbuild/mozbuild/action/test_archive.py` (line 66) | `"mozinference"` in the common libs list | R |

### 3.2 Prefs read by staying code that disappear

Covered in section 5. The notable one: `browser/components/asrouter/modules/SidebarChatBotPromo.sys.mjs`, sidebar and genai code read `browser.ml.chat.*` and `browser.ml.linkPreview.*`; those belong to genai (earlier) and are untouched by this step.

### 3.3 C++, IPDL, WebIDL, ipc (commit B, needs `./mach build`)

| File | What | Action |
|---|---|---|
| `dom/moz.build` (line 54) | `"onnx"` in DIRS | R |
| `dom/webidl/moz.build` (lines 859, 1153-1156, 1201-1212, 1337) | `ONNX.webidl`; `LlamaRunner.webidl` block; the `MOZ_WEBSPEECH` recognition list; `SpeechRecognitionEvent.webidl` in `GENERATED_EVENTS_WEBIDL_FILES` | R (keep the synthesis files and `SpeechSynthesisEvent`) |
| `dom/chrome-webidl/moz.build` (line 138) | `TextGenerator.webidl` | R |
| `dom/chrome-webidl/ChromeUtils.webidl` (lines 1051, 1106) | `"inference"` in `WebIDLProcType`; `"hwInference"` in `WebIDLUtilityActorName` | R both. Must match `ProcType` (`static_assert` in `ChromeUtils.cpp` line 1867) and the `UtilityActorName` list |
| `dom/base/ChromeUtils.cpp` (lines 1883, 2071-2072) | `PROCTYPE_TO_WEBIDL_CASE(Inference, Inference)`; `case RemoteType::Kind::Inference: type = ProcType::Inference` | R |
| `toolkit/components/processtools/ProcInfo.h` (line 58) | `Inference` in `enum class ProcType` | R |
| `toolkit/components/processtools/ProcInfo_common.cpp` (lines 62-63) | `UtilityActorName::HwInference -> "hw-inference"` | R |
| `dom/ipc/RemoteType.h` (82-83, 177), `RemoteType.cpp` (121-122, 219-220) | `Kind::Inference`, `IsInference()`, parse/stringify | R |
| `dom/ipc/gtest/RemoteTypeTest.cpp` (37, 92-93) | Inference cases | R |
| `dom/ipc/ContentChild.cpp` (2562-2563) | `SetProcessName("Inference")` branch | R |
| `dom/ipc/ProcessIsolation.h` (line ~155) | comment "should only occur in the inference process" | E (comment only); `AllowSystemIfLoaded` itself stays: other code uses it |
| `dom/fetch/FetchParent.cpp` (111-120), `ipc/glue/BackgroundParentImpl.cpp` (487-493), `dom/fs/child/FileSystemBackgroundRequestHandler.cpp` (80-85) | `IsInference()` adds `AllowSystemIfLoaded` | E: drop the `IsInference()` branch and the now-unused `options`. Behaviour becomes stricter, which is the intent |
| `dom/clients/manager/ClientValidation.cpp` (line 29) | comment mentions "non-inference processes" | E (comment) |
| `toolkit/components/glean/bindings/private/Labeled.cpp` (190-192), `Labeled.h` (291), `toolkit/components/glean/ipc/FOGIPC.cpp` (137, 229, 262, 397-400) | `eInferenceProcess` / `"inference"` process-type label | R the branches and enum value |
| `toolkit/components/processtools/metrics.yaml` (line 59) | `- inference` label in `&per_process_type_labels` | R (check the anchor is reused elsewhere in the file; it is a YAML alias, so one edit covers all users) |
| `dom/ipc/PContent.ipdl` (45, 1222-1232) | `include protocol PSpeechRecognition;`, `AcquireHWInferenceProcess`, `CreateSpeechRecognition`, `ReleaseHWInferenceConnection` (inside `#ifndef ANDROID`) | R (drop the `#ifndef ANDROID` wrapper if it becomes empty) |
| `dom/ipc/ContentParent.h` (1120-1131, 1523-1524), `ContentParent.cpp` (154-157, 1828-1830, 5060-5105) | includes, `EnsureHWInferenceConnection`, `RecvAcquire/Release...`, `RecvCreateSpeechRecognition`, members `mHWInferenceConnections`, `mHWInferenceKeepAlive`, reset in `ActorDestroy` | R |
| `ipc/glue/PUtilityProcess.ipdl` (14, 61, 151) | `include protocol PHWInference`, `manages PHWInference`, `async PHWInference()` | R |
| `ipc/glue/UtilityProcessChild.h` (9, 84, 97-98, 110), `.cpp` (275-282, 351) | include, `AllocPHWInferenceChild`, `GetHWInferenceChild`, `mHWInferenceInstance` | R |
| `ipc/glue/UtilityProcessManager.h` (26-28, 95-97), `.cpp` (15, 368-377) | forward decl, include, `LaunchIndependentHWInferenceProcess` | R. Keep `LaunchIndependentProcess` (used by `ipc/glue/test/gtest/TestUtilityProcess.cpp`) |
| `ipc/glue/UtilityProcessSandboxing.h` (line 29) | `HW_INFERENCE` enumerator (last before `COUNT`, so no index shift for other kinds) | R |
| `security/sandbox/mac/Sandbox.mm` (25, 309, 502-506...) | include of `SandboxPolicyHWInference.h`; `utilityKind != HW_INFERENCE` condition; the whole `else if (... HW_INFERENCE)` profile branch | E: drop the include and the condition; delete the branch (read the full branch to its closing brace at integration) |
| `security/sandbox/mac/moz.build` (line 26) | `"SandboxPolicyHWInference.h"` in EXPORTS | R |
| `extensions/permissions/PermissionDelegateHandler.h` (line 53), `.cpp` (58-59) | `{"speech-recognition-model-download", ...}` entry; `DELEGATED_PERMISSION_COUNT = 17` | R the entry and set the count to 16 (a `static_assert` ties them) |
| `dom/security/permissionspolicy/PermissionsPolicyUtils.cpp` (33-34), `test/mochitest/test_featureList.html` (26) | `"on-device-speech-recognition"` | R |
| `dom/base/nsDocumentWarningList.inc` (18-20) | 3 `SpeechRecognition*` warnings | R |
| `dom/locales/en-US/chrome/dom/dom.properties` (534-541) | their strings | R |
| `dom/base/UseCounters.conf` (317-319, 351-355, 464-466) | Speech* use counters | R |
| `dom/base/use_counter_metrics.yaml` | generated metrics for the same counters (page and document sections: ~4126-4780, 5809-5860, 11591-11640, 12169-12250, 13274-13320) | Regenerate with `./mach gen-use-counter-metrics`; do not hand-edit. The generator needs a build |
| `dom/webidl/Window.webidl` (136-138, 170-174) | `SpeechRecognitionAlternative/Result/ResultList`, `webkitSpeech*` in the list of window-exposed names | R |
| `dom/events/moz.build` (104-107, 175-176) | `SpeechRecognitionErrorEvent` export and source | R |
| `dom/bindings/moz.build` (line 88) | `/dom/media/webspeech/recognition` in LOCAL_INCLUDES | R |
| `dom/media/webspeech/moz.build` | `DIRS += ["recognition"]` and its comment | R (`synth` stays) |
| `dom/media/webspeech/synth` | unchanged | stays |
| `dom/media/gtest/moz.build` (line 131) | `TestSpeechModelMapping.cpp` | R |
| `dom/media/docs/` (index referencing `SpeechRecognition.md`) | doc | R link |
| `dom/events/test/test_all_synthetic_events.html` (340-346) | `SpeechRecognitionErrorEvent`, `SpeechRecognitionEvent` | R |
| `dom/tests/mochitest/general/test_interfaces.js` (1571-1630, 2340-2356) | 13 interface entries | R |
| `dom/ipc/tests/test_headless_content_process.js` (line 3) | `TEST_REMOTE_TYPE = "inference"` | E: switch to another remote type that exists and can be launched headless (for example `"privilegedabout"`); verify the test still exercises keep-alive of a non-web process |
| `dom/docs/ipc/process_model.md` (198-206) | "Inference Content" section | R |
| `toolkit/components/aboutprocesses/content/aboutProcesses.js` (719-720, 1148-1149) | `case "inference"`, `case "hwInference"` | R |
| `toolkit/locales/en-US/toolkit/about/aboutProcesses.ftl` (159, and `about-processes-inference-process`), `toolkit/locales/en-US/toolkit/global/processTypes.ftl` (67) | process strings | R |
| `toolkit/modules/ProcessType.sys.mjs` (line 29) | `utility_hwInference` | R |
| `build/clang-plugin/ThreadAllows.txt` (26-35) | `SpeechResampler`, `Parakeet`, `SpeechIPC` thread names | R (also check for any llama/ml thread name above line 26) |
| `build/sanitizers/ubsan_pointer_overflow_blacklist.txt` (25-26) | ggml entry | R |
| `build/sparse-profiles/toolchain-build` (10) | onnxruntime comment | R (taskcluster only) |
| `browser/installer/package-manifest.in` (145-146, 181) | `#ifdef ONNX_RUNTIME` onnxruntime dylib and `libmozinference` | R both lines. Packaging is checked at `tools/m6e/package/release.sh` time |
| `toolkit/content/licenses/moz.build` (429-432) | `libonnxruntime.dylib` license path under `if CONFIG["ONNX_RUNTIME"]` | R. Also check whether the llama.cpp, parakeet, ort wasm, transformers licences appear in the licence page lists (`toolkit/content/license.html` or `about:license` generator) |
| `config/external/moz.build` (line 14) | `DIRS += ["mozinference"]` | R |
| `third_party/moz.build` (133-134) | `with Files("llama.cpp")` | R |
| `tools/rewriting/ThirdPartyPaths.txt` (45-47, 188), `.prettierignore` (1376, 1534), `tools/clang-tidy/config.yaml` (37), `tools/lint/header-guards.yml` (17), `tools/lint/md.yml` (21), `eslint-rollouts.config.mjs` (211, 430, 600), `eslint-test-paths.config.mjs` (263), `stylelint-rollouts.config.js` (234-235), `docs/config.yml` (41, 256), `mots.yaml` (2471-2472), `tools/tryselect/try_presets.yml` (20, 27), `taskcluster/kinds/source-test/vendor-verify.yml` (178-185), `taskcluster/scripts/misc/vendor-verify-expected-fail.yml` (21), `taskcluster/kinds/source-test/mozlint.yml` (856) | path entries for deleted paths | R (P6.2 owns generic stale entries; these are for paths this step deletes) |
| `toolkit/components/glean/metrics_index.py` (36, 86) | `dom/media/webspeech/metrics.yaml`, `toolkit/components/ml/metrics.yaml` | R |
| `toolkit/components/glean/tags.yaml` (164) | tag `Core :: Machine Learning` | Becomes unused once genai/aiwindow metrics are gone; remove if nothing references it (grep at integration) |
| `toolkit/components/pageextractor/docs/index.md` (160) | link to the AI Runtime docs | E: drop the link |

### 3.4 Not touched in this step

- `toolkit/components/extensions` other than the `trial_ml` pieces.
- `services/settings`: no ML collection is in the M6E allowlist (`M6E_ALLOWED_MAIN_COLLECTIONS` is security-state and blocklists only), so there is no Remote Settings entry to remove. The `ml-*` collections (`ml-onnx-runtime`, `ml-inference-options`, `ml-model-allow-deny-list`, `ml-inference-words-block-list`, `ml-inference-request-options`) are read only from the deleted code and from the genai link-preview model (earlier feature).
- `services/settings/dumps/main/` holds no ML dump (`ai-window-prompts.json` is genai, `translations-models.json` is translations).
- Enterprise policies: `Policies.sys.mjs` / `policies-schema.json` only set `browser.ai.control.*` prefs. They stay with the AI Controls policy (genai's call).
- `browser.ai.control.*` prefs, `StartupTelemetry.sys.mjs` and `UITour.sys.mjs` reporting of them: stay (genai).

## 4. References from earlier features (ignore)

These disappear with branches 1 to 3 and need no action here:

- `browser/components/aiwindow/**` (many imports of `MLTestUtils`, `NLPUtils.cosSim`, `@mozilla.org/ml-utils;1` in `AutoTabGroupingSuggestions.sys.mjs`, `PlacesSemanticHistoryManager` users in `SearchBrowsingHistory.sys.mjs`, `MemoriesHistorySource.sys.mjs`, `AIWindow.sys.mjs` comment, `places.semanticHistory.smartwindow.*`).
- `browser/components/genai/**` (`LinkPreview.sys.mjs` calling `nsIMLUtils.canUseLlamaCpp()`, `LinkPreviewModel.sys.mjs`, `model-optin.mjs`, genai metrics, perftest.toml, `browser/locales/en-US/browser/genai.ftl`).
- `browser/components/translations/**`, `toolkit/components/translations/**` (the second user of the `inference` content process and of `MLEngineParent`; `TranslationsEngine` actor; test `shared-head.js` comments, `keepInferenceProcessAlive`).
- `browser/components/ipprotection/**`.
- `services/settings/dumps/main/ai-window-prompts.json` and `translations-models.json`.
- `browser.ml.chat.*`, `browser.ml.linkPreview.*`, `browser.ml.pageAssist.enabled`, sidebar, asrouter promo, contentanalysis chat-shortcut tests.
- `TranslationsParent.AIFeature` hits in `browser/base/content/browser.js` (line 2480), `nsContextMenu.sys.mjs` (2783) and `QuickActionsLoaderDefault.sys.mjs` (300): these are translations' own `AIFeature` getter, not the ML module.

### 4.1 From later features (must be handled in this branch)

| Later feature | File | What | Action |
|---|---|---|---|
| nimbus | `toolkit/components/nimbus/FeatureManifest.yaml` | `urlbar` feature variables: `quickSuggestMlInitDelaySeconds`-style `fallbackPref: browser.urlbar.quicksuggest.mlInitDelaySeconds` (507), `...mlEnabled` (513), `yelp.mlEnabled` (814), all `semanticHistory*` variables (618-745: pref `places.semanticHistory.*`, `browser.urlbar.suggest.semanticHistory.*`) | R the variables. If Nimbus checks that every `pref:` exists (a test in `toolkit/components/nimbus/test`), leaving them would fail |
| nimbus | same file, the formautofill feature (lines ~2262-2320: `extensions.formautofill.useml`, `.twoHead`, `.timeoutMS`, `.features`, `.modelVersion`, `.ignoreFieldTypes`) | | R the ML variables (and the whole feature if it only had ML variables) |
| nimbus | same file, `pdfjs` feature (~2592-2620): `enableAltText` variable with `pref: browser.ml.enable` and a "local inference engine" description (2612) | | E: drop the ML variable(s) |
| nimbus | same file, `smartTabGroups` (5518-5560) and `autoTabGrouping` (6324+, uses `browser.tabs.groups.smart.agglomerativeThresholdInt` at 6407; autoTabGrouping itself is aiwindow, earlier) | | R `smartTabGroups`; for autoTabGrouping the earlier branch should already be gone, otherwise R |
| nimbus | any Nimbus tests that enumerate those features | | E/R |
| crashreporter, updater, sync, fxaccounts | none found (grep of `inference`, `llama`, `onnx`, `hwinference` in `toolkit/crashreporter`, `toolkit/mozapps/update`, `services/sync`, `services/fxaccounts` finds nothing) | | |

## 5. Prefs, metrics and strings to remove

### Prefs (remove the definition)

`modules/libpref/init/all.js`:

- 1771-1775: `extensions.ml.enabled`, `extensions.htmlaboutaddons.local_model_management`
- 1880-1881: `dom.ipc.processCount.inference` and its comment
- 3627: `browser.ai.control.speechRecognition` default: stays (genai-owned `browser.ai.control.*` set)
- 3629-3650: `browser.ml.enable`, `.logLevel`, `.modelHubRootUrl`, `.modelHubUrlTemplate`, `.modelCacheMaxSize`, `.modelCacheTimeout`, `.llama.hwInference`, `.minimumPhysicalMemory`, `.checkForMemory`, `.overridePipelineOptions`. `browser.ml.pageExtractor.headlessTimeoutMs` is used by pageextractor (not an ML feature): stays
- 4031-4090: all `extensions.formautofill.useml*`

`modules/libpref/init/StaticPrefList.yaml`:

- 1854-1876: `browser.ml.modelHub.testing`, `browser.ml.hwinference.max_restarts`, `browser.ml.hwinference.browser_idle_shutdown_grace_ms` (and the leading comments)
- 13615-13660: `media.webspeech.recognition.enable`, `.install_on_start`, `.idle_shutdown_grace_ms`, `.endpoint_blank_ms`, `.testing.parakeet_init_delay_ms`, plus `.model`, `.model.<lang>`, `.model-download.prompt.testing` if defined in the YAML or elsewhere (grep `media.webspeech.recognition.` at integration)
- A StaticPrefList edit changes generated C++ headers: part of the C++ rebuild, and it forces a large rebuild because `StaticPrefs_*.h` is widely included. Do it in commit B, not in commit A

`browser/app/profile/firefox.js`:

- 586-591: `browser.urlbar.quicksuggest.mlEnabled`, `.mlInitDelaySeconds`
- 705-728: `places.semanticHistory.*` (featureGate, supportedRegions, embeddingType, multilingualEmbeddingRegions), `browser.ml.embedGen.*`, `browser.urlbar.suggest.semanticHistory.minLength`
- 755-757: `browser.urlbar.yelp.mlEnabled`
- 1109-1136: `browser.tabs.groups.smart.*` (enabled, suggestOtherTabsMethod, topicModelRevision, embeddingModelRevision, nearestNeighborThresholdInt, clusterMethod, agglomerativeThresholdInt, optin, searchTopicEnabled, userEnabled)
- 2278-2283: `places.semanticHistory.smartwindow.*`: genai/aiwindow own these; remove here only if the earlier branch did not

`toolkit/components/pdfjs/PdfJsDefaultPrefs.js` / `PdfJsOverridePrefs.js`: keep `pdfjs.enableGuessAltText`, `pdfjs.enableAltTextModelDownload` (vendored viewer inputs); they are locked false in m6e.js.

Also remove the `places.semanticHistory.initialized`, `.defaultBatchChunksize`, `.distanceThreshold`, `.maxChunkTimeMs*`, `.minEntriesBeforeThrottle`, `.removeOnStartup` references (they exist only as code defaults in the deleted modules).

`browser/app/profile/m6e.js`: currently locks `browser.ml.enable`, `browser.tabs.groups.smart.enabled/userEnabled`, `places.semanticHistory.featureGate`, and `browser.ai.control.*`. After the deletion:

- remove `browser.tabs.groups.smart.*`, `places.semanticHistory.featureGate` (line 334) if the pref is no longer defined elsewhere (a locked `pref()` in m6e.js would otherwise define it and keep it alive for no reader)
- keep `browser.ml.enable` locked false **only if** the vendored pdf.js viewer still reads it through `PdfStreamConverter` (it does today), and add `pdfjs.enableGuessAltText` and `pdfjs.enableAltTextModelDownload` locked false there
- `browser.ai.control.*` lines stay (genai)

`remote/shared/RecommendedPreferences.sys.mjs`, `geckoinstance.py`: see 3.1.

### Glean / telemetry

- Delete `toolkit/components/ml/metrics.yaml` (categories `firefox.ai.runtime`, `model_management`); metrics_index.py entry.
- Delete `dom/media/webspeech/metrics.yaml` (`media.speech_recognition`); metrics_index.py entry.
- Remove the 5 semantic-history metrics in `toolkit/components/places/metrics.yaml`.
- Remove 5 `tabgroup.smart_tab*` metrics in `browser/components/tabbrowser/metrics.yaml`.
- Remove the `available_semantic_sources` extra and semantic result-type text in `browser/components/urlbar/metrics.yaml`.
- Drop the `mlversion` event extra in formautofill if declared.
- Regenerate `dom/base/use_counter_metrics.yaml`.
- No `Events.yaml`, `Scalars.yaml` or `Histograms.json` entry mentions ML (checked).
- Check at integration that no remaining code reads `Glean.firefoxAiRuntime`, `Glean.modelManagement`, `Glean.mediaSpeechRecognition`: aiwindow/genai (earlier) are the only other users.

### Strings

- `browser/locales-preview/smartTabGroups.ftl` (file), `aboutAddons.ftl` mlmodel block, `addonNotifications.ftl` line 81, `permissions.ftl` speech strings, `preferences.ftl` three ML controls and confirmations, `extensionPermissions.ftl` trialML, `aboutProcesses.ftl`, `processTypes.ftl`, `dom.properties` speech warnings, `toolkit/components/aboutinference/content/aboutInference.ftl` (with the directory).
- Run `tools/m6e/branding/scan-strings.py` after the edits only as a sanity check; no brand-string changes are expected.

## 6. Build-system edits

### Front-end only (`./mach build faster` enough for these alone)

- `toolkit/components/moz.build` (aboutinference DIRS), `toolkit/locales/jar.mn`, `browser/locales/jar.mn`, `browser/components/*/jar.mn` and `moz.build` entries listed in section 3.1, `toolkit/components/extensions/{jar.mn,ext-toolkit.json,schemas/jar.mn}`, `toolkit/mozapps/extensions/{extensions.manifest,internal/moz.build}`, `toolkit/components/places/moz.build`, `toolkit/components/formautofill/moz.build`, `toolkit/components/pdfjs/jar.mn`, `browser/components/urlbar/moz.build`, `browser/components/tabbrowser/moz.build`.
- `toolkit/components/ml/jar.mn`, `actors/moz.build`, `tests/moz.build`: disappear with the directory.
- Test manifests: removing `BROWSER_CHROME_MANIFESTS` / `XPCSHELL_TESTS_MANIFESTS` entries re-runs moz.build and rewrites the test manifest backend files; no compile.

### C++ / IPDL / WebIDL / XPIDL (needs full `./mach build`; the incremental rebuild is large because of these)

- `toolkit/components/ml/moz.build` (the whole directory: XPIDL `nsIMLModelHub`, `nsIMLModelResolver`, `nsIMLUtils`; `XPCOM_MANIFESTS`; `UNIFIED_SOURCES` `MLUtils.cpp`; `DIRS` `backends/llama`, `ipc`, `textgeneration`; `TEST_DIRS tests/gtest`).
- `dom/moz.build`, `dom/webidl/moz.build`, `dom/chrome-webidl/moz.build`, `dom/events/moz.build`, `dom/bindings/moz.build`, `dom/media/webspeech/moz.build`, `dom/media/gtest/moz.build`.
- IPDL changes: `PContent.ipdl`, `PUtilityProcess.ipdl`, deletion of `PHWInference.ipdl`, `PTextGeneration.ipdl`, `TextGenerationTypes.ipdlh`, `PSpeechRecognition.ipdl`. These change generated protocol headers included by many files.
- `StaticPrefList.yaml` edit: regenerates `StaticPrefsAll.h`, a widespread include. Expect a very large rebuild.
- `security/sandbox/mac/{Sandbox.mm,moz.build}`.
- `config/external/moz.build` (mozinference), `third_party/moz.build`.
- `dom/base/UseCounters.conf`: regenerates `UseCounter.h`, widely included.
- `ipc/glue/UtilityProcess*`, `dom/ipc/*`, `extensions/permissions/*`, `dom/security/*`, `toolkit/components/glean/*` and `processtools`.

Because of `StaticPrefList.yaml`, `UseCounters.conf` and the IPDL changes, expect the C++ half to rebuild most of libxul. With LTO/PGO off for this phase it is still a long incremental build; plan one build for the whole feature.

### Configure-time (rebuilds everything; batch)

- `toolkit/moz.configure` lines ~4584-4680: the "ONNX runtime support" block (`--with-onnx-runtime`, `onnxruntime_task_name`, `ONNX_RUNTIME` config and define, and the `artifact_builds` variant that reads `taskcluster/kinds/toolchain/onnx.yml`). In the current build `ONNX_RUNTIME` is unset (`--disable-bootstrap`, no onnxruntime available; `obj-m6e/config.status` has no `ONNX_RUNTIME`), so the block is inert once the readers are gone. Recommendation: do not edit `moz.configure` in this step; remove it in the final batched configure change (or in P6.3, which already does a clean build). Everything that reads `ONNX_RUNTIME` (`ml/moz.build`, `package-manifest.in`, `toolkit/content/licenses/moz.build`) is removed here anyway, so skipping it is safe.
- `MOZ_WEBSPEECH` configure option stays: speech synthesis uses it.
- No mozconfig change needed.

## 7. Process-type removal (separate, highest risk)

Two process kinds are removed. Both need `./mach build`; both touch IPDL.

### 7.1 `inference` content process (`RemoteType::Kind::Inference`)

What it is: a content process with remote type `inference`, default count 1 (`dom.ipc.processCount.inference`), launched on demand by `MLEngineParent` (`hostProcess: "inference"`), hosting ChromeWorkers that run wasm or native ONNX/llama code. Pref name and string are read in `RemoteType.cpp`; the process count pref is looked up by remote type name.

Touches (all listed in 3.3): `dom/ipc/RemoteType.{h,cpp}` and its gtest, `ContentChild.cpp` (process name), `dom/base/ChromeUtils.cpp` (`ProcType` mapping), `ProcInfo.h`, `ChromeUtils.webidl` (`WebIDLProcType`), `FetchParent.cpp`, `BackgroundParentImpl.cpp`, `FileSystemBackgroundRequestHandler.cpp`, `ClientValidation.cpp` (comment), `ProcessIsolation.h` (comment), `Labeled.{cpp,h}`, `FOGIPC.cpp`, `processtools/metrics.yaml`, `aboutProcesses.js`, `dom/docs/ipc/process_model.md`, `dom/ipc/tests/test_headless_content_process.js`, `all.js` (`dom.ipc.processCount.inference`), `ActorManagerParent.sys.mjs`.

Consistency constraints the compiler enforces: `WebIDLProcType` and `ProcType` must have identical members and order (`static_assert` in `ChromeUtils.cpp`); removing one without the other fails the build. `switch` statements over `RemoteType::Kind` with no default will raise `-Wswitch` errors if one site is missed (RemoteType.cpp has two).

Behavioural effect: the three `IsInference()` sites currently relax principal validation to allow the system principal in this process. After removal the relaxation is gone, which only tightens validation. `ValidatePrincipalOptions::AllowSystemIfLoaded` stays because legacy chrome documents loaded in content processes use it (comment in `ProcessIsolation.h`).

Unknowns to check at integration: any other `switch` on `ProcType` (grep `ProcType::` and `eInferenceProcess`); the `gmplugin`/`utility` labels in telemetry; whether a Mochitest harness expects the `inference` label in `about:processes`.

### 7.2 `HW_INFERENCE` utility process (`SandboxingKind::HW_INFERENCE`, actor `PHWInference`)

What it is: an independent utility process (macOS sandbox policy `SandboxPolicyHWInference.h`, with Metal access) that runs llama.cpp text generation and speech recognition. It talks to the main process over `PHWInference` (managed by `PUtilityProcess`) and to content processes via `PContent::AcquireHWInferenceProcess` / `CreateSpeechRecognition` / `ReleaseHWInferenceConnection`.

Touches: `PUtilityProcess.ipdl`, `UtilityProcessChild.{h,cpp}`, `UtilityProcessManager.{h,cpp}`, `UtilityProcessSandboxing.h`, `Sandbox.mm`, `SandboxPolicyHWInference.h`, `security/sandbox/mac/moz.build`, `PContent.ipdl`, `ContentParent.{h,cpp}`, `ProcInfo_common.cpp`, `ChromeUtils.webidl` (`hwInference`), `aboutProcesses.js`, `ProcessType.sys.mjs`, `aboutProcesses.ftl`, `processTypes.ftl`, `ThreadAllows.txt`.

`HW_INFERENCE` is the last enumerator before `COUNT`, so removing it does not change the values of the other kinds. The `MOZ_DISABLE_CONTENT_SANDBOX=utility:N` index syntax is therefore unaffected for existing kinds. Check `security/sandbox` gtests and `ipc/glue/test/gtest/TestUtilityProcess.cpp` for iteration up to `COUNT` (they use `GENERIC_UTILITY` only in the grep, so likely fine).

Risk specific to the sandbox file: `Sandbox.mm` has a long `if/else if` chain; the HW_INFERENCE branch (line ~502) sets params for the policy. Deleting it must leave the `else` chain valid for the remaining utility kinds. Read the full branch before editing.

### 7.3 Other process-adjacent pieces

- `libmozinference` (`config/external/mozinference`): shared library loaded dynamically by `LlamaRuntimeLinker` and by parakeet code in the HWInference process. Removed with the process; also drop it from `package-manifest.in` and `test_archive.py`.
- `dom/onnx` + `ONNX.webidl` + `LlamaRunner.webidl` + `TextGenerator.webidl`: exposed only when `InInferenceProcess()` is true (`[Func=...]` gates), but they are still WebIDL interfaces in the binary. Removal deletes the bindings.
- `SpeechRecognition` DOM API (section 8, decision S1).

## 8. Must stay (with proof), decisions and fallbacks

### Decision S1: web speech recognition is deleted, not kept

Reason: it needs the `HWInference` process, `nsIMLModelHub` (JS service in the deleted module) and `nsIMLModelResolver` (XPIDL in the deleted module). It cannot compile or run without them. Keeping it would mean keeping the whole stack.

Current state: `media.webspeech.recognition.enable` is `@IS_NOT_ANDROID@` in `StaticPrefList.yaml`, i.e. true on macOS, and `m6e.js` does not lock it. The WebIDL interfaces are `Pref`-gated on it, so today `SpeechRecognition` is exposed to web content in M6E. The only thing stopping a session is the locked `browser.ai.control.speechRecognition = blocked` check in C++ (`SpeechRecognitionBlockedByAIControls` warning). That is a real entry point from web content and from IPC (`PContent::CreateSpeechRecognition` takes an endpoint from a content process), so it fails the D23 test and is a second reason to delete.

Unreachability if a later decision keeps it anyway (not recommended): lock `media.webspeech.recognition.enable` false in `m6e.js`. That removes the JS-visible interfaces but not the `PContent` IPC messages or the utility process, so it would not satisfy the "no entry points from IPC" rule on its own. That is why the C++ is removed.

### Decision S2: sqlite-vec stays (optional later removal)

`storage/moz.build` defines `MOZ_SQLITE_VEC0_EXT` for the browser app; `third_party/sqlite3/ext/sqlite-vec` is a vendored SQLite extension (D21: leave vendored code alone). Its only first-party user is `PlacesSemanticHistoryDatabase`, which is being deleted. Unreachability proof: it can only be loaded through `mozIStorageConnection.loadExtension("vec")` (`storage/mozStorageConnection.cpp` `Connection::LoadExtension`), an XPCOM method that chrome code calls on its own connections; web content has no reference to a storage connection, and there is no IPC path to it. SQL is composed only by chrome JS. Removal is possible (`storage/moz.build`, `mozStorageConnection.cpp` list entry, `third_party/sqlite3/ext/moz.build`, `storage/test/unit/test_storage_ext_vec.js`) but not required and would touch vendored code. Keep it, record the proof, and revisit in P6.2.

### Decision S3: `browser.ml.enable` stays defined and locked false

Read by the vendored pdf.js viewer code through the `PdfStreamConverter` pref map. Proof: `pref("browser.ml.enable", false, locked)` already in `m6e.js`; with the engine gone there is no consumer that could turn it into behaviour.

### Decision S4: `AllowSystemIfLoaded`, `LaunchIndependentProcess`, `UtilityProcessManager` generic API stay

They have other callers (legacy chrome documents; `TestUtilityProcess.cpp`).

### Decision S5: `pdfjs.enableAltText*` prefs and viewer code stay

The viewer (`toolkit/components/pdfjs/content/build/*`, `web/*`) is vendored. Parent-side handlers for `mlGuess`, `loadAIEngine`, `mlDelete` are removed, so even if the viewer asks, nothing answers. Prefs locked false (section 5) so the viewer UI never offers alt-text generation.

### Decision S6: `browser.ml.pageExtractor.headlessTimeoutMs`, `browser.pageextractor.*`

Belong to `toolkit/components/pageextractor`, which is not part of this feature and is not listed in the nine. Keep. Its docs link to the ML docs (edit the link).

## 9. Tests to delete or edit

Deleted outright: everything in section 2 (module tests, ML tests elsewhere).

Edited:

| Test | Edit |
|---|---|
| `dom/tests/mochitest/general/test_interfaces.js` | remove 13 Speech recognition and webkit entries |
| `dom/events/test/test_all_synthetic_events.html` | remove 2 entries |
| `dom/security/permissionspolicy/test/mochitest/test_featureList.html` | remove `on-device-speech-recognition` |
| `dom/ipc/gtest/RemoteTypeTest.cpp` | remove Inference cases |
| `dom/ipc/tests/test_headless_content_process.js` | use another remote type |
| `toolkit/components/extensions/test/xpcshell/test_ext_permissions_api.js`, `test_ext_permissions_optional_only.js` | replace `trialML` with `userScripts` |
| `toolkit/components/extensions/test/browser/browser.toml` | drop trial_ml entry and support-file reference |
| `toolkit/components/pdfjs/test/browser.toml`, `browser_pdfjs_update_preferences.js` (mentions alt text prefs) | drop deleted test entries; fix expected pref list |
| `browser/modules/test/browser/browser_PermissionUI.js` | remove speech tasks |
| `browser/components/urlbar/tests/unit/test_muxer.js`, `browser-engagementTelemetry/head*.js`, `browser_glean_telemetry_engagement_selected_result.js`, `tests/unit/test_searchEngineStore.js` (hit on `mlEnabled`/`engineProcess`: check, probably unrelated name collision) | remove semantic cases |
| `browser/components/preferences/tests/aiFeatures/*` | see 3.1 |
| `browser/components/tests/unit/test_startupTelemetry_aiControlBlocking.js` | genai-owned |
| `browser/components/enterprisepolicies/tests/xpcshell/test_simple_pref_policies.js` | genai-owned |
| `toolkit/components/places/tests/unit/xpcshell.toml`, `head_common.js` | drop the three deleted tests |
| `toolkit/mozapps/extensions/test/browser/browser.toml`, `test/xpcshell/xpcshell.toml` | drop deleted entries |
| `browser/extensions/formautofill/test/**` | drop six ML tests; `browser_dynamic_form_autocompletion.js` has a `useml` mention: check |
| `testing/web-platform/meta/speech-api/*` | delete `SpeechRecognition-*.ini` and speech parts of `idlharness.https.window.js.ini`; set the directory as expected-fail or disabled in `__dir__.ini` (it sets `media.webspeech.recognition.enable` and model-hub prefs). WPT tests under `testing/web-platform/tests/speech-api` will fail by design unless disabled |
| `browser/components/aiwindow/**` tests | earlier |

New or kept tests worth running at integration after the build: `dom/ipc/gtest` RemoteType, `./mach test toolkit/components/pdfjs/test` (non-AI), `toolkit/mozapps/extensions/test/browser/browser_html_*` (about:addons category list), `browser/components/urlbar/tests/unit` and `quicksuggest`, `browser/components/tabbrowser/test/browser/tabs` and tab group tests (panel after tabgroup-menu edit), `toolkit/components/formautofill` and `browser/extensions/formautofill` suites, `toolkit/components/places/tests/unit`, `toolkit/components/extensions/test/xpcshell/test_ext_permissions*`, `browser/base/content/test/browser-static` (`browser_all_files_referenced`, `browser_parsable_css`), `dom/tests/mochitest/general/test_interfaces.js`, `security/sandbox` gtests, utility-process gtest, `toolkit/components/aboutprocesses` tests, URLKeywordAnalyzer unit tests (netError search CTA), Glean metrics lint (`./mach lint -l glean_parser` or `./mach build` generating metrics).

## 10. Risks and open questions

R1. **Size and build cost.** The C++ half touches `StaticPrefList.yaml`, `UseCounters.conf`, IPDL and WebIDL, so the integration build will recompile most of libxul. One build for the whole feature; do not split into several C++ commits.

R2. **Speech recognition is a web-exposed API removal.** `SpeechRecognition` disappears from the Window global. The interface is currently exposed by default on macOS (pref true, not locked), so this is a visible web-platform change. It also drops WPT coverage. Confirm with the orchestrator that removing a web API in M6E is acceptable (D23 says delete when the build passes without it, and the user relayed approval for dormant feature removal; speech recognition was never mentioned by name in D30, but it is an AI/ML consumer). Fallback if refused: none that satisfies the IPC rule without keeping the whole stack; the alternative is to keep P6.1d out of the tree for now.

R3. **Overlap with genai/translations branches.** OnDeviceModelManager, `aiFeatures.mjs`, `preferences.js`, `main.js`, `preferences.xhtml`, StartupTelemetry, UITour, policies and tests share lines with those branches. Cherry-picking in order 1, 2, 3, 4 will conflict if those branches also edit the same hunks. The ML branch should edit only hunks that name the three ML features (smart tab groups, pdf alt text, speech recognition) and leave the rest. If genai removed the pane wholesale, drop these edits.

R4. **`tabgroup-menu.js`.** Smart suggestion code is interleaved with core group-editing logic (state machine `suggestionState` is also used for plain create/edit modes: `CREATE_STANDARD_INITIAL`, `EDIT_STANDARD_INITIAL`). A careless removal can break tab group creation, which is a core feature. After the edit run the tab group browser tests. Cheaper fallback with lower risk: keep `tabgroup-menu.js` unchanged except for the lazy `SmartTabGrouping` import in `#initSuggestions` and make `smartTabGroupsEnabled` always false. That leaves about 800 lines of dead UI code but it is unreachable behind `browser.tabs.groups.smart.enabled` locked false in `m6e.js`. I recommend the full removal but would accept the fallback if tests show instability.

R5. **`URLKeywordAnalyzer` and `CountVectorizer`.** Moving the class must preserve behaviour (it is a pure tokenizer here with an empty stopword list). Cover with the existing unit tests in `toolkit/modules/tests/xpcshell`.

R6. **Nimbus manifest.** Removing prefs while `FeatureManifest.yaml` still references them can break Nimbus startup validation. The manifest edit is in this branch (section 4.1) because Nimbus integrates later; if the Nimbus branch deletes the whole manifest then those edits are redundant but harmless.

R7. **Generated files in the tree.** `dom/base/use_counter_metrics.yaml` is checked in and generated from `UseCounters.conf`; regenerate with `./mach gen-use-counter-metrics` after the build, do not hand-edit. `tools/@types/generated/*` contains ML entries (`lib.gecko.modules.d.ts` 618-619, `lib.gecko.xpcom.d.ts` 10151-10200, `tspaths.json` many): regenerate via the TS tooling or leave for P6.2; they are not read by the build.

R8. **Taskcluster and CI-only files** (`taskcluster/kinds/{perftest,mochitest,fetch,toolchain,build,source-test}`, `taskcluster/scripts/misc/*onnx*`, `taskcluster/test/params/*`, `testing/mozharness`) mention the deleted paths. They are not used by local builds. Task graph unit tests (`taskcluster/gecko_taskgraph/test`) may reference them (`test_transforms_build.py`, `transforms/build.py`, `transforms/fetch.py`: hits on onnx). Suggest leaving taskcluster YAML to P6.2/P6.3 or removing in one sweep; local builds are unaffected either way.

R9. **`browser.ml.enable` pref plumbing in `PdfStreamConverter`.** Confirm that the vendored viewer treats a missing key as false; keep the pref defined and locked to avoid undefined reads.

R10. **Add-ons manager.** `addon-card.mjs`, `addon-list.mjs`, `addon-details.mjs`, `categories-box.mjs` are core about:addons files. The edits are removals of `mlmodel` branches only; run the about:addons browser tests.

R11. **`about:inference` is Nightly-only.** `browser/installer` and `docshell` conditionals use `NIGHTLY_BUILD`. M6E builds as Nightly (161.0a1), so the page exists today; removing it is a visible change but the page is dormant (`browser.ml.enable` false locked).

R12. **Open: licences page.** The about:license generator may list llama.cpp, parakeet, onnx and transformers licences. Check `toolkit/content/license.html`/`toolkit/content/licenses/moz.build` for entries tied to deleted directories (only the `ONNX_RUNTIME` one was found).

R13. **Open: other `UtilityActorName` consumers.** Removing `hwInference` from `WebIDLUtilityActorName` may affect `toolkit/components/processtools/ProcessToolsService`, the telemetry processes list or `GeckoChildProcessHost` doc comments. A final `git grep -i hwinference` and `git grep -w Inference -- '*.cpp' '*.h' '*.webidl' '*.ipdl'` must return nothing in first-party code.

R14. **Open: `LlamaRunner` and `libmozinference` in the current objdir.** The baseline build in `obj-m6e` may include mozinference; after the change the build system drops it, and a stale `dist/bin/libmozinference.dylib` can remain in the app bundle. Check the final bundle contents.

R15. **Order of removal inside the build.** If the preparer splits into commits A and B, commit A must not leave `ml/moz.build` referencing deleted JS (`MOZ_SRC_FILES`, `jar.mn`) while C++ still exists. Simplest is one commit for the whole branch.

## Critic addendum

Critic pass at base 4110e90366 (git grep from other angles: basenames, symbols, pref prefixes, Glean category names, FTL ids, manifests, lint configs, other analysts' reports). The analysis is broadly sound and the file lists in sections 2 and 3 held up. The items below are what it missed or got wrong.

### A. Wrong or misleading claims

1. **Section 8 S3 and R9 (`browser.ml.enable` is read by the vendored pdf.js viewer): wrong.** In `PdfStreamConverter.sys.mjs` the `browser.ml.enable` entry has `dispatchToParent: true` only, with no `dispatchToContent`. A grep of `toolkit/components/pdfjs/content/web/` and `content/build/` finds no use of the name. The only parent-side reader is the `ai_generation` telemetry field in `PdfJsParent.sys.mjs` (~line 549), which is inside the block the report already removes. The two prefs the viewer does read are `pdfjs.enableGuessAltText` and `pdfjs.enableAltTextModelDownload` (both `dispatchToContent`), so S5 stands. Correction: `browser.ml.enable` can be removed entirely. That means dropping the `PdfStreamConverter` map entry, the `all.js` definition, the locked line in `m6e.js`, and the entries in `UrlbarPrefs.sys.mjs`, `RecommendedPreferences.sys.mjs`, `geckoinstance.py`, `acceptance.py` and `tabgroup-menu.js`. If the orchestrator prefers to keep a locked pref for the acceptance check, say so explicitly, but "needed by the viewer" is not a valid reason.
2. **Section 3.3 `toolkit/components/glean/tags.yaml` (164): the tag `Core :: Machine Learning` will NOT become unused.** `toolkit/components/pageextractor/metrics.yaml` (staying) still lists it. Keep that tag. The tag that does become unused is `Core :: Web Speech` (`tags.yaml` line 228), used only by the deleted `dom/media/webspeech/metrics.yaml`. Remove that one and check `dom/media/metrics.yaml` does not use it (it only mentions Web Speech in prose).
3. **Section 5 Glean: "5 semantic-history metrics" in `toolkit/components/places/metrics.yaml` is incomplete.** There are 9. Besides the five `database_semantic_history_*` entries (276-340), there are `semantic_history_find_chunks_time` (445), `semantic_history_max_chunks_count` (461), `semantic_history_chunk_calculate_time` (477) and `semantic_history_indexing_stopped` (493). The last four are used only by `PlacesSemanticHistoryManager` and its test, so they must go too.
4. **Section 3.4 "Enterprise policies only set `browser.ai.control.*` prefs": not accurate.** `AIControls` and `GenerativeAI` in `Policies.sys.mjs` also set `pdfjs.enableAltText`, `browser.tabs.groups.smart.userEnabled` (lines 172, 182, 2190) plus the `browser.ml.chat.*` and `linkPreview` prefs. The schema keys `SmartTabGroups`, `PdfjsAltText`, `SpeechRecognition` (policies-schema.json ~154, 246) name features this step deletes.
5. **Section 5 `m6e.js`: "locks `places.semanticHistory.featureGate`" is wrong.** Line 334 is a plain, unlocked `pref(..., false)`. It still gets removed once the pref is undefined, but it is not a lock.
6. **Section 3.1 devtools row: lists only `onnx_worker` and `llama.cpp`.** The same two lists in `devtools/shared/performance-new/prefs-presets.sys.mjs` (lines 89-92 and 196-199) also contain `Parakeet` and `TextGenerator`. Remove all four from both lists, plus the comment line "On-device machine learning threads:".
7. **Section 3.3 `build/clang-plugin/ThreadAllows.txt` (26-35): incomplete.** Line 119 `TextGenerator` is also feature-owned. Remove it (check that no other consumer uses that thread name; the protobuf hit is a class name, not a thread).
8. **Section 3.1 `tools/lint/extra-js-modules/allowlist.txt`: lines 5, 454, 455 are not all.** Also line 394 `toolkit/components/formautofill/shared/FormAutofillML.sys.mjs` and line 654 `toolkit/mozapps/extensions/internal/ModelHubProvider.sys.mjs`. (P6.2 owns generic stale entries, but these point at files this step deletes.)

### B. Ownership gap with the genai branch (most important finding)

The analyst writes that the AI Controls scaffolding "stays (genai)" / "genai's call". The genai report (`p6-1c-genai.md`, sections 8.2, 2 and open question 1, and its "Keep" line for `all.js`) says the opposite: it defers the AI-controls scaffolding to P6.1d ("Removal scheduled for P6.1d (the pane then disappears with the ML engine)"). Neither branch would remove it. Because every `OnDeviceModelManager` feature is gone after this step, `browser/components/preferences/config/aiFeatures.mjs` (which imports `OnDeviceModelManager`) cannot be left as is. Decision needed, recommended: P6.1d owns this removal. List to add to the ML branch:

| File | Reference | Action |
|---|---|---|
| `modules/libpref/init/all.js` (~3620-3627) | `browser.ai.control.*` defaults | R |
| `browser/app/profile/m6e.js` (131-140, 152) | `browser.ai.control.*` locks, `browser.preferences.aiControls` lock, and the comment above them | R, or keep a minimal block only if the orchestrator decides the policies stay |
| `browser/app/profile/firefox.js` (1415, 1418) | `browser.preferences.aiControls`, `.showUnavailable` | R |
| `browser/components/preferences/config/aiFeatures.mjs`, `OnDeviceModelManager.mjs`, `main.js` (`aiControl*` settings, 120-125, 404-421), `preferences.js`, `preferences.xhtml`, `jar.mn`, `config/tabs-browsing.mjs` (`aiControlDefault` use at ~268-277) | the pane | D or E (whole pane) |
| `browser/components/preferences/tests/aiFeatures/` | whole directory | D (includes the genai-owned `browser_aiFeatures_chatbot.js`, `_smart_window*.js`, `head_smart_window.js` if genai left them) |
| `browser/components/StartupTelemetry.sys.mjs` (`aiControlBlocking`, 361-395) and `browser/components/metrics.yaml` (`global_ai_control_is_blocking`, `ai_control_is_blocking`, `global_ai_control_toggled`, `ai_control_changed`, ~1186-1260) and `browser/components/tests/unit/test_startupTelemetry_aiControlBlocking.js` | AI-control telemetry | R |
| `browser/components/uitour/UITour.sys.mjs` (~1708 `aiControls` target) | | R the AI-control reporting |
| `browser/components/customizableui/content/panelUI.js` (172-183, 1235-1237) | `AIControlDefault` / `AIControlSmartWindow` observers | genai-owned (smart window); check genai removed it, else R |
| `browser/components/enterprisepolicies/Policies.sys.mjs` (`AIControls` 157-215, `GenerativeAI` ~2165-2205, `TranslateEnabled` branch 3861-3869), `schemas/policies-schema.json` (`AIControls` ~62, `GenerativeAI`), `tests/xpcshell/test_simple_pref_policies.js` (~1360-1430), policy docs | policies that set undefined prefs | decision: removal (they set prefs nobody reads) or keep as inert |
| `tools/m6e/acceptance.py` (131-148) | `browser.ai.control.*` and `browser.ml.*` assertions | E to match whatever stays |
| `browser/locales/en-US/browser/preferences/preferences.ftl` (2618-2674 `preferences-ai-controls-*`) | | R the block, not only the three ML ids |
| `testing/web-platform/meta/...`, `browser/components/sessionstore/test/unit/data/sessionstore_complete.json` (`aiControlSmartTabGroupsSelect`, 530) | fixture data | leave (data only) |

If the orchestrator instead decides the scaffolding stays, the report must say what happens to `aiFeatures.mjs` (it would fail on its `OnDeviceModelManager` import) and must still unlock-proof the three ML controls.

`model-optin` hand-off: the genai report moves `model-optin.{mjs,css}` and ids `genai-model-optin-continue|optout|cancel` into `browser/components/tabbrowser/content/` and explicitly says "P6.1d deletes it with Smart Tab Groups". The ML report never mentions it. Add to this branch: delete `model-optin.mjs` and `model-optin.css` in their new tabbrowser location, their `jar.mn` entries, the three FTL ids (and whichever FTL file genai put them in), the `insertFTLIfNeeded` call and `document.createElement("model-optin")` (`tabgroup-menu.js` ~606, 613, 1205), and the `stylelint-rollouts.config.js` entry for `model-optin.css`. At the current base the module is still at `chrome://browser/content/genai/content/model-optin.mjs` (`tabgroup-menu.js` line 28).

### C. Additional references missed

| File | Reference | Action |
|---|---|---|
| `browser/components/urlbar/UrlbarPrefs.sys.mjs` (781, `PREF_OTHER_DEFAULTS`) | `["browser.ml.enable", false]` | R |
| `browser/components/urlbar/QuickSuggest.sys.mjs` (418-421, 480-481, 518-527, 550-581, 1275) | `#featuresByMlIntent`, `getFeatureByMlIntent`, the `case "ml":` branch in the source-to-feature lookup, the "ml" source doc | E: remove the whole ML-intent map and the `"ml"` case, not only the comments |
| `browser/components/urlbar/private/SuggestFeature.sys.mjs` (210-225), `YelpSuggestions.sys.mjs` (56-64, 99-125: `mlIntent`, `isMlIntentEnabled`, `s.source == "ml"`, `yelpMlEnabled`) | | E: remove the accessors and the `source == "ml"` selection |
| `browser/components/urlbar/metrics.yaml` (145, 585, 740) | result type doc `ml_yelp` | E (doc text) |
| `toolkit/mozapps/extensions/test/browser/browser_html_themes_mode.js` (35) | `["mlmodel", false]` category row | E |
| `modules/libpref/docs/index.md` (216) | `Inference` row in the process-type sanitization table | R |
| `browser/extensions/formautofill/test/browser/performance/perftest.toml` (17), `browser_dynamic_form_autocompletion.js` (6 `useml` mentions, e.g. 145) | `extensions.formautofill.useml.nativeOnnxAvailable` | E (drop the pref and the `iter` toggle logic) |
| `toolkit/components/formautofill/metrics.yaml` (1092 `mlversion`, 1696 "identified by ML") | the report said "if declared": it is declared | R `mlversion` extra, reword 1696 |
| `browser/extensions/formautofill/test/unit/xpcshell.toml` (86) | `test_ml_regex_hint.js` entry | R (the report only named the browser manifests) |
| `testing/mochitest/runjunit.py` (121-137) | `needsParakeetModelServer` override and `self.parakeetModelServer = None` | R together with the `runtests.py` edits, or `runjunit.py` breaks when the base method is removed |
| `build/rust/dummy-web/web-sys/Cargo.toml` (1211-1221) | `Speech{Grammar,GrammarList,Recognition,RecognitionAlternative,RecognitionError,...}` features | R (keep `SpeechSynthesis*`); dummy crate, not read by the build, but it mirrors the removed WebIDL. Touches Cargo.toml, so check `Cargo.lock` is unaffected (dummy crate; expected unaffected) |
| `testing/web-platform/meta/speech-api/` | also `SpeechGrammarList-item.https.html.ini` and `historical.html.ini`; `__dir__.ini` sets `media.webspeech.recognition.enable`, `browser.ml.modelHub.testing`, `...model-download.prompt.testing` | E: drop the prefs line; delete the recognition ini files; re-check `historical.html.ini` expectations |
| `python/mozperftest/mozperftest/system/ml_services.py`, `system/__init__.py` (9, 21, 73-90), `tests/test_ml_services.py` | `MLServices` layer and `ml_services` arg; only perf tests under deleted/earlier dirs use it | optional R (CI-only); leave to P6.2 if the orchestrator wants a smaller diff |
| `python/mozcmakeparser/mozcmakeparser/__init__.py` (157, 256-290, 409) | `llama` flavor and `MOZ_GGML_BACKENDS` handling | optional: vendoring tooling for the deleted `third_party/llama.cpp`; harmless to leave |
| `stylelint-rollouts.config.js` (271) | `mlmodel-card-header-additions.css` (report cited 234-235 only) | R |
| `dom/media/webspeech/moz.build` (7-8), `dom/events/moz.build` (105), `dom/webidl/moz.build` (1201-1202) | comments that explain the recognition backend | R with the entries they annotate |
| `build/sanitizers/ubsan_pointer_overflow_blacklist.txt` (25-26) | covered by report; the matching UBSan fixtures in `testing/mozbase/**/tests/test_ubsan.py`, `test_resource_monitor.py`, `mozlog/tests/*` reference `third_party/llama.cpp/ggml/src/ggml-c.c` only as inline test data | leave (data strings, not paths read) |
| `browser/themes/shared/tabbrowser/tabs.css` (2388) | `toolbarseparator:not(#tab-group-suggestions-separator)` | E: simplify the selector, do not just delete the rule |
| `browser/components/genai/content/link-preview-card.css` (209-210) | uses `--tab-group-suggestions-loading-animation-color-*` from `tabs.css` | earlier (genai deletes it); noted only so the `tabs.css` variable removal is safe once genai is integrated |

### D. Confirmed correct (spot checks)

- `AllowSystemIfLoaded` has other callers (`ClientValidation.cpp`, `BrowserParent.cpp`, `HttpChannelParent.cpp`, `ProcessIsolation.cpp`), so keeping it is right.
- `m6e.js` does not lock `media.webspeech.recognition.enable` (only `browser.ai.control.speechRecognition`), and the five `media.webspeech.recognition.*` StaticPrefList entries are at 13615-13660 as stated. `media.webspeech.test.enable` and `synth.*` stay.
- `RemoteType::Kind::Inference` use sites, `eInferenceProcess` sites (`FOGIPC.cpp`, `Labeled.cpp`), `UtilityProcess*` sites, `Sandbox.mm`/`moz.build`, `PContent.ipdl` and `ChromeUtils.webidl` lists match a fresh grep. The `UtilityActorName` mapping is macro-based, so only `ProcInfo_common.cpp` needs an edit.
- `trialML` appears only where the report lists it (`ExtensionPermissionMessages`, `ext-toolkit.json`, both `jar.mn`, `trial_ml.json`, `ext-trial-ml.js`, two xpcshell tests, one browser test).
- No ML dump or collection in `services/settings`; no ML entry in `toolkit/components/telemetry` (`Events.yaml`, `Scalars.yaml`, `Histograms.json`); no `Glean.firefoxAiRuntime`, `Glean.modelManagement` or `Glean.mediaSpeechRecognition` user outside deleted code and earlier-feature dirs.
- `toolkit/moz.configure` ONNX block (lines ~4662-4680) is inert and only `ml/moz.build`, `package-manifest.in` and `toolkit/content/licenses/moz.build` read `ONNX_RUNTIME`; no other licence-page entry for llama/ggml/parakeet/transformers exists (closes R12).
- sqlite-vec (S2): `MOZ_SQLITE_VEC0_EXT` in `storage/moz.build` and the `"vec"` entry in `mozStorageConnection.cpp` are chrome-only; leaving them is consistent with D21. The aiwindow user of `vec0` goes with genai (earlier).

### E. Counts and process notes

- `dom/onnx/*onnxruntime*.h` are listed in `tools/rewriting/ThirdPartyPaths.txt` (45-47), `tools/lint/header-guards.yml` (17), `tools/clang-tidy/config.yaml` (37) and `.prettierignore` (1376): all covered by the report's lint row, nothing extra.
- A `git grep -w -i inference` over first-party JS, ftl, toml, md and conf files finds no further hit than those in sections 3.1 and 3.3 plus `modules/libpref/docs/index.md` (above) and `dom/media/webspeech/metrics.yaml` (deleted).
- Commit-order caution: `tabgroup-menu.js` also carries lazy AIWindow imports (lines ~22-24) that genai must have removed first; when the ML edit lands, re-read the file at the genai tip, not at base.
