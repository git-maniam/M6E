# P6.1c analysis: generative AI interface and AI window (key p6-1c-genai)

Base: `main` at 4110e90366 (code identical to cb002d4874 for this feature). Read-only analysis; no build, no objdir use.
Integration order 3: VPN (1) and Translations (2) are already gone when this lands. Sync, Mozilla accounts, ML engine, updater, crash reporter and Nimbus are still present and are handled below where they reference this feature.

Search method: `git grep -n -I` per top-level directory (a whole-tree grep takes minutes while the baseline build runs). Identifier set used: `genai|GenAI|aiwindow|AIWindow|ai-window|smartwindow|SmartWindow|smart-window|smart_window|smartbar|aichat|AIChat|linkpreview|link-preview|LinkPreview|pageassist|PageAssist|browser.ml.chat|browser.ml.linkPreview|browser.ml.pageAssist|browser.smartwindow|browser.highlightToSearch|aitab|AITab|SmartFormFill|AgentMonitor|MemoryStore|chatbot-browser|viewGenaiChatSidebar`, plus `content/(genai|aiwindow)`, `skin/(smartbar|...)`, actor names, Glean categories, about: names, widget ids.

## 1. Summary

- The feature is two directories (751 tracked files: `browser/components/aiwindow` 7 loose + `ui` 535 + `models` 134 + `services` 6; `browser/components/genai` 71 incl. 36 tests) plus about 60 loose files elsewhere: the Smartbar urlbar (6 source files, 2 tests), Firefox View "Chats", two ASRouter promo modules, FTL, theme CSS and icons, two built-in themes, a Remote Settings dump, and many tests.
- What it contains: AI chatbot sidebar and shortcuts (selection menu, "highlight to search"), link preview, page assist, tab/page summarize context menus, Smart Window (AI window type, Smartbar, chat UI, memories, agent monitors/tasks, AI tab = `about:smartpage`, Smart Form Fill, Organize Tabs button, themes, first-run).
- The window-type plumbing is woven into about 45 first-party files (browser.js, browser-sets.js, BrowserWindowTracker, BrowserContentHandler, SessionStore, Tabbrowser, UrlbarUtils, Sanitizer, nsContextMenu, panelUI, sidebar, preferences, UITour, ASRouter, AboutWelcome, formautofill/satchel/passwordmgr autocomplete, LightweightThemeConsumer, CSS). Each needs an edit; most are `AIWindow.isAIWindowActive(w)` style branches that become `false`.
- Required C++ edits (small): `StaticPrefList.yaml` pref `browser.smartwindow.aitab.enabled` is read by `AboutRedirector.cpp` and `BaseHistory.cpp`; three chrome:// allow-lists in `nsFrameLoader.cpp`, `nsContentSecurityUtils.cpp`, `DOMSecurityMonitor.cpp` name feature pages. No configure, Cargo or gkrust change. Glean `metrics.yaml` deletion regenerates FOG bindings (needs `./mach build`, not `faster`).
- Hazards found:
  1. `browser/components/genai/content/model-optin.mjs` is imported by `tabgroup-menu.js` for Smart Tab Groups (ML feature, still present). It must be relocated, not deleted (section 8).
  2. `ProfileDataUpgrader.sys.mjs` calls `getBoolPref("browser.ml.chat.enabled")` with no default; once the pref is removed it throws during profile upgrade (section 3).
  3. `tools/m6e/acceptance.py` requires `browser.ml.chat.enabled` and `browser.smartwindow.enabled` to be locked false and reads `win.AIWindow`; update it in the same commit (section 3).
  4. Remote Settings dump `services/settings/dumps/main/ai-window-prompts.json` ships in the build.
- Decision proposed: keep the shared "AI controls" scaffolding (`browser.ai.control.*` prefs, AIControls/GenerativeAI policies, UITour `aiControls`, StartupTelemetry `aiControlBlocking`, `AIFeature.sys.mjs`) for P6.1d. Only the imports of deleted modules inside the Preferences AI pane are cut now.

## 2. Files to delete

### 2.1 Directories
| Path | Notes |
|---|---|
| `browser/components/genai/` | Whole dir: GenAI*, LinkPreview*, PageAssist*, chat.html/js/css, pageAssist.html, `content/` (incl. `genai.ftl`, link-preview-card, page-assist, chatbot-promo, model-optin), `assets/`, `jar.mn`, `metrics.yaml`, `tests/` (xpcshell, browser, chrome). Exception: relocate `content/model-optin.{mjs,css}` first (section 8). |
| `browser/components/aiwindow/` | Whole dir: `ui/` (actors, modules, components, content, assets, `aiwindow.manifest`, `jar.mn`, test incl. marionette/perf), `models/` (agents, aitab, memories, search, scripts, tests), `services/` (MemoryStore), `metrics.yaml`. `ui/aiwindow.manifest` carries the category entries `browser-first-window-ready`, `browser-window-delayed-startup`, `browser-window-unload`, `browser-idle-startup` (MonitorAgent), `browser-quit-application-granted`; they go with the dir. `TESTING_JS_MODULES` `AIWindowTestUtils.sys.mjs` goes too. |
| `browser/themes/addons/aiwindow/`, `browser/themes/addons/aiwindow-nova/` | Built-in themes `firefox-aiwindow@mozilla.org`, `firefox-aiwindow-nova@mozilla.org`. |

### 2.2 Loose files (outside the two directories)
Source:
- `browser/components/urlbar/content/SmartbarInput.mjs`, `SmartbarInputController.mjs`, `SmartbarInputUtils.mjs`, `SmartbarMentionUtils.mjs`
- `browser/components/urlbar/SmartbarMentionsPanelSearch.sys.mjs`, `browser/components/urlbar/UrlbarProviderAiChat.sys.mjs`
- `browser/components/firefoxview/ChatsController.sys.mjs`, `chats.mjs`, `chats-tab-list.mjs`, `chats-tab-list.css`, `content/empty-chat.svg`, `content/view-chats.svg`
- `browser/components/asrouter/modules/SidebarChatBotPromo.sys.mjs`, `SmartWindowNewTabPromo.sys.mjs`
- `browser/components/asrouter/content-src/templates/OnboardingMessage/SidebarChatBotPromo.schema.json`, `SmartWindowNewtabPromo.schema.json`
- `browser/components/asrouter/docs/sidebar-chatbot-promo.md`
- `toolkit/mozapps/extensions/content/components/smartwindow-themes-notice.mjs`
- `browser/components/aboutwelcome/assets/mr-kit-smart-window.svg`
- `browser/themes/shared/aiWindowSidebar.css`, `smartbar.css`, `smartwindowGroupTabs.css`, `smartwindowMonitor.css`
- `browser/themes/shared/icons/smart-window-dot.svg`, `smart-window-mono-32.svg`, `smart-window-simplified.svg`
- `services/settings/dumps/main/ai-window-prompts.json`

Locales:
- `browser/locales/en-US/browser/aiWindow.ftl`, `aiWindowContent.ftl`, `aiFeatures.ftl`, `genai.ftl`, `autoTabGrouping.ftl`
- `browser/locales-preview/aiWindow.ftl`, `toolkit/locales-preview/smartWindow-aboutAddons.ftl`
- (`browser/components/genai/content/genai.ftl`, the `preview/genai.ftl` source, goes with the dir)

Tests and tooling:
- `browser/base/content/test/browser-menubar/browser_aiwindow_options.js`
- `browser/components/uitour/test/browser_UITour_aiwindow.js`
- `browser/components/sessionstore/test/marionette/test_restore_aiwindow.py`
- `browser/components/preferences/tests/aiFeatures/`: `browser_aiFeatures_chatbot.js`, `browser_aiFeatures_smart_window*.js` (7 files), `head_smart_window.js`; `browser/components/preferences/tests/browser_tabs_browsing_highlightToSearch.js`
- `browser/components/urlbar/tests/unit/test_UrlbarProviderAiChat.js`, `test_resultGroups_smartbar.js`
- `toolkit/components/messaging-system/schemas/SpecialMessageActionSchemas/test/browser/browser_sma_fxa_aiwindow_signin_flow.js`, `browser_sma_open_organize_tabs_panel.js`, `browser_sma_summarize_page.js`
- `toolkit/mozapps/extensions/test/browser/browser_html_smartwindow_themes_notice.js`
- `toolkit/components/contentanalysis/tests/browser/browser_clipboard_paste_chat_shortcuts.js`
- `toolkit/components/ml/tests/browser/browser_ml_llama_smollm2_smoke.js` (imports `genai/LinkPreviewModel.sys.mjs`, ML code but a hard dependency)
- `browser/components/tests/unit/test_profileDataUpgrade_smartwindow.js`, `test_profileDataUpgrade_smartwindow_semanticHistory.js` (if the migrations are removed, see 3)
- `browser/components/customizableui/test/unit/test_organize_tabs_migration.js` (if the v27 migration block is removed)
- `python/l10n/fluent_migrations/bug_2069149_genai_shortcut_button_tooltip.py`

## 3. References from staying code

Action key: **edit** = remove or simplify; **required** = breaks (import of deleted module, missing-default pref read, missing element) if not done. Unmarked edits are dead-string cleanup.

### 3.1 Registration and startup
| File | What | Action |
|---|---|---|
| `browser/components/moz.build` | `DIRS` `"aiwindow"`, `"genai"` | remove both (required) |
| `browser/components/BrowserComponents.manifest` | `browser-window-delayed-startup ... genai/LinkPreview.sys.mjs LinkPreview.init`; `browser-idle-startup ... genai/GenAI.sys.mjs GenAI.init` | remove both lines (required) |
| `browser/components/DesktopActorRegistry.sys.mjs` | actors `AIChatContent`, `AISmartBar`, `AITab`, `AgentMonitorItem`, `SmartWindowTasks`, `GenAI`, `LinkPreview`, `PageAssist`, `SmartFormFill`, `SmartFormFillReview`; `"chrome://browser/content/genai/chat.html"` and `"about:aichatcontent"` in the `LightweightTheme` `matches` list | remove each actor block and the two match entries (required) |
| `toolkit/components/glean/metrics_index.py` | `browser/components/aiwindow/metrics.yaml`, `browser/components/genai/metrics.yaml` | remove both (required) |
| `browser/components/storybook/.storybook/main.js` | story globs for `aiwindow/ui/**` and `genai/content/**` | remove 2 globs + comment |
| `mots.yaml` | submodules `core_machine_learning_frontend` (includes both dirs) and `core_machine_learning_models` (aiwindow/models, services) | remove includes/submodules (mots lint) |
| `.prettierignore`, `tools/rewriting/Generated.txt` | `browser/components/aiwindow/models/PreferencesNavMap.sys.mjs` | remove lines |
| `stylelint-rollouts.config.js` | 4 paths under aiwindow/genai | remove |
| `tools/tryselect/try_presets.yml` | 2 path entries | remove |
| `tools/lint/extra-js-modules/allowlist.txt` | `asrouter/modules/SidebarChatBotPromo.sys.mjs`, `SmartWindowNewTabPromo.sys.mjs` | remove lines |
| `testing/marionette/harness/marionette_harness/tests/integration-tests.toml` | `include:` of `aiwindow/ui/test/marionette/manifest.toml` | remove (required) |
| `python/mozperftest/perfdocs/config.yml` | `genai/tests/browser`, `aiwindow/ui/test/browser_perf` entries | remove (perfdocs lint) |
| `taskcluster/kinds/perftest/{linux,macosx-aarch64,windows11-24h2}.yml` | link-preview perf test path; jobs `smartwindow-intent-perf-wasm/-native`, `smartwindow-perf` | remove paths and jobs |
| `testing/profiles/common/user.js` | `browser.smartwindow.autoTabGrouping.preloadModels` | remove |
| `tools/@types/generated/{lib.gecko.modules.d.ts,tspaths.json,lib.gecko.glean.d.ts}` | generated type paths | regenerate or hand-delete the genai/aiwindow rows (non-blocking) |
| `taskcluster/test/params/mc-nightly-all.yml`, `testing/perfdocs/generated/mozperftest.md`, `testing/mozbase/mozlog/tests/test_testsummary.py`, `tools/m6e/branding/fox-art.txt` | fixture or generated data | ignore |

### 3.2 Window type, windows and menus (browser/base, browser/modules)
| File | What | Action |
|---|---|---|
| `browser/modules/BrowserWindowTracker.sys.mjs` | lazy `AIWindow`; `openWindow` calls `AIWindow.handleAIWindowOptions(options)`; `@param options.aiWindow` doc | drop import and call (`args` stays from `options`), drop doc (required) |
| `browser/modules/URILoadingHelper.sys.mjs` | `params.aiWindow` sets `ai-window` extra option | remove 3 lines |
| `browser/components/BrowserContentHandler.sys.mjs` | lazy `AIWindow`, `AuthProvider`; `canOpenAsSmartWindow()`, `openAsSmart` branches in `openBrowserWindow` (array args), `aiWindow: openAsSmart`, start page substitution `AIWindow.initialStartupURL` | remove helper and branches; args become the plain `nsISupportsString` / array path (required) |
| `browser/base/content/browser-main.js` | `if (AIWindow.isOpeningAIWindow(window)) importESModule(SmartbarInput.mjs)` | remove (required) |
| `browser/base/content/browser-init.js` | `ai-window` and `aiwindow-immersive-view` extraOptions attributes; `LinkPreview.teardown(window)` in `onUnload` | remove (required for LinkPreview) |
| `browser/base/content/browser.js` | lazy `AIWindow`; `customElements.setElementCreationCallback` for `agent-monitor-panel`, `smartwindow-group-tabs-card/-flyout`; `menu_newAIWindow` hidden logic (~1942-1950); `AIWindow.updateImmersiveView` (~2354) | remove all (required) |
| `browser/base/content/browser.js.globals` | `"AIWindow"` | remove |
| `browser/base/content/browser-sets.js` | lazy `AIWindowUI`; default-window launch (~25); `Tools:ClassicWindow`, `Tools:AIWindow`, `Tools:ChatsHistory` cases; `viewGenaiChatSidebarKb` case with `Glean.genaiChatbot.keyboardShortcut` | remove cases; keep other commands (required) |
| `browser/base/content/browser-sets.inc.xhtml` | `<command id="Tools:AIWindow"/>`, `Tools:ChatsHistory`, `<key id="viewGenaiChatSidebarKb">` (l10n `ai-chatbot-sidebar-shortcut`) | remove |
| `browser/base/content/browserSets.ftl` (`browser/locales/en-US/browser/browserSets.ftl`) | `ai-chatbot-sidebar-shortcut` | remove |
| `browser/base/content/browser-menubar.inc.xhtml` / `browser-menubar.js` | `menu_newAIWindow`; `chatsHistoryMenu`, `startChatHistorySeparator`, `recentChatsHeader`, "Chat history menu items"; `AIWindow.appMenu(event, window)` (~207) | remove (required) |
| `browser/base/content/appmenu-viewcache.inc.xhtml` | `appMenu-new-ai-window-button` + badge; `appMenu-chats-history-button`; whole `panelview ai-window-toggle-view` | remove |
| `browser/components/customizableui/content/panelUI.js` | observer `ai-window-state-changed`; getters `isAIWindowEnabled`, `AIControlSmartWindow`; the block ~1233-1260 (new AI window / classic / chat-history items) | remove |
| `browser/base/content/browser-box.inc.xhtml` | `ai-window-splitter`, `ai-window-box` (and stack) | remove |
| `browser/base/content/navigator-toolbox.inc.xhtml` / `navigator-toolbox.js` | `smartwindow-ask-button` toolbaritem; lazy `AIWindowUI`; two `case "smartwindow-ask-button"`; the button in two id lists (~227, ~366) | remove |
| `browser/base/content/browser.xhtml` | localization links `browser/genai.ftl`, `browser/autoTabGrouping.ftl`, `preview/genai.ftl`, `preview/aiWindow.ftl`, `browser/aiWindow.ftl` | remove 5 links (required: missing FTL logs errors) |
| `browser/base/content/macWindow.inc.xhtml`, `sanitize_v2.xhtml`, `spotlight.html`, `webext-panels.xhtml` | `browser/aiWindow.ftl`, `preview/aiWindow.ftl` links | remove |
| `browser/base/content/spotlight.js` | `smart_window_user_feedback_data` event_context | remove branch |
| `browser/base/content/utilityOverlay.js` | lazy `AIWindow`; `AIWindow.newTabURL` branch in `openTrustedLinkIn`/new tab URL (~70) | remove |
| `browser/base/content/browser-places.js` | `AIWindow.isAIWindowActive`, `isAIWindowNewTabPage`, `Glean.smartWindow.bookmarkbar.opened` (~880); ~1568-1572 `|| AIWindow.isAIWindowNewTabPage(uri)` | remove (required) |
| `browser/components/places/content/places-commands.js` | `AIWindow.isAIWindowActive(window)` (~18) | remove branch (required) |
| `browser/base/content/sanitizeDialog.js` + `browser/modules/Sanitizer.sys.mjs` | lazy `AIWindow`, `ChatStore`; `clearChatConversations()`; "history-downloads-and-chat" label switch | remove function and its call in the clear list; drop label branch (required) |
| `browser/base/content/browser-sync.js` (Sync, later feature) | lazy `AIWindow`; `browser/aiWindow.ftl` in the FxA l10n list (~1233); `_confirmFxaAndSyncDisconnect` picks `fxa-signout-dialog-body-aiwindow` | use `fxa-signout-dialog-body` only; drop import and FTL entry (required) |
| `browser/components/asrouter/modules/RemoteL10n.sys.mjs` + tests `unit/RemoteL10n.test.js` | `"browser/aiWindow.ftl"` in DOMLocalization list | remove entry and test expectations (required) |
| `browser/components/sessionstore/SessionStore.sys.mjs` | lazy `AIWindow`; `isAIWindow` in window state (~1774, 5449-5456, 5770, 5823, 7056-7066, 7519-7530); JSDoc | remove all branches; restored windows are always classic (required) |
| `browser/components/sessionstore/session.schema.json` | property `isAIWindow` | remove; `test/unit/data/sessionstore_complete.json` has the key, remove |
| `browser/components/tabbrowser/Tabbrowser.sys.mjs` | lazy `AIWindow`; icon map for `aiWindow.html` (~96); branches at ~817-829, 3242, 5768, 5829, 10919 (`getChatTabConversationId`, `isAIWindowContentPage`) | remove (required) |
| `browser/components/tabbrowser/NewTabPagePreloading.sys.mjs` | `AIWindow.isAIWindowActive` matching of preloaded browsers (~67-116) | remove |
| `browser/components/tabbrowser/TabMetrics.sys.mjs` | `SMART_WINDOW_GROUP_SUGGESTIONS`, `SMART_WINDOW_TASKS` | remove |
| `browser/components/tabbrowser/content/tab-context-menu.js` | `#context_createAITab`, `#context_askChat`, `#context_askChatSummarize` in layout lists (~77-86, 234-247, 286); `GenAI.buildTabMenu/buildTabSummarizeItem`, `AIWindowMenu.canShowAITabMenu` (~870-893); lazy `AIWindow`, `AIWindowMenu`, `GenAI` (~1512) | remove (required) |
| `browser/components/tabbrowser/content/tabgroup-menu.js` | lazy `AIWindow`, `AIWindowMenu`; `tabGroupEditor_createAITab` markup, handler, visibility (~112, 482, 544, 1033); import of `genai/content/model-optin.mjs` | remove createAITab pieces (required); re-point model-optin import (section 8) |
| `browser/base/content/main-popupset.inc.xhtml` / `main-popupset.js` | `context_createAITab`, `context_askChat`, `context_askChatSummarize` menu items; selection shortcut panel and "more actions" panel (`genai-shortcut-*`, `chat-shortcuts-options-panel`, `selection-more-actions-panel`, ~230-300); `context_createAITab` case and lazy `AIWindow` | remove |
| `browser/base/content/browser-context.inc.xhtml` / `browser-context.js` | `context-openlinksmartwindow`, `context-previewlink`, `context-create-aitab`, `<menu id="context-ask-chat"/>` and the 4 switch cases | remove (required) |
| `browser/base/content/nsContextMenu.sys.mjs` | lazy `AIWindow`, `AIWindowMenu`, `GenAI`, `LinkPreview`; `showSmartWindow` block (~528-536); preview link (~546, 2327-2330); `GenAI.buildAskChatMenu` (~928); `context-create-aitab` (~935); `openLinkInSmartWindow` (~1566); `createAITab` (~2662) | remove (required) |

### 3.3 Sidebar, Firefox View, preferences, UITour
| File | What | Action |
|---|---|---|
| `browser/components/sidebar/browser-sidebar.js` | map `viewGenaiChatSidebar:"aichat"`, `viewGenaiPageAssistSidebar:"aipageassist"`; two sidebar registrations (~195-230); `hideInAIWindow`, `isAIWindow()` (~107-114), `ai-window-state-changed` observer (~392, 669-708, 865); lazy `AIWindow` (~2782) | remove (required) |
| `browser/components/sidebar/sidebar-main.mjs` | lazy `GenAI`; `preview/genai.ftl` in l10n lists; `viewGenaiChatSidebar` tooltip/shortcut table (~97); `aichat` context menu builder (~391); click counters `genai`/`totalToolsMinusGenai`; provider-name tooltips (~745-773) | remove; keep generic counters if still used (required) |
| `browser/components/sidebar/sidebar-customize.mjs`/`.html`, `sidebar-panel-header.mjs` | `viewGenai*` rows, `chatbotEnabled` Glean, `preview/genai.ftl` | remove |
| `browser/components/sidebar/SidebarManager.sys.mjs` | `DEFAULT_LAUNCHER_TOOLS = "opentabs,bookmarks,aichat,history,syncedtabs,passwords"` | drop `aichat` |
| `browser/components/sidebar/metrics.yaml` | `sidebar.chatbot_icon_click`, `sidebar_customize.chatbot_enabled` | remove (regenerates FOG) |
| `browser/app/profile/firefox.js` | `sidebar.notification.badge.aichat` (2202) | remove |
| `browser/themes/shared/sidebar.css` | `[sidebarcommand="viewGenaiChatSidebar"]` rule | remove |
| `browser/components/firefoxview/jar.mn` | chats.mjs, chats-tab-list.{css,mjs}, empty-chat.svg, view-chats.svg | remove 5 lines (required) |
| `browser/components/preferences/OnDeviceModelManager.mjs` | lazy `AIWindow`, `GenAI`, `LinkPreview`; features `KeyPoints`, `Chatbot`, `SmartWindow` and their pref lists and controllers (~30-175) | remove the three features; leave Translations (already gone from earlier edit), PdfjsAltText, SmartTabGroups, SpeechRecognition for P6.1d (required) |
| `browser/components/preferences/config/aiFeatures.mjs` | lazy `AIWindow`, `ChatStore`, `GenAI`, `MemoryStore`, `Utils`; all `browser.smartwindow.*` and `browser.ml.chat.shortcuts.smartwindow` prefs/settings; `sidebarChatbotFieldset`, chatbot provider list (`GenAI.chatProviders`, `GenAI.init`); `aiControlLinkPreviewKeyPointsSelect`; the Smart Window, default-window, model, memories groups (~713-1270, 1494-1750); `BlockAiConfirmationDialog` smart-window copy | remove those settings/groups; keep global block toggle, pdfjs, smart tab groups, speech recognition (required) |
| `browser/components/preferences/config/tabs-browsing.mjs` | lazy `GenAI`, `LinkPreview`; prefs `browser.ml.linkPreview.*`, `browser.ml.chat.shortcuts`, `browser.highlightToSearch.featureGate`; settings `linkPreviewEnabled/KeyPoints/LongPress`, `chatbotShortcuts`-style setting using `GenAI.canOfferChatbot`; `Glean.selectionMenu.optionToggled` (~488); groups at ~797 and ~947 | remove (required) |
| `browser/components/preferences/main.js` | lazy `LinkPreview`; prefs `browser.ml.chat.provider`, settings `aiControlLinkPreviews`, `aiControlSidebarChatbot`, `aiControlSmartWindow`, provider setting | remove those (required for LinkPreview import) |
| `browser/components/preferences/preferences.js` | pane `personalizeSmartWindow` and `...ManageMemories` child pane (~388-431) | remove |
| `browser/components/preferences/preferences.xhtml` | `browser/genai.ftl`, `browser/aiFeatures.ftl` links | remove (aiFeatures.ftl has only Smart Window strings) |
| `browser/components/preferences/config/LegacyPaneMappings.mjs` | `general-link-preview` -> `tabsBrowsing/link-preview` | remove |
| `browser/locales/en-US/browser/preferences/preferences.ftl` | `preferences-ai-controls-sidebar-chatbot-*`, `...block-confirmation-sidebar-chatbot` (and link-preview ids if present) | remove |
| `browser/components/uitour/UITour.sys.mjs`, `UITour-lib.js` | lazy `AIWindow`; `showFirefoxAccountsForAIWindow` case and lib method; `aiControls` config keys `linkPreviewKeyPoints`, `sidebarChatbot`, `smartWindow` (keep the rest) | remove (required for the case); keys with the controls cleanup |
| `browser/components/uitour/test/browser.toml`, `browser_UITour_aiControls.js` | `browser_UITour_aiwindow.js` entry; aiControls expectations | remove entry; edit test |
| `browser/tools/mozscreenshots/.../Nova.sys.mjs` | sets/reads `browser.smartwindow.enabled` (~208, 654) | remove those lines |

### 3.4 Urlbar and search
| File | What | Action |
|---|---|---|
| `browser/components/urlbar/moz.build`, `jar.mn`, `tsconfig.json` | `SmartbarMentionsPanelSearch.sys.mjs`, `UrlbarProviderAiChat.sys.mjs`; four `Smartbar*.mjs` jar entries; `content/SmartbarInput.mjs` | remove (required) |
| `UrlbarProvidersManager.sys.mjs` | provider `UrlbarProviderAiChat` (module path), `"smartbar"` in every `supportedSAPs` | remove the provider; drop `"smartbar"` entries (required for provider) |
| `UrlbarPrefs.sys.mjs` | `smartbar.maxResults`, `smartbar.showSearchSuggestionsFirst`, `browser.smartwindow.agent.enabled`, `...smartbarMentions.loglevel`, `makeSmartBarGroups`, `case "smartbar"` result-group branch | remove |
| `UrlbarUtils.sys.mjs` | lazy `AIWindow`; `getBrowserWindow`-style helper `getSmartbarForWindow` branch (~1343) | remove (required) |
| `content/UrlbarInputBase.mjs` | lazy `AIWindow`; `windowMode` returns `"smartwindow"` (~859); `sapName == "smartbar"` branch (~5090); `WindowMode` typedef | simplify to classic/private (required) |
| `content/UrlbarShared.mjs`, `content/UrlbarView.mjs`, `content/UrlbarChildController.mjs`, `content/UrlbarChildTelemetry.mjs`, `content/UrlbarTelemetryUtils.mjs`, `UrlbarParentController.sys.mjs`, `ActionsProviderTabGroups.sys.mjs`, `UrlbarProviderSemanticHistorySearch.sys.mjs`, `actors/UrlbarChild.sys.mjs`, `types/urlbar.ts` | `HEURISTIC_AI_CHAT`, `UrlbarProviderAiChat` cases; smartbar tab-cycling, `smartbarData`/`smartbarExtras`, `aiwindow-input-cta:on-action` | remove (cleanup; dead once SAP is gone; telemetry code paths become unreachable but compile) |
| `urlbar/metrics.yaml` | extras `smartwindow` mode values; `smartbar` sap enums (~1609-1653) | edit descriptions/enums (cleanup, FOG regen) |
| `urlbar/docs/{preferences,telemetry,adding-a-search-bar}.md`, `search/docs/telemetry.md` | smartbar text | edit |
| `browser/components/search/BrowserSearchTelemetry.sys.mjs`, `search/metrics.yaml` | `smartbar`, `smartwindow_assistant` sources and counters | remove (FOG regen) |
| `toolkit/components/places/PlacesSemanticHistoryManager.sys.mjs` | pref `places.semanticHistory.smartwindow.featureGate`, `isEnabledForSmartWindow` getter and its uses (~282, 347, 429) | remove getter and gate; callers in urlbar (`UrlbarParentController` ~2243, `UrlbarProviderSemanticHistorySearch` ~108) use the classic gate (required) |
| `toolkit/components/places/tests/unit/test_canAddURI.js`, `urlbar/tests/unit/test_UrlbarPrefs.js`, `test_UrlbarProviderSemanticHistorySearch.js`, `test_providerTabToSearch.js`, `test_search_suggestions_tail.js`, `browser-engagementTelemetry/browser_glean_telemetry_engagement_sap.js`, `browser-telemetry/browser_zeroPrefix.js`, `quicksuggest/unit/test_quicksuggest_temporalResults.js`, `browser-quickactions/browser_general.js`, `tests/unit/xpcshell.toml` | smartbar/smartpage/ask-icon expectations | edit |

### 3.5 Autocomplete and Smart Form Fill
| File | What | Action |
|---|---|---|
| `toolkit/components/formautofill/FormAutofillParent.sys.mjs` | lazy getter loading `aiwindow/ui/modules/SmartFormFillAutocomplete.sys.mjs`; `smartFormFillPromise` in `_getAutocompleteEntries`-style merge (~74-85, 1171-1190) | delete getter, promise and merge term; keep relay entries (required) |
| `toolkit/components/passwordmgr/LoginManagerParent.sys.mjs` | same lazy getter; spread at ~853 | remove |
| `toolkit/components/satchel/FormHistoryParent.sys.mjs` | same lazy getter; `smartFormFillPromise` (~127-140) | remove |
| `toolkit/actors/AutoCompleteParent.sys.mjs` | `"SmartFormFill"` in the actor prefix list and mapping (~727, 774) | remove |
| `toolkit/components/satchel/FillHelpers.sys.mjs` | `item.style == "smartFormFill"` | remove branch |
| `toolkit/components/satchel/autocomplete-row-item/autocomplete-row-item.{mjs,css}` | `smartFormFill` type, loader and label renderers | remove |
| `browser/themes/shared/autocomplete.css` | `#PopupAutoComplete[resultstyles~="smartFormFill"]` rules + `--sff-loader-image` | remove |
| `toolkit/content/widgets/panel-list/panel-list.mjs` | comment mentions Smartbar | optional comment edit |
| tests: `satchel/test/browser/browser_autocomplete.js`, `satchel/megalist/.../browser_passwords_sidebar.js`, `passwordmgr/test/unit/test_login_autocomplete_result.js`, `browser/extensions/formautofill/test/browser/browser_autocomplete_footer.js`, `test/unit/test_profileAutocompleteResult.js` | smart-window icon/style cases | edit |

### 3.6 Messaging, onboarding, themes
| File | What | Action |
|---|---|---|
| `browser/components/asrouter/modules/ASRouter.sys.mjs` | lazy `SidebarChatBotPromo`, `SmartWindowNewTabPromo`, `AIWindow`; cases `sidebar_chatbot_promo`, `smart_window_newtab_promo`; `trigger.context.isAIWindow` (~2633) | remove (required) |
| `ASRouterTargeting.sys.mjs` | lazy `AgentPageDetector`, `AIWindow`, `AutoTabGroupingSuggestions`; `hasActiveAIWindow`, `isSmartWindowOnboarding`, other smart-window attributes; `addAIWindowTargeting()` wrapper (~1862-2076) | remove attributes; `addAIWindowTargeting` returns `targeting` unchanged (required) |
| `ASRouterTelemetry.sys.mjs`, `ASRouterParentProcessMessageHandler.sys.mjs`, `ActorConstants.mjs` | `SMART_WINDOW_PROMO_TELEMETRY`, `SIDEBAR_CHATBOT_PROMO_TELEMETRY`, `smart_window_promo_user_event`, `sidebar_chatbot_promo_user_event`, policy functions | remove |
| `ASRouterTriggerListeners.sys.mjs` | `tab.smartWindowActionSource` pass-through (~1298-1309) | remove |
| `OnboardingMessageProvider.sys.mjs`, `FeatureCalloutMessages.sys.mjs`, `PanelTestProvider.sys.mjs` | messages `SMARTWINDOW_*` (default promo, feedback modals, ToU, newtab/switcher/close-tab/sidebar callouts), `SPLIT_VIEW_SMART_WINDOW_CALLOUT`, `SIDEBAR_CHATBOT_PROMO_TEST`, genai survey/sidebar-customization callout strings | remove message objects; check `isAIWindow` helpers at PanelTestProvider ~3736 |
| `ASRouterAdmin.jsx` (`content-src/components/ASRouterAdmin`) | `"sidebar_chatbot_promo"` template list | remove; rebuilt bundle `content/asrouter-admin.bundle.js` is generated |
| `asrouter/moz.build`, `content-src/schemas/make-schemas.py`, `MessagingExperiment.schema.json`, `content/components/.../asrouter-newtab-message.mjs` (comment), `content/.../asrouter-newtab-multistage.css`, `content-src/styles/_feature-callout.scss`, `_multistage.scss` | module/schema registrations, `SmartWindowNewtabPromo`/`SidebarChatBotPromo` schema defs, `:root[ai-window]`, `.SMARTWINDOW_*` CSS | remove (css bundle files are generated from scss; edit both) |
| `asrouter/metrics.yaml` | `microsurvey.smart_window.*` | remove (FOG regen) |
| `browser/components/messagepreview/actors/AboutMessagePreviewParent.sys.mjs` | lazy `SidebarChatBotPromo`; template map `sidebar_chatbot_promo` | remove (required: import of deleted module) |
| `browser/components/aboutwelcome/modules/AboutWelcomeDefaults.sys.mjs` | screen `AW_SMART_WINDOW` and `FXA_AIWINDOW_SIGNIN_FLOW` actions (~259-370); campaign `smart_window` sets `browser.smartwindow.enabled` (~873) and backdrop (~941) | remove (required: pref write) |
| `aboutwelcome/modules/AboutWelcomeTelemetry.sys.mjs` | lazy `ChatUtils` (aiwindow); `smart_window_user_feedback_data` -> `Glean.microsurveySmartWindow` (~223-232) | remove (required) |
| `aboutwelcome/content/aboutwelcome.css`, `aboutwelcome/jar.mn` | `:root[ai-window]`, `.SMARTWINDOW_*`, `--mr-smart-window-background-gradient`; `mr-kit-smart-window.svg` | remove |
| `toolkit/components/messaging-system/lib/SpecialMessageActions.sys.mjs` | lazy `AIWindow`, `AIWindowUI`, `GenAI`; pref whitelist `browser.smartwindow.*` (~408-415); cases `FXA_AIWINDOW_SIGNIN_FLOW`, `OPEN_SMARTWINDOW_MONITOR_CREATE`, summarize-page and organize-tabs actions (~1097-1197) | remove (required) |
| `MessagingSystemBlocklists.sys.mjs` | `FXA_AIWINDOW_SIGNIN_FLOW` | remove |
| `messaging-system/schemas/SpecialMessageActionSchemas/{SpecialMessageActionSchemas.json,index.md}`, `TriggerActionSchemas/{TriggerActionSchemas.json,index.md}`, `.../test/browser/browser.toml` | action and `smartWindowNewTab` trigger schemas; the three deleted tests | edit |
| `browser/components/asrouter/tests/**` (`NimbusRolloutMessageProvider.sys.mjs`, `browser/browser_asrouter_menu_messages.js`, `browser_asrouter_targeting.js`, `browser_feature_callout_panel.js`, `browser_trigger_listeners.js`, `unit/ASRouter.test.js`, `unit/ASRouterTargeting.test.js`, `unit/TargetingDocs.test.js`, `xpcshell/head.js`, `xpcshell/test_OnboardingMessageProvider.js`) | expectations for the removed messages/attributes | edit |
| asrouter docs: `docs/{feature-callout,index,messaging-surfaces,targeting-attributes}.md` | smart-window attributes, chatbot promo links | edit |
| `toolkit/modules/LightweightThemeConsumer.sys.mjs` | `_isAIWindow`, `ai-window` MutationObserver, `toggleAIWindowMode`, branches (~230-614) | remove (required: `LightweightThemeManager.promiseAIThemeData` is also called) |
| `toolkit/mozapps/extensions/LightweightThemeManager.sys.mjs` | `promiseAIThemeData`, `promiseAINovathemeData`, caches pointing at `resource://builtin-themes/aiwindow*/` | remove (required) |
| `toolkit/mozapps/extensions/content/aboutaddons.html` / `aboutaddons.mjs` | `locales-preview/smartWindow-aboutAddons.ftl` link; `smartwindow-themes-notice` script and element creation (~79-82) | remove (required) |
| `toolkit/mozapps/extensions/test/browser/browser.toml` | `browser_html_smartwindow_themes_notice.js` | remove entry |
| `toolkit/locales/jar.mn` | `locales-preview/smartWindow-aboutAddons.ftl` | remove |
| `browser/locales/jar.mn` | `preview/genai.ftl`, `preview/aiWindow.ftl` | remove 2 lines (required) |
| `browser/themes/addons/{jar.inc,jar}.mn`, `moz.build` | built-in theme entries (jar.mn 8-12, moz.build 8-32) | remove (required) |

### 3.7 CSS and theme
- `browser/themes/shared/jar.inc.mn`: remove `aiWindowSidebar.css`, `smartbar.css`, `smartwindowGroupTabs.css`, `smartwindowMonitor.css` and the three `smart-window-*.svg` lines (required).
- `browser-shared.css`: remove 3 `@import` lines (32-34), `:root[ai-window]` (~309), `#ai-window-toggle` and switcher block (~1464-1630).
- `toolbarbuttons.css`: `--smartwindow-ask-btn-*` and ask-button rules; `toolbarbutton-icons.css` (~666-680): ids `smartwindow-ask-button-inner`, `smartwindow-group-tabs-button`, `smartwindow-monitor-button`.
- `tabbrowser/content-area.css`, `tabbrowser/fullscreen-and-pointerlock.css`, `urlbar/variables.css` (comment): `ai-window-open`, `ai-window-box`, `smartwindow-content` selectors.
- Strings/tests: `browser/base/content/test/browser-static/browser_all_files_referenced.js` (drop `aiwindow/assets/model-choice-0.svg` entry), `browser_parsable_css.js` (drop smartbar/ai-action-confirmation whitelist), `browser-contextMenu/browser_contextmenu.js` (3 link preview tests), `browser_contextmenu_linkopen.js` (smartwindow case), `browser-general/browser_bug417483.js` (`browser.ml.chat.menu`), `browser-utilityOverlay/browser_openWebLinkIn.js` (2 AI window tests), `browser/components/tabbrowser/test/browser/tabs/browser_tabContextMenu_altStructure.js` (chat provider), `smarttabgrouping/performance/browser_smart_tab_grouping_perf.js` (owner string only).

### 3.8 Profile upgrade, customize, misc JS
| File | What | Action |
|---|---|---|
| `browser/components/ProfileDataUpgrader.sys.mjs` | v152: `getBoolPref("browser.ml.chat.enabled")` without default; v153 `"aichat,..."`; v164 `browser.aiwindow.` -> `browser.smartwindow.` copy; v172 sets `places.semanticHistory.smartwindow.featureGate` | remove the four blocks (152 is required: pref read without default throws). Keep the version numbering gaps; the code skips versions fine. Delete the two `test_profileDataUpgrade_smartwindow*.js` tests and their `xpcshell.toml` entries |
| `browser/components/customizableui/CustomizableUI.sys.mjs` | `currentVersion < 27` block moving `smartwindow-group-tabs-button` beside `ai-window-toggle` | remove block (string only; safe to leave). Delete `test_organize_tabs_migration.js`; in `test_titlebar_spacer_migration.js` replace the arbitrary widget id `smartwindow-group-tabs-button` |
| `browser/actors/PromptParent.sys.mjs` | `chatbot-browser` group checks (~111, 125) | simplify (optional; see 6) |
| `toolkit/modules/ActorManagerParent.sys.mjs` | `"chatbot-browser"` in `messageManagerGroups` of the login-form actor (~456) | remove (optional) |
| `toolkit/components/contentanalysis/ContentAnalysisUtils.sys.mjs` | comments naming the GenAI chat shortcut | optional comment edit |
| `browser/components/StartupTelemetry.sys.mjs` | `aiControlBlocking` map | no change now (reads with `""` default; P6.1d) |
| `tools/m6e/acceptance.py` | widget list `"AI": ["ai-window-toggle", "smartwindow-group-tabs-button", "genai-chat-menu"]` (~131); `REMOVED_PREFS["AI"]` keys `browser.ml.chat.enabled`, `browser.smartwindow.enabled` (~144-145); `win.AIWindow?.isAIWindowEnabled` probe and "AI window is disabled" check (~556, 1420) | drop those entries and the check; keep `browser.ml.enable`, `browser.ai.control.*` (required: acceptance would fail 2 checks otherwise) |
| `tools/m6e/merge/check-helpers.py`, `acceptance.py` `REMOVED_ABOUT` | dormant about pages list `aichatcontent`, `smartpage`, `smartwindowtasks`, `smartformfillreview` | keep (still must not be registered) |

### 3.9 C++ and prefs
| File | What | Action |
|---|---|---|
| `modules/libpref/init/StaticPrefList.yaml` | `browser.smartwindow.aitab.enabled` | remove (touches StaticPrefs; rebuild) |
| `browser/components/about/AboutRedirector.cpp` | `smartpage` early return on `browser_smartwindow_aitab_enabled()` (~178) | remove `if` (page is already unregistered) |
| `docshell/base/BaseHistory.cpp` | `IsSmartPage()` and its use in `CanStore` | remove; scheme check reverts to upstream form |
| `dom/base/nsFrameLoader.cpp` | `kAllowedURIs` entry `chrome://browser/content/aiwindow/aiWindow.html` and its comment bullet (~2602, 2628) | remove entry |
| `dom/security/nsContentSecurityUtils.cpp` | `sImgSrcMozRemoteImageAllowList` `aiWindow.html`; `sImgSrcDataBlobAllowList` `aiWindow.html`, `genai/chat.html` | remove 3 lines |
| `dom/security/DOMSecurityMonitor.cpp` | `chrome://browser/content/aiwindow/components/ai-chat-message.mjs` in the innerHTML allowlist | remove |
| `dom/security/test/gtest/TestFilenameEvalParser.cpp` | sample path `moz-src:///browser/components/genai/LinkPreview.sys.mjs` | switch to any other `moz-src` module (string only, optional) |
| `toolkit/components/extensions/ExtensionPolicyService.cpp` | `chatbot-browser` message-manager group (~369-374) | remove group (optional; see 6) |

## 4. References from earlier features (ignore)
- VPN: `browser/components/ipprotection/content/vpn-site-rules-list.mjs` uses `chrome://browser/content/aiwindow/assets/warning.svg`. Gone with P6.1a.
- Translations: `browser/components/translations/tests/browser/browser_translations_*ai_feature*.js`, `toolkit/components/translations/tests/browser/{browser_translations_ai_feature.js,shared-head.js,browser_about_translations_enabling.js}`, `TranslationsFeature.sys.mjs` (`browser.ai.control.translations`). Gone with P6.1b.

References from LATER features that need no edit now (inert strings, or removed with their owner; record so the owners know):
- ML (P6.1d): `toolkit/components/ml/content/EngineProcess.sys.mjs` engine entries `smart-window-tab-topic`, `link-preview`, `aitab`, intent/search agents and an `@import` type line; `ModelHub.sys.mjs` `wllamapreview: "link-preview"`; `ml.d.ts`; `metrics.yaml` `link-preview` label; `aboutinference/content/aboutInference.js` `link-preview`; `AIFeature.sys.mjs` doc comment; ML tests that name `link-preview` (cache, utils, perf). `browser_ml_llama_smollm2_smoke.js` is the only one that hard-imports a deleted module (deleted above). `locales`: `toolkit/locales/en-US/toolkit/about/aboutAddons.ftl` `mlmodel-link-preview`.
- Sync (P6.1e): `services/sync/modules/engines/history.sys.mjs` `SMARTPAGE_URL_REGEX`; `services/sync/tests/unit/test_history_{store,tracker}.js` set `browser.smartwindow.aitab.enabled` (setting an undefined pref is harmless). `firefox.js` `services.sync.prefs.sync.browser.urlbar.smartbar.*` (1675-1678) belong with the pref removal below.
- Mozilla accounts (P6.1f): `services/fxaccounts/FxAccountsCommon.sys.mjs` scope service `smartwindow` (inert).
- Nimbus (P6.1i): `toolkit/components/nimbus/FeatureManifest.yaml` features `chatbot`, `linkPreviews`, `smartWindow`, `smartWindowAgentWatch`, `autoTabGrouping`, variables `highlightToSearchEnabled` (search feature) and `semanticHistorySmartwindowDistanceThreshold`. Recommend deleting these entries here (pure data) so the manifest has no dangling prefs; if Nimbus tests object, leave for P6.1i. `ExperimentAPI.sys.mjs`/`TargetingContextRecorder.sys.mjs` only read `browser.ai.control.default` (stays).

## 5. Prefs, metrics, strings to remove

Prefs (`browser/app/profile/firefox.js`):
- 2221-2256: all `browser.ml.chat.*` (enabled, hideLocalhost, maxLength, menu, page, page.footerBadge, page.menuBadge, prompt.prefix, prompts.0/1/3/4, provider, shortcuts, shortcuts.custom, shortcuts.smartwindow, shortcuts.longPress, shortcuts.debounce, shortcut.onboardingMouseoverCount, sidebar) and all `browser.ml.linkPreview.*`; 2258 `browser.ml.pageAssist.enabled`.
- 2260-2353: all `browser.smartwindow.*` (incl. the `#ifdef NIGHTLY_BUILD` `aitab.menus` block), `places.semanticHistory.smartwindow.featureGate`, `places.semanticHistory.smartwindow.distanceThreshold`.
- 933-940: `browser.highlightToSearch.{featureGate,enabled,search.enabled,copy.enabled}`.
- 605-609: `browser.urlbar.smartbar.*`; 1675-1678: their `services.sync.prefs.sync.*` twins; 2202: `sidebar.notification.badge.aichat`.
- Also in `UrlbarPrefs.sys.mjs` (defaults table) and `StaticPrefList.yaml` (`browser.smartwindow.aitab.enabled`).
- Keep: `browser.ml.enable` (ML), `browser.ai.control.*` (all.js 3620-3627; the `sidebarChatbot`, `linkPreviewKeyPoints`, `smartWindow` ones become inert, remove in P6.1d with the controls UI), `browser.tabs.groups.smart.*`.

`browser/app/profile/m6e.js`: remove `browser.ml.chat.enabled`, `browser.ml.chat.page`, `browser.ml.linkPreview.enabled`, `browser.smartwindow.enabled` (lines 142-145), `browser.smartwindow.aitab.enabled` (273), `browser.smartwindow.aitab.menus` (342). Keep the `browser.ai.control.*` and `browser.ml.enable` lines and update the comment above them in P6.1d.

`modules/libpref/docs/index.md`: example text mentioning `linkPreview.shift` (doc only).

Glean (all need FOG regeneration, so a `./mach build`, not `faster`): categories `genai.chatbot`, `genai.linkpreview`, `selection_menu`, `smart_window` (files deleted with the dirs); `microsurvey.smart_window` (`asrouter/metrics.yaml`); `sidebar.chatbot_icon_click`; `sidebar_customize.chatbot_enabled`; `search.*` smartbar/smartwindow_assistant counters and enum values; `urlbar` smartbar extras. Pings: none are defined by the feature (`smart_window_promo` and `sidebar_chatbot_promo` are ASRouter ping types in code only). Telemetry (Events/Scalars/Histograms.json): no hits.

FTL: files in 2.2; ids in `browserSets.ftl` (`ai-chatbot-sidebar-shortcut`), `sidebar.ftl` (`menu-view-genai-chat`, `sidebar-menu-genai-*`, `sidebar-menu-open/close-ai-chatbot-*`), `featureCallout.ftl` (`sidebar-genai-survey-*`, `link-preview-onboarding-callout-*`, chatbot mentions in sidebar-customization subtitle 2), `newtab/onboarding.ftl` (`onboarding-genai-sidebar-*`, "Smart window" sections, `smartwindow-onboarding-*`, `smartwindow-existing-user-fx-tou-*`), `preferences.ftl` (`preferences-ai-controls-sidebar-chatbot-*`, `...block-confirmation-sidebar-chatbot`), `toolkit/locales/en-US/toolkit/firefoxlabs/features.ftl` (`experimental-features-link-previews*`), `toolkit/locales/en-US/toolkit/branding/brandings.ftl` (`-smart-window-brand-name`, used only by deleted strings; remove after confirming no other user), `browser.ftl` (1 hit; check which id). Also `fxa-signout-dialog-body-aiwindow` dies with `aiWindow.ftl`. `browser/locales/en-US/browser/genai.ftl` `genai-model-optin-*` (3 ids) must move with `model-optin` (section 8). `scan-strings.py` should be run on the result.

Remote Settings / network: `ai-window-prompts` dump; collection `ai-window-aitab` (fetched live; no allowlist entry exists, so it is already blocked by D2); endpoints `mlpa-prod-prod-mozilla.freetls.fastly.net` (default `browser.smartwindow.endpoint`, `searchQuery.endpointURL`) disappear with the pref removal.

Python/l10n: `python/l10n/fluent_migrations/bug_2069149_genai_shortcut_button_tooltip.py` (delete); `bug_2050157_preferences_radio_group_accessible_names.py` lists `aiFeatures.ftl` as a source (historical; check, probably leave).

## 6. Build-system edits

| Edit | Kind |
|---|---|
| `browser/components/moz.build` DIRS; deleted dirs' `moz.build`, `jar.mn`, `aiwindow.manifest`, `MOZ_SRC_FILES`, `BROWSER_CHROME_MANIFESTS`, `MARIONETTE_MANIFESTS`, `XPCSHELL_TESTS_MANIFESTS`, `MOCHITEST_CHROME_MANIFESTS`, `TESTING_JS_MODULES`, `EXTRA_COMPONENTS` | front-end (`./mach build faster` handles moz.build/jar.mn/FTL changes; this changes the build config so run `./mach build` once to refresh backend) |
| `BrowserComponents.manifest` categories; `DesktopActorRegistry` | front-end |
| Remote Settings dump removal + `services/settings/dumps/main/moz.build` | front-end (dump list is data; `gen_last_modified.py` regenerates `last_modified.json`) |
| `browser/components/urlbar/{moz.build,jar.mn}`, `firefoxview/jar.mn`, `asrouter/moz.build`, `themes/addons/{moz.build,jar.mn}`, `themes/shared/jar.inc.mn`, `aboutwelcome/jar.mn`, `locales/jar.mn`, `toolkit/locales/jar.mn` | front-end |
| `toolkit/components/glean/metrics_index.py`, `metrics.yaml` removals | needs `./mach build` (FOG codegen, C++/Rust headers); not configure-time |
| `modules/libpref/init/StaticPrefList.yaml` + `AboutRedirector.cpp`, `BaseHistory.cpp`, `nsFrameLoader.cpp`, `nsContentSecurityUtils.cpp`, `DOMSecurityMonitor.cpp`, (`ExtensionPolicyService.cpp`, `TestFilenameEvalParser.cpp` optional) | C++ rebuild (StaticPrefs regenerated; incremental, no LTO/PGO in Phase 6) |
| configure, `mozconfig`, `moz.configure`, Cargo.toml/Cargo.lock, gkrust, `confvars.sh`, `package-manifest.in`, `allowed-dupes.mn` | none. No `MOZ_*`/`ENABLE_*` flag, no crate, no installer entry. |
| `taskcluster/kinds/perftest/*.yml`, `python/mozperftest/perfdocs/config.yml`, `mots.yaml`, lint ignore files, `tools/tryselect/try_presets.yml` | CI/lint only |

## 7. Tests to delete or edit

Delete: everything under the two dirs (`genai/tests/{xpcshell,browser,chrome}` 36 files; `aiwindow/ui/test/**` 272 files; `aiwindow/models/tests/**` 83 files; `aiwindow/services/tests`), plus the loose tests in 2.2.

Edit (remove the feature cases only):
- `browser/base/content/test/browser-contextMenu/browser_contextmenu.js` (3 link preview tests), `browser_contextmenu_linkopen.js`, `browser-general/browser_bug417483.js`, `browser-static/browser_all_files_referenced.js`, `browser_parsable_css.js`, `browser-utilityOverlay/browser_openWebLinkIn.js`, `browser-menubar/browser.toml`
- `browser/components/sidebar/tests/browser/`: `browser_a11y_sidebar.js`, `browser_extensions_sidebar.js`, `browser_glean_sidebar.js`, `browser_sidebar_context_menu.js`, `browser_sidebar_focus_ring.js`, `browser_sidebar_nimbus.js`, `browser_sidebar_prefs.js`, `browser_toolbar_sidebar_button.js`, `browser_tools_migration.js`, `browser_tools_overflow.js`, `browser_verticalTabs_widget_placements.js`, `browser_view_sidebar_menu.js`, `legacy/browser_sidebar_move.js`; `sidebar/tests/marionette/test_initialize_vertical_tabs.py` (default tools list includes `aichat`); `browser/components/tests/marionette/test_no_errors_clean_profile.py` (`viewGenaiChatSidebarKb` allowed-message entry)
- `browser/components/preferences/tests/browser.toml`, `aiFeatures/browser.toml`, `browser_aiFeatures.js`, `browser_aiFeatures_block_dialog.js`, `browser_aiFeatures_speech_recognition.js`, `browser_aiFeatures_telemetry.js`, `browser_aiFeatures_visibility.js`, `aiFeatures/head.js`, `experimental_features/*` (link previews)
- `browser/components/enterprisepolicies/tests/xpcshell/test_simple_pref_policies.js`, `browser/components/tests/unit/test_startupTelemetry_aiControlBlocking.js` only if the three AI-control keys are removed now (not required by this plan)
- `browser/components/uitour/test/browser_UITour_aiControls.js`, `browser.toml`
- `browser/components/aboutwelcome/tests/browser/browser_aboutwelcome_attribution.js`, `browser_aboutwelcome_multistage_languageSwitcher.js`, `tests/xpcshell/test_AboutWelcomeTelemetry.js`
- asrouter tests listed in 3.6; `tabbrowser/test/browser/tabs/browser_tabContextMenu_altStructure.js`
- `toolkit/components/places/tests/unit/test_canAddURI.js` (smartpage cases)
- `toolkit/components/satchel/test/browser/browser_autocomplete.js`, `satchel/megalist/.../browser_passwords_sidebar.js`, `passwordmgr/test/unit/test_login_autocomplete_result.js`, `browser/extensions/formautofill/test/{browser/browser_autocomplete_footer.js,unit/test_profileAutocompleteResult.js}`
- `browser/components/urlbar/tests/**` files in 3.4; `browser/components/sessionstore/test/marionette/manifest.toml` and `test/unit/data/sessionstore_complete.json`
- `browser/components/customizableui/test/unit/xpcshell.toml` (drop deleted test), `browser/components/tests/unit/xpcshell.toml` (drop two tests)
- `services/sync/tests/unit/test_history_{store,tracker}.js` belong to P6.1e (no edit now)
- `toolkit/components/ml/tests/browser/browser.toml` (drop smoke test entry)

## 8. Must stay (with proof)

1. `model-optin` component (`genai/content/model-optin.{mjs,css}`) and ids `genai-model-optin-continue|optout|cancel`. Needed by Smart Tab Groups in `tabgroup-menu.js` (`document.createElement("model-optin")`, ML feature, P6.1d). Plan: `git mv` the two files to `browser/components/tabbrowser/content/`, add `jar.mn` entries (`content/browser/tabbrowser/model-optin.mjs/.css`), change the import in `tabgroup-menu.js`, move the three FTL ids into a tabbrowser FTL already loaded by `browser.xhtml` (e.g. a tabbrowser FTL in `browser/locales/en-US/browser/`) and update the `insertFTLIfNeeded` call. P6.1d deletes it with Smart Tab Groups. Unreachability until then: `browser.tabs.groups.smart.enabled` and `userEnabled` are `false, locked` in `m6e.js` (lines 146-147), plus `browser.ai.control.smartTabGroups` blocked and locked; the optin UI is only started from the smart-suggestion flow.
2. AI-controls scaffolding (pref names `browser.ai.control.*`, `AIControls`/`GenerativeAI` policies, UITour `aiControls`, StartupTelemetry `aiControlBlocking`, Preferences "AI controls" pane skeleton, `AIFeature.sys.mjs`). Shared by pdfjs alt text, smart tab groups and speech recognition. No feature code remains behind the three AI-control prefs of this feature after the deletion. Proof of unreachability: `browser.ai.control.default`, `.sidebarChatbot`, `.smartWindow`, `.linkPreviewKeyPoints` are `"blocked", locked` in `m6e.js:133-140`; `acceptance.py` keeps asserting them. Removal scheduled for P6.1d (the pane then disappears with the ML engine).
3. Generic sidebar launcher, Firefox View shell, `PromptParent` sidebar logic, urlbar core, SessionStore, tab context menu infrastructure: stay, only the feature branches go.
4. `chatbot-browser` message-manager group (`ActorManagerParent.sys.mjs`, `PromptParent.sys.mjs`, `ExtensionPolicyService.cpp`): optional to remove. Only `genai/chat.js` sets the attribute, so nothing can create a browser in that group once the dir is gone; removing the C++ line is a small recompile of one file, keep it in the same C++ batch if desired.
5. `CustomizableUI.sys.mjs` v27 migration and the `ProfileDataUpgrader` blocks other than 152: pure placement/pref string logic with no module dependency; deletion is cleanup only. Both are version-gated and never run on new M6E profiles at their versions.
6. Dormant-page lists (`DORMANT_PAGES`, `REMOVED_ABOUT`) in `tools/m6e`: keep; they assert that the pages stay unregistered.
7. `services/fxaccounts` `smartwindow` scope and sync `SMARTPAGE_URL_REGEX`: owned by later features, inert.

Evidence that the feature is already unreachable on the baseline (to cite in P6.2): `m6e.js` locks `browser.ml.enable`, `browser.ml.chat.enabled`, `browser.ml.chat.page`, `browser.ml.linkPreview.enabled`, `browser.smartwindow.enabled` false; `browser.smartwindow.aitab.enabled/menus` false (user pref); the about pages `aichatcontent`, `smartpage`, `smartwindowtasks`, `smartformfillreview` are unregistered; `ai-window-toggle` and `smartwindow-group-tabs-button` are not in the default placements. Actors `AIChatContent`, `AISmartBar`, `AITab`, `AgentMonitorItem`, `SmartWindowTasks`, `SmartFormFillReview` have `enablePreference` gates; `GenAI` and `SmartFormFill` register through `onAddActor` pref observers; `LinkPreview` has `enablePreference: browser.ml.linkPreview.enabled`.

## 9. Risks and open questions

- Size: roughly 45 staying source files, 12 sidebar/preferences/urlbar tests, and many ASRouter data files change. Most edits are `AIWindow.isAIWindowActive()` branches; a missed lazy `AIWindow.*` call throws only at runtime on that path. After the edits run `git grep -n -E "AIWindow|GenAI|LinkPreview|SmartFormFill|aiwindow|genai|smartbar" -- browser toolkit services dom docshell modules testing tools` and expect only the section-4 items.
- `browser.xhtml` and other chrome documents load FTL files by name; a missing `browser/aiWindow.ftl`, `browser/genai.ftl`, `preview/*.ftl` logs errors and fails `browser_parsable_*`/l10n tests. All link removals must land in the same commit as the FTL deletions.
- `ProfileDataUpgrader` v152: the pref default is gone; the block must be removed or given a default, otherwise profile upgrade throws on startup. Same for any other no-default reads: `sidebar-main.mjs` (`browser.ml.chat.page`), `browser-sets.js` (`browser.ml.chat.enabled`), `DesktopActorRegistry` (all removed with their code).
- Unknown until built: whether `firefoxview.html`/`firefoxview.mjs` or `browser.js` loads the Chats view by a computed name (no static reference to `chats.mjs` found; only `jar.mn` and `FirefoxViewHandler.openTab("chats")`). Check `FirefoxViewHandler` and `browser/components/firefoxview/*` views list after deleting.
- `Sanitizer`: `item-history-downloads-and-chat` label id lives in a sanitize FTL file; verify it is only in `aiWindow.ftl` (it was found only through the dialog code), and that the clear-history items still pass `browser_sanitize*` tests.
- FOG codegen: metrics.yaml removals regenerate bindings, so a pure `faster` build will not pick them up. Do the C++ and Glean changes in one `./mach build`.
- Nimbus manifest edits (section 4) may break `toolkit/components/nimbus` tests if they enumerate features; keep them as a separate small commit that can be reverted or deferred.
- Open question 1: remove the AI-controls pref entries for `sidebarChatbot`, `linkPreviewKeyPoints`, `smartWindow` now (touches all.js, m6e.js, Policies, UITour, StartupTelemetry, acceptance.py, 4 tests) or defer to P6.1d. Recommended: defer.
- Open question 2: `toolkit/locales/.../brandings.ftl` `-smart-window-brand-name` and the whole `-smart-window` term: delete only after confirming `aiWindow*.ftl` deletion leaves no user (`featureCallout.ftl`, `onboarding.ftl` edits first).
- Open question 3: `tools/@types/generated` should be regenerated by the existing generator rather than hand-edited; confirm the command (`./mach` types target) before touching.
- Network: nothing in this feature has an allowlisted endpoint or listening socket (all calls go to `mlpa-prod-prod-mozilla.freetls.fastly.net`, user-configured custom endpoints and chat provider URLs, all behind the locked prefs). After deletion no code path remains that can open those connections.

## Critic addendum

Method: per-directory `git grep -I -l -i` for the identifier set (smartwindow, smart-window, smart_window, aiwindow, ai-window, genai, smartbar, linkpreview, link-preview, pageassist, aichat, chatbot, aitab, smartformfill, highlightToSearch) over browser, toolkit, services, dom, docshell, modules, testing, tools, python, taskcluster, build, devtools, remote, security, config, docs; every hit compared by basename against this report. Also: all 528 FTL message ids defined in the deleted FTL files checked against staying code; all `components/(aiwindow|genai)` / `content/(aiwindow|genai)` / `builtin-themes/aiwindow` path references (92 files) checked; Glean `Glean.<category>` users; actor and about: names. Result: the analysis is largely complete (the unmatched hits were globbed or noise such as webcompat `chatbot.weixin.qq.com`, puppeteer, test262, freetype `maichattawa*`, content-sharing `link-preview-list`, design-system storybook `.link-preview`). The items below were missed or are wrong.

### A. Wrong claim (must fix)

| File | Reference | Action |
|---|---|---|
| `tools/m6e/branding/fox-art.txt` | Section 3.1 says "ignore" for this file. It is not fixture data: `tools/m6e/branding/replace-fox-art.sh` reads it and `python3` opens each path, so a missing path aborts the script; `tools/m6e/merge/check-helpers.py` `check_fox_art` reports "listed in fox-art.txt but gone; replace-fox-art.sh stops on it". Ten lines name files this step deletes: `browser/components/aboutwelcome/assets/mr-kit-smart-window.svg`, `browser/components/aiwindow/ui/assets/{agent-watch-empty,agent-watch-kit,aitab-error-kit,kit-meditating,kit,model-choice-3,set-default-onboarding,smart-window-promo-default}.svg`, `browser/components/genai/assets/onboarding-link-preview-image-longpress.svg` | Delete those 10 lines in the same commit (required). |

### B. Missed references

| File | Reference | Action |
|---|---|---|
| `browser/base/content/browser-menubar.inc.xhtml` (`menu_newClassicWindow`, l10n `menu-file-new-classic-window`), `appmenu-viewcache.inc.xhtml` (`appMenu-new-classic-window-button`, l10n `appmenuitem-new-classic-window`), `browser-sets.inc.xhtml` (`<command id="Tools:ClassicWindow"/>`), `browser.js` (~1944 `#menu_newClassicWindow` hidden logic), `panelUI.js` (~1245 `appMenu-new-classic-window-button`), `browser-sets.js` (~306 `Tools:ClassicWindow`) | The report removes `menu_newAIWindow`/`appMenu-new-ai-window-button` and the `Tools:ClassicWindow` case but not the "New Classic Window" menu items themselves. Their l10n ids (`menu-file-new-classic-window`, `appmenuitem-new-classic-window`) exist only in `browser/locales/en-US/browser/aiWindow.ftl`, so keeping the items leaves untranslated labels and a duplicate of "New Window" | Remove the two items, the command, and the references in browser.js and panelUI.js (required once aiWindow.ftl goes). |
| `toolkit/modules/ActorManagerParent.sys.mjs` ~510 | `PageExtractor` actor `matches` contains `"about:smartpage?*"` | Remove that entry (the page does not exist). Safe string edit, not required to build. |
| `browser/components/asrouter/tests/browser/browser_multistage_spotlight_telemetry.js` | tests for `smart_window_user_feedback_data` (lines ~168-289) tied to the AboutWelcomeTelemetry change in 3.6 | Edit: drop the smart-window feedback cases together with `AboutWelcomeTelemetry.sys.mjs`. |
| `browser/components/sessionstore/test/unit/data/sessionstore_complete.json` | besides `isAIWindow`, the fixture holds preferences-settings records `linkPreviewEnabled`, `linkPreviewKeyPoints`, `linkPreviewLongPress` (~314-322) and `aiControlLinkPreviewKeyPointsSelect` (~534) | Edit the fixture only if the matching settings are removed from `tabs-browsing.mjs`/`main.js`; the fixture is data and does not break if it keeps them. |
| `stylelint-rollouts.config.js` | five entries, not four: `aiwindow/ui/components/ai-window/ai-window.css`, `genai/chat.css`, `genai/content/link-preview-card.css`, `genai/content/model-optin.css`, and `browser/themes/shared/aiWindowSidebar.css` (~157) | Remove all except `model-optin.css`, whose path must be rewritten to the relocated `browser/components/tabbrowser/content/model-optin.css`. |
| `browser/components/enterprisepolicies/Policies.sys.mjs` (`AIControls` ~161-185, `GenerativeAI` ~2174-2186) and `schemas/policies-schema.json` (~177-223, 2433-2446) | The policies still name `browser.ml.chat.enabled`, `browser.ml.chat.page`, `browser.ml.linkPreview.enabled`, `browser.ml.linkPreview.optin`, `browser.smartwindow.enabled` and the `SidebarChatbot`/`LinkPreviewKeyPoints`/`SmartWindow`/`Chatbot`/`LinkPreviews` keys. Report only says "keep scaffolding" | No edit now (a policy that locks an undefined pref is harmless, and `test_simple_pref_policies.js` ~1368-1406 keeps passing). Record the strings for P6.1d so they are not forgotten. |
| `tools/@types/subs/FeatureManifest.sys.d.mts` | hit for the identifier set (generated from the Nimbus manifest) | Regenerate or edit with the Nimbus manifest cleanup in section 4. |
| `python/l10n/fluent_migrations/bug_2050157_preferences_radio_group_accessible_names.py` | lists `aiFeatures.ftl` as a source (report: "probably leave") | Confirmed: historical, leave. |
| `browser/locales/en-US/browser/aiWindow.ftl` ids used by staying code outside the report's lists | `menu-history-chats`, `menu-history-chats-recent` (menubar/appmenu chat history, covered by `chatsHistoryMenu` removal), `firefoxview-chat*`/`firefoxview-chats-*` (die with `chats.mjs`), `fxa-signout-dialog-body-aiwindow` (browser-sync.js), `item-history-downloads-and-chat*` (`sanitizeDialog.js`, covered), `smart-window-model-*`/`ai-window-*` (aiFeatures.mjs, covered) | No new action. Verified every one of the 528 ids defined in the five deleted FTL files has a user only in files the report already lists, with the exception of the two classic-window ids above. |
| `browser/components/aboutwelcome/jar.mn` | `euo-chatbot.svg` | Unused-by-code welcome asset listed only in `browser_all_files_referenced.js`; not owned by this feature as a directory, leave unless the owner decides otherwise. |

### C. Claims checked and found correct

- Hazard 1 (`model-optin` imported by `tabgroup-menu.js`, line 28 import and 606 `createElement("model-optin")`; consumer test `browser_tab_grouping_telemetry.js`). Only other users of the `genai-model-optin-*` ids are in `genai.ftl`. Relocation plan is right; the stylelint-rollouts path (above) is the one extra place that names `model-optin.css`.
- Hazard 2 (`ProfileDataUpgrader.sys.mjs` line 798 `getBoolPref("browser.ml.chat.enabled")` with no default). Confirmed. Lines 801-819 (`aichat` tool handling) and 933-943, 989 are the other blocks.
- Section 4 "earlier feature" claims: VPN `vpn-site-rules-list.mjs` line 255 uses `chrome://browser/content/aiwindow/assets/warning.svg` (confirmed, in `browser/components/ipprotection`, gone with P6.1a); the translations `*ai_feature*` tests (confirmed). No other VPN or translations file references the feature.
- Section 8 "must stay" evidence: `m6e.js` 133-147 lock `browser.ai.control.*`, `browser.ml.enable`, `browser.ml.chat.enabled/page`, `browser.ml.linkPreview.enabled`, `browser.smartwindow.enabled`, `browser.tabs.groups.smart.enabled/userEnabled` (confirmed). `chatbot-browser` group is set only by `genai/chat.js` (confirmed; also read in `PromptParent.sys.mjs` 111/125, `ActorManagerParent.sys.mjs` 456, `ExtensionPolicyService.cpp` 370-374).
- Firefox View "Chats" open question: no static or computed reference exists. The only entry points are `FirefoxViewHandler.openTab("chats")` in `browser-sets.js` (~313) and the `jar.mn` rows. `firefoxview/moz.build` uses `EXTRA_JS_MODULES += ["*.sys.mjs"]`, so deleting `ChatsController.sys.mjs` needs no moz.build edit. No `firefoxview/metrics.yaml` or `triage.json` hit.
- Glean: only `browser-places.js:883`, `browser-sets.js:395`, `AboutWelcomeTelemetry.sys.mjs:226-229`, `aiFeatures.mjs`, `tabs-browsing.mjs:488` and their tests use the deleted categories (`smart_window`, `genai.chatbot`, `selection_menu`, `microsurvey.smart_window`); all are in the report. `genai.linkpreview` has no users outside the dir.
- No hits in Cargo, gkrust, moz.configure, mozconfig, confvars.sh, package-manifest.in, allowed-dupes.mn, devtools, remote (non-vendored), security (non-vendored), build, taskcluster beyond the three perftest files and two params fixtures, `eslint*.config.mjs`, `.eslintignore`, `.stylelintignore`.
- `mots.yaml` lines 2428-2429, 2445-2446; `.prettierignore` 1570; `tools/rewriting/Generated.txt` 5; `tools/tryselect/try_presets.yml` 22 and 28; `tools/lint/extra-js-modules/allowlist.txt` 39-40: all confirmed. `core_machine_learning_frontend` and `core_machine_learning_models` list only these paths in `includes`, so after deletion both submodules are empty: remove the two submodule entries entirely (not just the lines), as the report's section 3.1 says.
