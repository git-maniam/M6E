# P6.1e Sync: deletion analysis

Base commit cb002d4874. Read-only analysis. Feature order 5 of 9: VPN, Translations, GenAI and ML are already gone when this branch is integrated. Mozilla accounts (p6-1f), updater, crash reporter and Nimbus are still present.

## 1. Summary

Sync is `services/sync` (220 tracked files: 22 modules, 9 engines, 115 xpcshell tests, the TPS harness, the `about:sync-log` page, Glean metrics and ping). Deleting it is easy in itself. The cost is the front-end code around it, because three things are shared with Mozilla accounts:

1. `browser/base/content/browser-sync.js` is the `gSync` object (4322 lines). It holds Synced Tabs, Send Tab, the sync promo, the sync status button, and also the Mozilla accounts toolbar/avatar panel, the profiles submenu telemetry and the Monitor/Relay/VPN CTA menu. It imports `Weave`, `UIState` and `SyncedTabs` at the top. There are about 60 call sites of `gSync` in staying code (context menus, tab context menu, panelUI, navigator-toolbox, share panel, aboutlogins, asrouter, profiles).
2. `services/sync/modules/UIState.sys.mjs` is the Sync/FxA state broker. It is used by 12 files that are mostly FxA UI (AccountsGlue, MenuMessage, SpecialMessageActions, UITour, BackupService, aboutlogins, trust panel, newtab PrivacyFeed, megalist, preferences). It imports `Weave`.
3. The "sync" pane in about:preferences (`config/account-sync.mjs`) is the default pane of the settings redesign (`browser.settings-redesign.enabled` is true in firefox.js). It also hosts Default Browser, Import, Profiles, Backup and Referrals, so the file cannot be deleted whole.

Recommended boundary (needs the orchestrator's agreement, see section 9): the 1e branch deletes `browser-sync.js` and removes every `gSync`, `Weave`, `UIState`, `SyncedTabs` and `services-sync` reference from staying code. The pure-FxA pieces that do not touch Sync (services/fxaccounts, the `appMenu-fxa-*`/`PanelUI-fxa*` DOM that is not Sync or Send Tab, fxa.ftl strings, `identity.fxaccounts.*` prefs, FxA web channel) stay for 1f. Because FxA is already locked off (`identity.fxaccounts.enabled` false, locked, in `browser/app/profile/m6e.js`), `gSync.init()` already returns early via `onFxaDisabled()` and nothing visible changes.

Things that must stay (details in section 8): `PlacesSyncUtils.sys.mjs` and the Places sync columns (used by `Bookmarks.sys.mjs` change tracking), login `syncCounter`/`everSynced`/sync-ID APIs, the Rust `sync15`, `webext-storage`, `logins` and `autofill` crates, the `storage.sync` extension API (local Rust store), `services/common` and `services/crypto` (FxA and Remote Settings consumers), and the pref behind `BrowserUtils.getShareableURL` (it is named `services.sync.engine.tabs.filteredSchemes` but is used by the macOS Share menu).

Scale: about 330 files deleted, about 70 staying files edited, about 40 test files deleted or edited outside the feature directories.

## 2. Files to delete

### Directories (git rm -r)

| Path | Files | Note |
|---|---|---|
| `services/sync` | 220 | Whole tree: modules, modules-testing, content (`aboutSyncLog.*`), docs, tests/unit, tests/browser, tests/tps, tps (TPS add-on), `Weave.sys.mjs`, `SyncComponents.manifest`, `components.conf` (contract `@mozilla.org/weave/service;1`), `pings.yaml` (`sync` ping), `modules/metrics.yaml` |
| `browser/components/syncedtabs` | 22 | Old Synced Tabs sidebar, widgets, stores, jar.mn, tests. `EventEmitter.sys.mjs` is used by `browser/modules/webrtcUI.sys.mjs`: change that import to `resource://gre/modules/EventEmitter.sys.mjs` (same on/off/emit API, `new EventEmitter()`), then delete |
| `browser/base/content/test/browser-sync` | 17 | gSync tests |
| `browser/components/preferences/tests/sync` | 6 | Sync pane tests |
| `toolkit/components/places/tests/sync` | 23 | Bookmark mirror and `PlacesSyncUtils` tests (the `test_sync_utils.js` file tests the module that stays, see section 8; either move it to `tests/unit` or drop it with the mirror tests; recommended: keep a trimmed copy) |
| `testing/tps` | 12 | Sync test harness (`tps-build`, `tps-test`) |
| `browser/components/preferences/widgets/sync-device-name` | 2 | |
| `browser/components/preferences/widgets/sync-engine-list` | 3 | |
| `browser/themes/shared/syncedtabs` | 1 | `sidebar.css` |
| `toolkit/components/places/bookmark_sync` | 5 | First-party Rust crate (`SyncedBookmarksMerger`), only user is the bookmark mirror |

### Loose files

Front-end, browser/:
- `browser/base/content/browser-sync.js`
- `browser/components/customizableui/content/sync-promo.mjs`, `sync-promo.css`
- `browser/components/customizableui/test/browser_synced_tabs_menu.js`, `browser_remote_tabs_button.js`, `browser_history_sync_promo.js`, `browser_bookmarks_sync_promo.js`
- `browser/components/firefoxview/SyncedTabsController.sys.mjs`, `firefox-view-synced-tabs-error-handler.sys.mjs`, `firefox-view-tabs-setup-manager.sys.mjs`, `content/synced-tabs-empty.svg`, `content/synced-tabs-error.svg`
- `browser/components/sidebar/sidebar-syncedtabs.html`, `sidebar-syncedtabs.mjs`, `tests/browser/browser_syncedtabs_sidebar.js`, `browser_syncedtabs_sidebar_glean.js`
- `browser/components/urlbar/UrlbarProviderRemoteTabs.sys.mjs` and its tests (`tests/unit/test_remote_tabs.js`, `tests/browser-tabs/browser_remotetab.js`, `tests/browser-telemetry/browser_remotetab.js`)
- `browser/components/preferences/sync.js`, `sync.inc.xhtml`, `fxaPairDevice.js`, `fxaPairDevice.xhtml`, `dialogs/syncChooseWhatToSync.js`, `dialogs/syncChooseWhatToSync.xhtml`, `tests/chrome/test_sync_device_name.html`
- `browser/components/profiles/tests/browser/browser_fxa_menu_profiles_telemetry.js` (tests `Glean.fxaAvatarMenu` profile metrics that live in the deleted metrics.yaml)
- `browser/components/uitour/test/browser_UITour_sync.js`
- `browser/components/sharing/tests/browser/browser_testSharePanelSendToDevice.js`
- `browser/extensions/formautofill/test/unit/test_sync.js`, `test_sync_deprecate_address_x_name_fields.js`, `test_sync_deprecate_credit_card_v4.js`, `test_rust_autofill_address_sync_engine.js`, `test_rust_autofill_credit_card_sync_engine.js`, `test_rust_autofill_address_bridged_engine.js`, `test_rust_autofill_credit_card_bridged_engine.js`, `test_storage_syncfields.js` (check this one: it may test storage fields that stay), `test/browser/creditCard/browser_creditCard_doorhanger_sync.js`
- Themes: `browser/themes/shared/fxa/sync-devices.svg`, `sync-illustration.svg`, `sync-illustration-issue.svg`, `sync-promo-*.svg` (4), `browser/themes/shared/icons/synced-tabs.svg`, `browser/themes/shared/preferences/category-sync.svg` only if the category button is removed (it is not, see section 3), `icons/sync.svg` after a check that nothing else uses it. Keep `icons/send-tab.svg` unless the share panel is edited to drop it (its only users are `#send-tab-button` and the share panel)
- Locales: `browser/locales/en-US/browser/sync.ftl`, `syncedTabs.ftl`, `browser/locales/en-US/browser/preferences/fxaPairDevice.ftl`, `toolkit/locales/en-US/services/aboutSyncLog.ftl`
- Docs: `docs/rust-components/api/js/tabs.md` only if the `tabs` crate goes (section 6, R2)

Toolkit:
- `toolkit/components/formautofill/FormAutofillSync.sys.mjs` (only loaded by `services/sync/modules/service.sys.mjs`)
- `toolkit/components/extensions/ExtensionStorageSyncKinto.sys.mjs` (imports `resource://services-sync/*`; the Kinto backend is off by default, `webextensions.storage.sync.kinto` is false)
- Extension tests: `test/xpcshell/test_ext_storage_sync_kinto.js`, `test_ext_storage_sync_kinto_crypto.js`, `test_ext_storage_content_sync_kinto.js`, `test_StorageSyncService.js` (imports the sync engine), `head_sync.js` (check consumers first), plus the matching entries in `xpcshell-common.toml`
- `toolkit/components/places/SyncedBookmarksMirror.sys.mjs`, `SyncedBookmarksMirror.h`, `mozISyncedBookmarksMirror.idl`
- `toolkit/components/places/tests/bookmarks/test_sync_fields.js` only if it tests mirror behaviour (it tests `syncStatus` fields on bookmarks, which stay; keep)

## 3. References from staying code (edit, remove or replace)

Convention: "drop" = delete the line or block; every deleted `lazy.X` getter must also lose its uses. UIState rows assume the options in section 9 (A = remove use, B = relocate).

### 3a. Build, registration and manifests

| File | What | Action |
|---|---|---|
| `services/moz.build` | `if CONFIG["MOZ_SERVICES_SYNC"]: DIRS += ["sync"]` | drop |
| `services/docs/index.md` | toctree entry `sync/index` | drop |
| `browser/components/moz.build` | `"syncedtabs"` in DIRS | drop |
| `browser/base/jar.mn` | `content/browser/browser-sync.js` | drop |
| `browser/base/moz.build` | `content/test/browser-sync/browser.toml` | drop |
| `browser/base/content/moz.build` | `with Files("test/browser-sync/**")` and `Files("browser-sync.js")` | drop |
| `browser/components/BrowserComponents.manifest` | `category browser-window-idle-tasks .../browser-sync.js gSync.init` (line 108) and `browser-window-unload .../browser-sync.js gSync.uninit` (line 150) | drop both (a category entry pointing at a missing script logs errors at every window) |
| `browser/components/preferences/jar.mn` | `sync.js`, `config/account-sync.mjs` (kept, edited), `widgets/sync-device-name.mjs`, `sync-engines-list.mjs`, `sync-engines-list.css` | drop all but account-sync |
| `browser/components/preferences/dialogs/jar.mn` | `syncChooseWhatToSync.xhtml/.js` | drop |
| `browser/components/preferences/moz.build` | `tests/sync/browser.toml` | drop |
| `browser/components/preferences/preferences.xhtml` | script tags for `sync-device-name.mjs`, `sync-engines-list.mjs`; `#include sync.inc.xhtml` (line 260) | drop. Keep `<moz-page-nav-button id="category-sync">`, the pane is the redesign landing pane |
| `browser/components/customizableui/content/jar.mn` | `sync-promo.mjs`, `sync-promo.css` | drop |
| `browser/components/firefoxview/jar.mn` | `synced-tabs-empty.svg`, `synced-tabs-error.svg` | drop |
| `browser/components/sidebar/jar.mn` | `sidebar-syncedtabs.html/.mjs` | drop |
| `browser/themes/shared/jar.inc.mn` | lines for `category-sync.svg` (keep, see above), `fxa/sync-*.svg` (7), `synced-tabs.svg`, `syncedtabs/sidebar.css` | drop the deleted files' lines |
| `toolkit/content/jar.mn` | `aboutSyncLog.css/.html/.mjs` from `/services/sync/content/` (lines 27-29) | drop |
| `toolkit/components/places/moz.build` | `mozISyncedBookmarksMirror.idl`, `SyncedBookmarksMirror.h` in EXPORTS, `SyncedBookmarksMirror.sys.mjs` in EXTRA_JS_MODULES; keep `PlacesSyncUtils.sys.mjs` | drop the three |
| `toolkit/components/places/components.conf` | class `@mozilla.org/browser/synced-bookmarks-merger;1` (lines ~105-110) | drop |
| `toolkit/components/places/tests/moz.build` | `sync/xpcshell.toml` | drop |
| `toolkit/components/formautofill/moz.build` | `FormAutofillSync.sys.mjs` | drop |
| `toolkit/components/extensions/moz.build` | `ExtensionStorageSyncKinto.sys.mjs` (line 37) | drop |
| `toolkit/components/extensions/test/xpcshell/xpcshell-common.toml` | entries for the deleted Kinto tests and `test_StorageSyncService.js` (lines ~742-755) | drop |
| `docshell/base/nsAboutRedirector.cpp` | `{"sync-log", "chrome://global/content/aboutSyncLog.html", ...}` (line 186) | drop (C++) |
| `docshell/build/components.conf` | `'sync-log'` in the about modules list (line 30) | drop (C++) |
| `dom/security/nsContentSecurityUtils.cpp` | allowlist entries `syncChooseWhatToSync.xhtml` (1264), `fxaPairDevice.xhtml` (1266), `sidebar-syncedtabs.html` (1316, 1358), `syncedtabs/sidebar.html` (1360, 1406) | drop (C++) |
| `browser/components/DesktopActorRegistry.sys.mjs` | `matches` lists with `chrome://browser/content/syncedtabs/sidebar.html` (693) and `.../sidebar/sidebar-syncedtabs.html` (699) | drop both lines |
| `browser/components/glean`-style index: `toolkit/components/glean/metrics_index.py` | `services/sync/modules/metrics.yaml` (line 67), `services/sync/pings.yaml` (line 234) | drop both |
| `toolkit/components/glean/tags.yaml` | tag `'Firefox :: Sync'` | drop (only the deleted metrics use it) |
| `toolkit/library/rust/gkrust-features.mozbuild` | `if CONFIG["MOZ_PLACES"]: gkrust_features += ["moz_places"]` (line 42-43) | drop (Rust, R1) |
| `toolkit/library/rust/shared/Cargo.toml` | `bookmark_sync` optional dep (47), feature `moz_places = ["bookmark_sync"]` (162) | drop (R1) |
| `toolkit/library/rust/shared/lib.rs` | `#[cfg(feature = "moz_places")] extern crate bookmark_sync;` (16-17) | drop (R1) |
| `Cargo.lock` | package `bookmark_sync` (688) and its entry in the gkrust-shared dependency list (3008) | drop. `dogear` stays: the vendored `places` crate also depends on it |
| `python/mach/mach/command_util.py` | `tps-build`, `tps-test` references to `testing/tps/mach_commands.py` (248-249) | drop |
| `docs/config.yml` | `testing/tps` (79) | drop |
| `tools/lint/md.yml` | `testing/tps/docs/` (20) | drop |
| `python/mozbuild/mozbuild/action/test_archive.py` | `tps` common/tests package entries (151-161) | drop |
| `python/mozbuild/mozpack/chrome/manifest.py` | docstring example `resource services-sync ...` (248) | optional, comment only |
| `build/sparse-profiles/tps` | whole file | delete |
| `taskcluster/kinds/addon/kind.yml` | `tps-xpi` job (lines 20-49) | drop |
| `taskcluster/kinds/merge-automation/kind.yml` | `services/sync/modules/constants.sys.mjs` version-bump entry (75) | drop |
| `mots.yaml` | `services/sync/**/*` module (4672-4680) | drop the module |
| `tools/use-moz-src/mach_commands.py` | `services/sync/` special case (128-129) | drop |
| `tools/ts/config/fixed_paths.js` | `resource://services-sync/util.sys.mjs` and `resource://testing-common/services/sync/utils.sys.mjs` (61-62, 86-87) | drop |
| `tools/@types/generated/*` (`tspaths.json`, `lib.gecko.modules.d.ts`, `lib.gecko.xpcom.d.ts`, `lib.gecko.glean.d.ts`, `browser-window-globals.d.ts`) | generated type maps | regenerate or hand-drop the sync entries; non-blocking |

### 3b. Browser window and front-end code

| File | What | Action |
|---|---|---|
| `browser/base/content/browser.js` | lazy `Weave` (126), `TabsSetupFlowManager` (104-105), `gSync` lazy script getter (257-258), `customElements.setElementCreationCallback("sync-promo", ...)` (652-656), `gSync.maybeUpdateUIState()` in the FxA toolbar toggle (873), `FirefoxViewHandler`: `SyncedTabs` getter in `init()` (5148), `_closeDeviceConnectedTab()` (5201-5227), `_onTabForegrounded()` (5229-5233) and their callers | drop. Check callers of `_closeDeviceConnectedTab` and `_onTabForegrounded` in the same handler |
| `browser/base/content/browser.js.globals` | `gSync` (183), `TabsSetupFlowManager` (138) | drop |
| `browser/base/content/browser.xhtml` | `<link rel="localization" href="browser/syncedTabs.ftl"/>` (99) | drop. `sync.ftl` is inserted by `insertFTLIfNeeded` in gSync only |
| `browser/base/content/browser-context.js` | `context-sendlinktodevice-popup` and `context-sendpagetodevice-popup` cases (`gSync.populateSendTabToDevicesMenu`, 311-330), `gSync._resetSendTabExposureTracking()` (346) | drop |
| `browser/base/content/browser-context.inc.xhtml` | `context-sendlinktodevice`, `context-sep-sendlinktodevice`, `context-sep-sendpagetodevice`, `context-sendpagetodevice` (141-148, 484-491) | drop |
| `browser/base/content/nsContextMenu.sys.mjs` | `initSyncItems()` and its callers, `syncItemsShown` (1322-1324) | drop |
| `browser/base/content/navigator-toolbox.js` | `case "fxa-toolbar-menu-button": gSync.toggleAccountPanel(...)` (162, 449) | drop both cases. The button DOM in `navigator-toolbox.inc.xhtml` (476-490) is FxA, left to 1f |
| `browser/components/customizableui/content/panelUI.js` | `#appMenu-nova-fxa-label` click (473), `appMenu-fxa-label2` command (505) calling `gSync.toggleAccountPanel` | drop both |
| `browser/base/content/browser-menubar.js` | `sync-tabs-menuitem`, `historyRemoteTabsPromo`, `sync-setup`, `sync-enable`, `sync-unverifieditem`, `sync-syncnowitem`, `sync-reauthitem` handlers (43-67) | drop |
| `browser/base/content/browser-menubar.inc.xhtml` | menu items `sync-tabs-menuitem`, `historyRemoteTabsPromo` (223-226), `sync-setup`..`sync-reauthitem` (362-382) | drop |
| `browser/base/content/browser-places.js` | `gSync.getSyncPromoState(["tabs"])` (724), `gSync.handleSyncPromoAction` (887), `gSync.getSyncPromoState(["bookmarks"])` (2058) | drop the promo logic; also the `synced-tabs` menuitem hide comment (722) |
| `browser/base/content/main-popupset.js` | `syncedTabsManageDevices` case (433-435), `containerSyncedTabsPopup` listeners (563-572), `SyncedTabsOpenSelectedInContainerTabMenu` case (690), `context_sendTabToDevicePopupMenu` case (646-650), `gSync._resetSendTabExposureTracking()` (770), and the `sidebar-synced-tabs-context-*` handlers | drop |
| `browser/base/content/main-popupset.inc.xhtml` | `context_sendTabToDevice` (118-124), `sidebar-menu-synced-tabs` item (409-410), `sidebar-synced-tabs-context-menu` (501-546), `SyncedTabsSidebarContext` and `SyncedTabsSidebarTabsFilterContext` (933-978) | drop |
| `browser/base/content/appmenu-viewcache.inc.xhtml` | `PanelUI-history-sync-promo` (284), `PanelUI-bookmarks-sync-promo` (358), `PanelUI-remotetabs` panelview (524-655), `PanelUI-fxa-device-recent-tabs` (806-823), `PanelUI-fxa-menu-secure-sync-subpanel` (825-853), `PanelUI-fxa-menu-all-devices` (857-873), `PanelUI-fxa-menu-sendtab-*` panelviews (918-960) | drop (Sync/Send Tab specific). The remaining `appMenu-fxa-*`, `PanelUI-fxa` main view and profiles views are FxA and profiles, left to 1f |
| `browser/components/tabbrowser/content/tab-context-menu.js` | `#context_sendTabToDevice` and separator in two visibility arrays (106-107, 262-263), `gSync.updateTabContextMenu(...)` and the alt-label block (1101-1109) | drop |
| `browser/components/customizableui/CustomizableWidgets.sys.mjs` | `sync-button` widget block under `identity.fxaccounts.enabled` (527-605), `send-tab-button` widget (607-700) | drop both |
| `browser/components/customizableui/CustomizableUI.sys.mjs` | `"sync-button"` in the legacy default-placements list (632) | drop |
| `browser/components/BrowserGlue.sys.mjs` | lazy `WeaveService` (113-117), `if (prefHasUserValue("services.sync.username")) WeaveService.init()` (734-737), idle task `WeaveService` (1126-1136) | drop all three |
| `browser/components/places/PlacesUIUtils.sys.mjs` | lazy `CLIENT_NOT_CONFIGURED` and `Weave` (12, 26), `shouldShowTabsFromOtherComputersMenuitem()` (1041-1046), `sidebar-synced-tabs-context-menu` cases (1432, 1441, 1498) | drop. Remove callers of `shouldShowTabsFromOtherComputersMenuitem` (browser-menubar.js, bookmarks/history menus) |
| `browser/components/sidebar/browser-sidebar.js` | `viewTabsSidebar: "syncedtabs"` (21), the `viewTabsSidebar` sidebar definition (150-167) | drop |
| `browser/components/sidebar/SidebarManager.sys.mjs` | default tool list string `...,syncedtabs,passwords` (30) | edit string |
| `browser/components/sidebar/sidebar-customize.mjs` | `["viewTabsSidebar", ...]` (21), `case "viewTabsSidebar"` + `Glean.sidebarCustomize.syncedTabsEnabled` (155-156) | drop |
| `browser/components/sidebar/sidebar-page.mjs` | `sidebar-synced-tabs-context-*` cases (225-258) | drop |
| `browser/components/sidebar/sidebar.css` | `&.synced-tabs` (140) | drop |
| `browser/components/ProfileDataUpgrader.sys.mjs` | migration string `"aichat,syncedtabs,history"` (819) | leave (a one-time migration of stored values) or remove token; low value |
| `browser/components/sharing/SharePageAction.sys.mjs` | `gSync` use for send-tab targets in the share panel (191, 312-396) | drop the "send to device" section of the share panel; keep the macOS native share items |
| `browser/components/sharing/SharingUtils.sys.mjs` | `window.gSync.openPairDevice`, `sendTabsAndConfirm`, target lookup (396-414) | drop the send-to-device helpers |
| `browser/components/urlbar/UrlbarProvidersManager.sys.mjs` | registration of `UrlbarProviderRemoteTabs` (151-153) | drop |
| `browser/components/urlbar/moz.build` | `UrlbarProviderRemoteTabs.sys.mjs` (47) | drop |
| `browser/components/urlbar/UrlbarPrefs.sys.mjs`, `firefox.js` | `suggest.remotetab` pref default (599) and `browser.urlbar.suggest.remotetab` (firefox.js 428) | drop the pref and any Settings checkbox that binds it. Keep the `REMOTE_TAB` result type in `UrlbarShared.mjs`, `UrlbarView.mjs`, muxer and `metrics.yaml` (generic result plumbing, unreachable without a provider; trimming it is churn in a hot area) |
| `browser/modules/webrtcUI.sys.mjs` | `import { EventEmitter } from "moz-src:///browser/components/syncedtabs/EventEmitter.sys.mjs"` (5) | change to `resource://gre/modules/EventEmitter.sys.mjs` |
| `browser/modules/BrowserUsageTelemetry.sys.mjs` | `"sync-device-name"` in the widget id list (90) | drop |
| `browser/components/firefoxview/viewpage.mjs`, `card-container.mjs`, `firefoxview.css` | `syncedtabs-tab-list` selectors (viewpage 202, card-container 118) and `&.synced-tabs::part` (css 154) | drop the token |
| `browser/components/firefoxview/metrics.yaml` | `fxa_continue_sync`, `fxa_mobile_sync`, `synced_tabs_tabs`, `syncedtabs` in an allowed-values list (212-261, 405) | drop. Only SyncedTabsController and the setup manager record them |
| `browser/components/sidebar/metrics.yaml` | `synced_tabs_icon_click`, category `synced_tabs`, `sidebar_customize.synced_tabs_enabled`, `sidebar_synced_tabs`, the `synced_tabs` value in a list (141-191, 334-356, 466, 806) | drop |
| `browser/modules/metrics.yaml` | `preferences_pane_sync` (1323-1339) | drop; also its Scalars mirror `browser.ui.interaction.preferences_paneSync` if present in `toolkit/components/telemetry/Scalars.yaml` |
| `browser/themes/shared/customizableui/panelUI-shared.css` | rules for `#PanelUI-remotetabs*`, `.PanelUI-remotetabs-*`, `#PanelUI-fxa-menu-syncnow-button`, `.fxa-menu-secure-sync*`, `#PanelUI-fxa-device-recent-tabs`, `.fxa-menu-sync-status-*`, `#PanelUI-sendTabToDevice*`, `--panel-ui-sync-illustration-height` (lines 798-799, 979, 1051-1143, 1388-1396, 1424-1553, 1927-1940) | drop the Sync/Send Tab selectors; leave `PanelUI-fxa-menu-*` rules that serve FxA and profiles |
| `browser/themes/osx/customizableui/panelUI.css` | `.PanelUI-remotetabs-notabsforclient-label`, `#PanelUI-remotetabs-tabslist` (15-35) | drop |
| `browser/themes/shared/toolbarbutton-icons.css` | `#sync-button`, `#send-tab-button` (97-102) | drop |
| `browser/themes/shared/sidebar.css` | `synced-tabs.svg` rule (264) | drop |
| `browser/themes/shared/preferences/preferences.css` | `#syncing` (719-724), `.sync-engines-list*`, `.sync-engine-*` (788-866) | drop |

### 3c. Preferences pane

| File | What | Action |
|---|---|---|
| `browser/components/preferences/config/account-sync.mjs` | `SCOPE_APP_SYNC` import, `Weave` lazy, `Preferences.addAll` for `services.sync.engine.*`, `SyncHelpers`, groups `accountDisabled`, `account`, `sync` (lines 842-1063), uses of `gSync.disconnect` (192, 264) | Edit, do not delete: keep `importBrowserData`, `profilePane`, `profiles`, `backup`, `referrals` (1064-1178). Remove the three account/sync groups and their helpers |
| `browser/components/preferences/preferences.js` | `Weave` import (58), `FxAccounts` imports (61-64, FxA, 1f), `UIState` lazy (119), `sync` pane `groupIds` `"accountDisabled","account","sync"` (478-483), legacy branch `else if (accountsEnabled) { register_module("paneSync", gSyncPane) }` (619-625), `/* import-globals-from sync.js */` (9) | Edit: drop the three group ids, the legacy `paneSync` registration and the Weave/UIState lazies. `defaultBrowserSync` stays |
| `browser/components/preferences/main.js` | `onGetStarted` opens `about:preferences#sync` for Dev Edition (726-734) | FxA-only, left to 1f |
| `browser/components/preferences/fxaPairDevice.js` | `Weave.Utils.ensureMPUnlocked` | deleted (section 2) |
| `browser/components/preferences/findInPage.js` | `"paneSync"` default pane (544) | no change (pane still exists) |
| `browser/components/preferences/config/LegacyPaneMappings.mjs` | `{category:"sync"}` mappings | no change |

### 3d. Modules that read Sync state

| File | What | Action |
|---|---|---|
| `browser/components/AccountsGlue.sys.mjs` | `UIState` lazy (17), `sync-ui-state:update` case (101-123), `_updateFxaBadges` (441-462), `idle-daily` clientInfo ping (125-131) | FxA module. Option B: change the URL only. Option A: drop the three UIState blocks |
| `browser/components/asrouter/modules/MenuMessage.sys.mjs` | `UIState` lazy (16), `UIState.get()` (81), `win.gSync.toggleAccountPanel` (232) | drop the signed-in check (treat as not signed in) and the `toggleAccountPanel` call |
| `toolkit/components/messaging-system/lib/SpecialMessageActions.sys.mjs` | `UIState` lazy (79), observer for `UIState.ON_UPDATE` (607-669) | FxA sign-in flow action; Option B: URL only. Option A: drop the observer branch |
| `browser/components/uitour/UITour.sys.mjs` | `UIState` lazy (25), observer `UIState.ON_UPDATE` (233, 747-748), `sync` target in `getConfiguration` (1656-1670) and `fxa` target (1756-1770) read `services.sync.*` with defaults | Option A: drop the observer and the `sync` config case; the prefs have defaults, reads stay valid |
| `browser/components/backup/BackupService.sys.mjs` | `UIState` lazy (92), `fxaState` in the backup state (3155-3157) | Option A: report signed-out |
| `browser/components/aboutlogins/AboutLoginsParent.sys.mjs` | `UIState` lazy (22), `getSyncState()` (836-850), `UIState.ON_UPDATE` case (566), `PASSWORD_SYNC_ENABLED` getter + `onPasswordSyncEnabledPreferenceChange` (894-899), `gSync.openFxAEmailFirstPage` (224) | Edit: `getSyncState()` returns a constant signed-out state, drop the observer, the pref getter and the gSync call. Check the about:logins content script for use of the SyncState message |
| `toolkit/components/satchel/megalist/aggregator/datasources/LoginDataSource.sys.mjs` | `UIState` lazy (20), `PASSWORD_SYNC_ENABLED` getter (31-35), `#isPasswordSyncEnabled()` (512-515) | Edit: return false, remove the getter. The `...-remove-all-message-sync` string becomes unused |
| `browser/base/content/browser-trustPanel.js` | `UIState` lazy (27), `#hasMonitorAccount()` (1132-1135) | Edit: return false (this is the FxA Monitor check; 1f can remove the rest) |
| `browser/extensions/newtab/lib/Widgets/PrivacyFeed.sys.mjs` | `UIState` lazy (14), `signedIn` (229) | Edit: signedIn = false |
| `toolkit/components/telemetry/app/TelemetryEnvironment.sys.mjs` | `_updateServicesInfo()` gets `@mozilla.org/weave/service;1` (1479-1481) and sets `Glean.fxa.syncEnabled` | Edit: `syncEnabled = false` constant or drop the Weave lookup and the `Glean.fxa.syncEnabled` line; the `fxa` metric group is in `services/fxaccounts/metrics.yaml` (1f) |
| `browser/components/profiles` (`browser-profiles.js` in `browser/base/content`) | 15 calls of `gSync.emitFxaToolbarTelemetry(...)` (423-553) | Edit: drop the calls. The `fxa_avatar_menu` profile metrics live in the deleted `services/sync/modules/metrics.yaml`, so the calls would throw |
| `toolkit/components/formautofill/default/FormAutofillPrompter.sys.mjs` | credit card doorhanger sync checkbox reads `services.sync.engine.creditcards` without a default (920, 1481-1495) | Edit: drop the `checkbox` block. A removed pref read without a default throws |
| `browser/extensions/formautofill/api.js` | sets/clears `services.sync.engine.addresses.available` and `creditcards.available` (95-132) | drop |
| `toolkit/components/formautofill/RustAutofillAddressStorage.sys.mjs`, `RustAutofillCreditCardStorage.sys.mjs` | `addressesBridgedEngine()` / `creditCardsBridgedEngine()` accessors (282-285, 720-723) | drop the two methods. The Rust `Store` still exposes them (vendored) |
| `toolkit/modules/BrowserUtils.sys.mjs` | `INVALID_SHAREABLE_SCHEMES` pref getter named `services.sync.engine.tabs.filteredSchemes` (174), Relay `extraCheck` list with `identity.sync.tokenserver.uri` (1053) | Replace: use the literal default `"about\|resource\|chrome\|file\|blob\|moz-extension\|data"` and a new pref name (or no pref). Without it, `getShareableURL` treats every URL as shareable. Remove the tokenserver entry from `extraCheck` |
| `toolkit/modules/tests/xpcshell/test_BrowserUtils.js` | sets `services.sync.engine.tabs.filteredSchemes` (182-184) | edit to the new name |
| `toolkit/components/extensions/parent/ext-storage.js` | Kinto branch in `extensionStorageSync` getter (18-25), `dummyContextForKinto` and `closeCallback` (222-249) | Edit: use only `ExtensionStorageSync.sys.mjs` |
| `toolkit/components/extensions/Extension.sys.mjs` | `storageSyncOldKintoBackend` getter on `webextensions.storage.sync.kinto` (108-111, used at 598) | Edit: treat as false; drop the getter and the pref |
| `toolkit/components/passwordmgr/LoginStorageMigrator.sys.mjs` | comment only (14) | none |
| `toolkit/components/utils/ClientEnvironment.sys.mjs` | `usesFirefoxSync` and device counts read `services.sync.*` with defaults (153-164) | Leave: defaults make reads safe. Optional: return constants |
| `browser/components/asrouter/modules/ASRouterTargeting.sys.mjs`, `ASRouterPreferences.sys.mjs`, `OnboardingMessageProvider.sys.mjs` | read `services.sync.username`, `.clients.devices.*`, `.numClients` with defaults | Leave (defaults make reads safe); left to the asrouter/Nimbus pass |
| `browser/components/migration/FirefoxProfileMigrator.sys.mjs` | skip-list includes `services.sync.username` (416) | Leave |
| `toolkit/modules/Troubleshoot.sys.mjs` | pref allowlist for about:support `services.sync.*` (95-99) | drop the five entries |
| `toolkit/components/messaging-system/lib/MessagingSystemBlocklists.sys.mjs` | blocklist prefix `"services.sync."` (123) | Leave (it blocks pref-writing actions; harmless and protective) |
| `python/mozbuild/mozbuild/mozinfo.py` | `d["sync"] = substs.get("MOZ_SERVICES_SYNC") == "1"` | Leave until the configure cleanup (section 5) |
| `toolkit/profile/content/profileDowngrade.js` | `AppConstants.MOZ_SERVICES_SYNC &&` (21) | Edit: drop the condition and the sync-related downgrade UI string branch |
| `services/automation/ServicesAutomation.sys.mjs` | imports `resource://services-sync/{util,main}` (32-33) | Edit or delete the file. It is only built for nightly/debug (`services/moz.build`), nothing else uses it: recommend deleting `services/automation` and the `DIRS += ["automation"]` block |

### 3e. Prefs, tests and tooling outside the feature

See sections 5 and 7.

### 3f. References from later-feature code: services/fxaccounts (p6-1f)

FxA imports Sync modules at module level, so after `services/sync` is deleted the FxA modules stop loading (static imports) or throw when a lazy getter fires. The FxA code is locked off at run time but is still imported by `AccountsGlue`, `BrowserGlue`, `preferences.js` and the FxA tests. Minimum edits to keep it loadable until 1f deletes it:

| File | What | Action |
|---|---|---|
| `services/fxaccounts/FxAccountsWebChannel.sys.mjs` | static `import { SyncDisconnect } from "resource://services-sync/SyncDisconnect.sys.mjs"` (47, used 932), lazy `Weave` (57, used 644, 672), lazy `mpLocked` from `services-sync/util` (60-63), engine pref writes `services.sync.engine.*` (640-646, 1054) | remove the import (breaks module load otherwise); stub `SyncDisconnect.disconnect` use to a no-op or remove the "disconnect" web-channel command; drop the Sync-engine configuration branch of the `login` handler (Weave.Service.engineManager, Weave.Service.configure); replace `mpLocked` with an inline `Services.logins.primaryPasswordEnabled` check |
| `services/fxaccounts/FxAccountsCommands.sys.mjs` | lazy `BulkKeyBundle`, `CryptoWrapper`, `getRemoteCommandStore`, `RemoteCommand`, `Resource`, `Utils` from `services-sync` (25-31); `INVALID_SHAREABLE_SCHEMES` from `services.sync.engine.tabs.filteredSchemes` (35-44, rename like `BrowserUtils`); `services.sync.tabs.*` topics (53, 673-705); send-tab and close-tab crypto/queue use (331-333, 419-421, 503-625, 774-923) | Send Tab and Close Tab are Sync features built on Sync's crypto and `TabsStore`. Stub or remove the `SendTab`/`CloseRemoteTab` classes and the callers (`AccountsGlue` `fxaccounts:commands:*`, `FxAccountsDevice`), or accept that these paths throw if run (they are unreachable with FxA locked off) |
| `services/fxaccounts/FxAccounts.sys.mjs` | lazy `mpLocked` and `ensureMPUnlocked` from `services-sync/util` (53-61); `observerPreloads` entry importing `Weave` (842-848) | inline primary-password helpers; drop the `Weave` preload |
| `services/fxaccounts/FxAccountsDevice.sys.mjs` | static `import { DEVICE_TYPE_DESKTOP } from "resource://services-sync/constants.sys.mjs"` (19, used 160) | replace with the literal `"desktop"`; breaks module load otherwise |
| `services/fxaccounts/FxAccountsPairing.sys.mjs` | lazy `Weave` (29, used 185) | drop; pairing is Sync's device-pairing flow |
| `services/fxaccounts/FxAccountsTelemetry.sys.mjs` | `importESModule("resource://services-sync/telemetry.sys.mjs")` in `recordEvent` (43), `services.sync.username` read (188) | drop the import and the call (the events were consumed by the Sync ping) |
| `services/fxaccounts/FxAccountsCommon.sys.mjs` | `prefRoot: "services.sync."` (34) | leave (a pref root string; only reads with defaults) |
| FxA tests: `test_commands_closetab.js` (imports `TabsStore`), `test_device.js`, `test_web_channel.js` (mocks `Weave`), `test_accounts_config.js`, `browser/test_open_uri.js` / `head.js` (use `AccountsGlue`), `test_sync_warning_dialogs.js` (`Glean.syncMergeDialog`) | | delete or skip; 1f deletes the whole directory |

Recommendation: because of this coupling, integrate 1e and 1f together (one build, one test pass), or delete the whole of `services/fxaccounts` in the 1e commit. Splitting forces about seven throw-away edits in modules that are deleted one step later.

## 4. References from earlier features (ignore)

None needed. The VPN branch (`toolkit/components/ipprotection`, `fxa/IPPSignInWatcher.sys.mjs`, `IPPAndroidSignInWatcher.sys.mjs`, `tests/xpcshell/test_IPPSignInWatcher.js`) matches `UIState`/`services.sync.*` in the base tree but is gone before this branch. Translations, GenAI and ML code (`browser/components/genai`, `aiwindow`, `translations`) show hits for `sidebar.main.tools` strings with `syncedtabs` in test files (`browser_aiwindow...`, `browser_sidebar_aiwindow.js`): those are earlier-feature files.

References from later features (not handled here, they stay working until their own deletion):
- p6-1f, services/fxaccounts: it is a LATER feature and it statically imports Sync modules, so those references must be handled in this branch (section 3f). Do not treat them as ignorable.
- p6-1g updater: `nsUpdateSyncManager` and `nsIUpdateSyncManager` matches are unrelated (a mutex between update processes).
- p6-1h crash reporter, p6-1i Nimbus: no references (`UIState` matches in `toolkit/crashreporter/client` are `ReportCrashUIState`, unrelated).

## 5. Prefs, metrics and strings to remove

### Prefs

- `modules/libpref/init/all.js` lines 3747-3848: the whole `#ifdef MOZ_SERVICES_SYNC` block (`services.sync.*`: scheduler, engine toggles, log appenders, telemetry, validation, extension-storage). Also lines 3555-3557: `webextensions.storage.sync.kinto`, `webextensions.storage.sync.serverURL`. Keep `webextensions.storage.sync.enabled` if present elsewhere.
- `modules/libpref/moz.build` lines 157-158: `DEFINES["MOZ_SERVICES_SYNC"]`.
- `modules/libpref/docs/index.md` lines ~14 and 285-297: the paragraph describing `services.sync.prefs.sync.*`. Reword or drop.
- `browser/app/profile/firefox.js`:
  - 1616-1769: the `services.sync.prefs.sync.*` (113 prefs), `services.sync.prefs.sync-seen.*`, `services.sync.prefs.dangerously_allow_arbitrary`, `services.sync.syncedTabs.showRemoteIcons`, `services.sync.syncedTabs.syncDelayAfterTabChange`.
  - 2397-2398 `identity.sync.tokenserver.uri`; 2407-2408 `identity.sendtabpromo.url`, `identity.sendtab.deviceissues.url`.
  - 2891-2892 `services.sync.engine.creditcards.available`.
  - 428 `browser.urlbar.suggest.remotetab`.
  - Leave for 1f: `identity.fxaccounts.*`, `identity.mobilepromo.*` (used by FxA CTA and onboarding), `browser.profiles.sync.allow-danger-merge`, `nimbus.profilesdatastoreservice.sync.enabled`.
- `browser/app/profile/m6e.js` lines 343-345: `services.sync.log.appender.file.logOnSuccess`, `services.sync.engine.bookmarks.validation.enabled`, `services.sync.engine.passwords.validation.enabled`. Update the comment at line 18 ("Mozilla accounts and Sync are removed") when 1f lands.
- `testing/mochitest/ignorePrefs.json` lines 62-65 (`services.sync.clients.lastSync`, `declinedEngines`, `globalScore`, `nextSync`), `testing/profiles/perf/user.js` line 110.
- `modules/libpref/init/StaticPrefList.yaml`: no sync prefs, no edit.
- `tools/m6e/merge/new-nightly-prefs.py` lines 92 and 119: `MOZ_SERVICES_SYNC` in the Nightly-only define table; drop with the configure cleanup.

### Glean and telemetry

Deleted with `services/sync/modules/metrics.yaml` (categories): `synced_tabs`, `fxa_avatar_menu`, `fxa_app_menu`, `tab_context_menu`, `page_context_menu`, `send_tab_toolbar`, `firefoxview` (the 4 send-tab/sync_device_id metrics defined there only), `sync_merge_dialog`, `sync`, `syncs`. And the `sync` ping (`services/sync/pings.yaml`). Consumers outside the feature, all handled in section 3: `browser-profiles.js` (fxa_avatar_menu profile metrics), `TelemetryEnvironment` (`Glean.fxa.syncEnabled` lives in the fxaccounts yaml, not here), `FxAccountsWebChannel` (`Glean.syncMergeDialog`, 1f; it uses `?.` so it survives).

Legacy Telemetry definitions that become dead (optional, they regenerate C++ enums, so batch with other telemetry changes):
- `toolkit/components/telemetry/Events.yaml`: categories `fxa_avatar_menu` (614), `fxa_app_menu` (656), `synced_tabs` (1132); `fxa` (595) is FxA.
- `toolkit/components/telemetry/Histograms.json`: `WEAVE_DEVICE_COUNT_DESKTOP` (2190), `WEAVE_DEVICE_COUNT_MOBILE` (2201). Lines 1146-1187 are store names, not Sync.

### Strings

Whole-file FTL to delete: `browser/locales/en-US/browser/sync.ftl` (156 lines, 31 `fxa-*` messages used only by gSync; `FxAccountsWebChannel` references the file by name in 1f code and must be checked), `syncedTabs.ftl`, `preferences/fxaPairDevice.ftl`, `toolkit/locales/en-US/services/aboutSyncLog.ftl` (`toolkit/locales/jar.mn` takes `services/**/*.ftl` by glob, no edit).

Message-id prefixes left behind in shared FTL files after the code is gone (remove by prefix in a single pass; message ids are not referenced elsewhere once the DOM is removed):
- `browser/appmenu.ftl`: `appmenu-remote-tabs-*`, `appmenuitem-fxa-toolbar-*`, `fxa-toolbar-sync-*`, `appmenu-fxa-sync-and-save-data2`, `appmenu-secure-sync-header`, `fxa-menu-*sync*`.
- `browser/browser.ftl`: `toolbar-button-synced-tabs`, `toolbar-button-send-tab`, `menu-view-synced-tabs-sidebar`-style ids.
- `browser/browserContext.ftl`, `tabContextMenu.ftl`, `menubar.ftl`: send-to-device and `menu-tools-*sync*` ids.
- `browser/preferences/preferences.ftl`: `sync-*`, `pane-sync-title3`, `account-sync-*`, `sync-engine-*`, `sync-device-name-*` (75 candidate lines; `pane-account-sync-title2` and `account-sync-section` stay because the pane stays).
- `browser/firefoxView.ftl`, `fxviewTabList.ftl`, `sidebar.ftl`, `sidebarMenu.ftl`, `sharePanel.ftl`, `featureCallout.ftl`, `contextual-manager.ftl` (`...remove-all-message-sync`), `aboutLogins.ftl`, `backupSettings.ftl`, `confirmationHints.ftl`, `newtab/asrouter.ftl`, `newtab/onboarding.ftl`.
- `toolkit/about/aboutSupport.ftl` sync lines, `toolkit/global/profileDowngrade.ftl` sync strings, `toolkit/formautofill/formAutofill.ftl` credit-card sync checkbox string.
- `tools/lint/fluent-lint/exclusions.yml` line 156 (`sync-engine-addons`), `python/l10n/fluent_migrations/bug_1971841_sync_settings_redesign.py` and `bug_2032640_send_tab_account_menu.py` (historical migration scripts, leave).

The FTL cleanup is a long tail with no runtime effect. Recommended: delete the four whole files in 1e, and do the prefix pass in P6.2 with the lint-config cleanup.

## 6. Build-system edits

Flag key: FE = front-end (`./mach build faster`), CPP = C++ or IDL (`./mach build`), RUST = Rust, CFG = configure-time.

| Edit | Type |
|---|---|
| `services/moz.build`, `browser/components/moz.build`, `browser/base/*`, jar.mn files, `BrowserComponents.manifest`, `toolkit/content/jar.mn`, `toolkit/components/*/moz.build` for formautofill and extensions, places `components.conf` JS parts | FE (a moz.build change regenerates the backend once) |
| `modules/libpref/init/all.js`, `modules/libpref/moz.build` `DEFINES` removal | FE for the pref file; libpref compile flags change, so libpref and dependants recompile once |
| `docshell/base/nsAboutRedirector.cpp`, `docshell/build/components.conf` (about:sync-log) | CPP |
| `dom/security/nsContentSecurityUtils.cpp` (allowlist URLs) | CPP |
| `toolkit/components/places/mozISyncedBookmarksMirror.idl`, `SyncedBookmarksMirror.h`, `places/components.conf` C++ class entry, `places/moz.build` EXPORTS/XPIDL | CPP (XPIDL header regeneration; places users of the headers recompile) |
| R1: `toolkit/components/places/bookmark_sync` removal, `gkrust-features.mozbuild`, `toolkit/library/rust/shared/{Cargo.toml,lib.rs}`, `Cargo.lock` | RUST (libgkrust relink; `dogear` stays in the lock because of the vendored `places` crate) |
| R2 (optional, can be deferred to P6.3): drop the `tabs` crate from the JS bindings crate: `toolkit/components/uniffi-bindgen-gecko-js/components/Cargo.toml` (`tabs = "0.1"`), `components/lib.rs` (`tabs::uniffi_reexport_scaffolding!()`), `config.toml` (`[tabs.async_wrappers]`), `components/moz.build` (`generated/RustTabs.sys.mjs`), delete `components/generated/RustTabs.sys.mjs`, regenerate `toolkit/components/uniffi-js/GeneratedScaffolding.cpp` and the other `generated/` files (`./mach uniffi generate`, `mach_commands.py` has the crate map), `Cargo.lock` (tabs line under gkrust-uniffi-components, workspace root `Cargo.toml` `tabs = { path = ... }` override at 407). `third_party/application-services/components/tabs` and `sync_manager` directories stay (D21). `sync15`, `webext-storage`, `logins`, `autofill` stay | RUST + CPP (large regenerated C++ file) |
| `toolkit/modules/AppConstants.sys.mjs` (`MOZ_SERVICES_SYNC`), `toolkit/modules/moz.build:324`, `tools/@types/subs/AppConstants.sys.d.mts:31`, `toolkit/moz.configure:1194` (`project_flag("MOZ_SERVICES_SYNC")`), `browser/moz.configure:7` (`imply_option("MOZ_SERVICES_SYNC", True)`), `python/mozbuild/mozbuild/mozinfo.py:87`, `tools/m6e/merge/new-nightly-prefs.py` | CFG. Not required for the branch to build. Recommend leaving the flag defined during 1e (nothing reads it after `profileDowngrade.js` is edited) and removing it in one configure batch with the other flags (updater, crashreporter, Nimbus) so that the 40 minute to 2 hour reconfigure cost is paid once |
| Lint configs for deleted paths: `eslint-file-globals.config.mjs:107`, `eslint-rollouts.config.mjs` (59, 70, 176, 203, 356, 665, 1841), `eslint-subdirs.config.mjs:113`, `eslint-test-paths.config.mjs:312`, `stylelint-rollouts.config.js` (58, 193), `tools/lint/extra-js-modules/allowlist.txt` (58-60, 230-262), `.prettierignore:1709` is Android (leave) | FE-neutral, lint only |
| `browser/installer/package-manifest.in`, `allowed-dupes.mn`, `confvars.sh`: no sync entries | none |

## 7. Tests to delete or edit

Delete (whole): everything listed in section 2 under tests, plus `services/sync/tests` and `toolkit/components/places/tests/sync` (23 files; keep a trimmed `PlacesSyncUtils` test, see section 8).

Edit (staying tests that reference removed things; edit the Sync part, delete the test if it only covers Sync):
- Sidebar: `browser_glean_sidebar.js` (syncedtabs toggle cases at 166-191 and `TabsSetupFlowManager` stubs), `browser_tools_overflow.js`, `browser_tools_migration.js`, `browser_extensions_sidebar.js`, `browser_hide_sidebar.js`, `browser_a11y_sidebar.js`, `browser_sidebar_prefs.js`, `browser_toolbar_sidebar_button.js`, `browser_sidebar_focus_ring.js`, `browser_launcher_hidden_restore.js`, `browser_resize_sidebar.js`, `browser_vertical_tabs.js`, `browser_sidebar_menubar_item_commands.js`, `browser_launcher_splitter_drag_release.js`, `browser_sidebar_collapsed_close_tab_button.js`, `browser_sidebar_pinned_tab_promo.js`, `browser.toml` (the two sync test entries), `SidebarTestUtils.sys.mjs`; also `toolkit/components/satchel/megalist/content/tests/browser/browser_passwords_sidebar.js` (tool list string) and `browser/components/miniwindow/test/browser/browser_sidebar.js`.
- Urlbar: `browser-engagementTelemetry/head.js`, `browser-UrlbarView/browser_resultTypes_display.js`, `browser-searchMode/browser_excludeResults.js`, `browser-tips/head.js`, `browser-trustPanel/browser_trust_panel.js` (UIState).
- Customize UI: `browser/components/customizableui/test/browser.toml` and any test using `sync-button`.
- Browser base: `browser-static/browser_all_files_referenced.js` (list of referenced files, 2 entries), `browser-performance/browser_startup.js` (module allowlist; `PlacesSyncUtils` entry stays), `browser-siteIdentity/browser_check_identity_state.js` (`about:sync-log`, line 119), `browser-keyboard/head.js`, `browser_toolbarKeyNav.js`, `browser_toolbarButtonKeyPress.js` (account button handling).
- Preferences: `tests/head-common.js` (3 refs), `tests/privacy/browser_privacy_syncDataClearing_v2.js` (check, it may test the sync cleanup pane), `tests/moz.build`/`browser.toml` lists.
- Places: `browser/components/places/tests/browser/browser_mobile_root_on_bookmark_menus.js` imports `PlacesSyncUtils` (module stays, no edit).
- Asrouter/messaging: `browser_trigger_listeners.js`, `browser_asrouter_targeting.js`, `browser_asrouter_menu_messages.js`, `tests/unit/ASRouter.test.js`, `ASRouterPreferences.test.js`, `toolkit/components/messaging-system/lib/test/unit/test_MessagingSystemBlocklists.js` (keep, the `services.sync.` blocklist stays).
- FxA-adjacent browser tests that import Sync modules and belong to 1f deletion: `uitour/test/browser_fxa_config.js`, `browser_fxa.js`, `aboutwelcome/tests/browser/browser_aboutwelcome_fxa_signin_flow.js`, `aboutlogins/tests/browser/browser_fxAccounts.js` and `head.js`, `tests/browser/browser_browserGlue_client_association_ping.js`, `browser_browserGlue_fxa_client_info_ping.js`, `backup/tests/xpcshell/test_BackupService.js`. In 1e, either delete them with the matching gSync/UIState code or let them fail until 1f; recommended: delete in 1e because they cannot run without `UIState`.
- Other: `toolkit/components/utils/test/unit/test_ClientEnvironment.js` (keep), `toolkit/components/passwordmgr/test/unit/test_LoginStorageMigrator.js` (`services.sync.username` pref, keep), `toolkit/components/extensions/test/xpcshell/test_ext_storage_sync.js`, `test_ext_storage_content_sync.js`, `test_ext_api_events_listener_calls_exceptions.js` (set `webextensions.storage.sync.kinto`; edit the pref line), `test/mochitest/test_ext_storage_cleanup.html` (reads the Kinto pref).
- Perf suite expectations: `testing/talos` pages are bundled JS, ignore.

Suites to run for integration of 1e: `browser/components/sidebar`, `browser/components/customizableui`, `browser/components/urlbar` (unit and browser-tabs), `browser/components/preferences`, `browser/components/places`, `toolkit/components/places` (`tests/unit`, `tests/bookmarks`), `toolkit/components/passwordmgr`, `browser/extensions/formautofill`, `toolkit/components/extensions` (storage tests), `browser/base/content/test/static` and `general`, plus `tools/m6e/acceptance.py` (its `REMOVED_ELEMENTS` list for "Mozilla accounts and Sync" accepts missing elements; confirm it does, then it needs no edit).

## 8. Must stay (with proof)

| Item | Why it cannot go | Unreachability proof |
|---|---|---|
| `toolkit/components/places/PlacesSyncUtils.sys.mjs`, `moz_bookmarks.syncStatus/syncChangeCounter`, `moz_bookmarks_deleted`, `moz_places.sync*` in `Database.cpp`/`Bookmarks.sys.mjs` | `Bookmarks.sys.mjs` calls `PlacesSyncUtils.bookmarks.determineSyncChangeDelta`, `addSyncChangesForBookmarksWithURL`, `determineInitialSyncStatus`, `resetSyncMetadata` in ~20 places for every bookmark mutation. Removing needs a Places schema migration (C++) | Pure local DB bookkeeping. No network code remains once `services/sync` and the mirror are gone: `git grep "services-sync" toolkit/components/places` must return nothing (check at integration) |
| Login sync metadata (`syncCounter`, `everSynced` in `LoginInfo.sys.mjs`, `storage-json.sys.mjs`, `storage-rust.sys.mjs`; `nsILoginInfo.idl`; `getSyncID`/`setSyncID`/`getLastSync`/`setLastSync`/`ensureCurrentSyncID` in `nsILoginManager.idl` and `LoginManager.sys.mjs`) | Persisted fields in `logins.json` and the Rust `logins` DB; removing changes an XPIDL interface and storage migration | No caller after `services/sync/modules/engines/passwords.sys.mjs` is deleted: `git grep -E "getSyncID|ensureCurrentSyncID" -- browser toolkit` must show only the IDL, `LoginManager.sys.mjs`, storage backends and passwordmgr tests |
| Rust crates `sync15`, `logins`, `autofill`, `webext-storage` and their `*BridgedEngine` UniFFI exports (`RustSync15.sys.mjs`, `RustLogins.sys.mjs`, `RustAutofill.sys.mjs`, `RustWebextstorage.sys.mjs`, `GeneratedScaffolding.cpp`) | Vendored under `third_party/application-services` (D21: leave so upstream updates apply); `logins`, `autofill`, `webext-storage` are the live local stores | The bridged-engine objects are only returned from `LoginStore.bridged_engine()` etc.; the only JS callers were in the deleted Sync engines and `FormAutofillSync`. Network I/O in sync15 needs a `Sync15StorageClient` driven by Weave; no JS creates one after the deletion (`git grep -l "BridgedEngine" -- browser toolkit services` returns only generated bindings). UniFFI scaffolding is reachable only from privileged JS through `moz-src:///` module imports, not from web content or content-process IPC |
| `storage.sync` WebExtension API, `ExtensionStorageSync.sys.mjs`, `ExtensionStorageComponents.sys.mjs`, `webext-storage` | Extensions need `storage.sync` as a local area; it works without Sync | Local Rust store; no uploader exists after deletion; Kinto backend deleted |
| `services/common` (`hawk*`, `rest`, `tokenserverclient`, `async`, `observers`, `utils`, kinto-*), `services/crypto` (`WeaveCrypto`, `jwcrypto`, `utils`) | `services/fxaccounts` (1f) and `services/settings` (Remote Settings, kinto clients) import them. `WeaveCrypto` pref branch reads `services.sync.log.` | Libraries with no entry points. Re-examine after 1f for deletion of the FxA-only modules; `async.sys.mjs` has a comment mentioning the bookmark mirror (edit the comment) |
| `viaduct-necko`, `application-services` init | Remote Settings and other Rust components still use the HTTP backend | Not Sync specific |
| `webcompat`/`BrowserUtils.getShareableURL` | Used by the macOS Share menu | Pref renamed (section 3d) |
| `identity.fxaccounts.enabled=false` (locked) in `m6e.js` | Keeps FxA UI and `gSync`-free FxA code inert until 1f | Locked pref, no FxA sign-in entry point; remains as the unreachability proof for FxA until 1f deletes the code |
| Places mobile root (`mobile____`) folder and `PlacesSyncUtils.bookmarks.ensureMobileQuery` | Used by bookmarks menus tests and `PlacesUtils` | Local only |

Proof checks to add to P6.2: after integration, `git grep -n -E "services-sync|services\.sync\.|Weave\b|@mozilla.org/weave" -- browser toolkit services dom docshell modules` should return only the allowed leftovers (read-only ASRouter/ClientEnvironment pref reads with defaults, the `services.sync.` blocklist prefix), and `obj-m6e/dist/M6E.app` should not contain `modules/services-sync` or `chrome/toolkit/content/global/aboutSyncLog.html` (`unzip -l omni.ja`). No listening sockets exist in Sync (it is an HTTP client only).

## 9. Risks and open questions

1. **Boundary with p6-1f (needs a decision before the branches are prepared).** `gSync` and `UIState` mix Sync and FxA. Two workable options:
   - Option A (this report's default): 1e deletes `browser-sync.js`, deletes Sync/Send Tab DOM, and edits every `UIState` consumer to treat the user as signed out. 1f then removes the remaining FxA code. More edits in 1e, no new module.
   - Option B: 1e moves a stripped `UIState.sys.mjs` (no Weave imports, no sync events, `hasSyncKeys` from FxA scopes) into `services/fxaccounts` and only changes the import URL in the 12 consumers; 1f deletes it together with the consumers. Fewer logic edits in 1e, but `UIState` consumers keep running against FxA-locked state until 1f.
   Option B has the lower risk of behavioural error in files that are all FxA anyway. Section 3f adds a second reason: `services/fxaccounts` statically imports `SyncDisconnect` and `constants` from Sync, so it stops loading unless it is edited or deleted. Merging 1e and 1f into one integration is cleaner than either option and is my recommendation.
2. **Merge conflicts.** `browser-sync.js`, `appmenu-viewcache.inc.xhtml`, `navigator-toolbox.js`, `account-sync.mjs`, `AccountsGlue.sys.mjs` and `browser-profiles.js` will be touched by both 1e and 1f. Integrate 1e first (as ordered) and have 1f rebase.
3. **Profiles feature coupling.** `browser-profiles.js` (15 calls) and `browser_fxa_menu_profiles_telemetry.js` depend on `gSync.emitFxaToolbarTelemetry` and on `Glean.fxaAvatarMenu`. The profile views in `appmenu-viewcache.inc.xhtml` (`PanelUI-fxa-menu-all-profiles`, `create-profile`) live inside the FxA panel and are already hidden. Confirm that M6E does not reach the profiles UI through the avatar panel.
4. **Settings redesign pane.** `account-sync.mjs` and `preferences.js` must keep the `sync` pane for Default Browser, Import, Profiles, Backup and Referrals. After the edit the pane's label key (`pane-account-sync-title2`) still says account/sync; a rename in `preferences.ftl` is cosmetic and may be done in 1f.
5. **Pref reads without defaults.** Any staying `getBoolPref("services.sync.*")` without a default throws once `all.js` loses the pref. Found and covered: `FormAutofillPrompter.sys.mjs`, `BrowserUtils.sys.mjs` (silent behaviour change instead of a throw). The checks above used targeted greps; re-run `git grep -n "services\.sync\." -- '*.js' '*.mjs' '*.sys.mjs'` on the prepared branch.
6. **Rust and generated files.** R1 touches `Cargo.lock` by hand; verify `cargo metadata --locked --offline` (or the build's own lockfile check) passes. R2 needs `./mach uniffi generate` and rewrites a very large `GeneratedScaffolding.cpp`; recommend R2 is a separate optional commit or deferred to P6.3's clean build. `mach vendor rust` must not be run: `dogear` stays.
7. **`PlacesSyncUtils` test.** `toolkit/components/places/tests/sync/test_sync_utils.js` tests the module that stays; deleting it removes coverage. Move it to `tests/unit` with `head_sync.js` helpers it needs.
8. **`ExtensionStorageSyncKinto` removal** changes `Extension.sys.mjs` and `ext-storage.js` (extension APIs). Run the `storage.sync` xpcshell tests (`test_ext_storage_sync.js`, `test_ext_storage_content_sync.js`) after the change. Fallback if it proves fragile: leave the file, lock `webextensions.storage.sync.kinto` to false, and remove only its `resource://services-sync` imports.
9. **C++ allowlists.** `nsContentSecurityUtils.cpp` has several lists with Sync chrome URLs; a stale entry is harmless but a missing entry for a still-present URL would break a page. Only remove the five URLs named in section 3a.
10. **Telemetry yaml/json.** Removing `Events.yaml` and `Histograms.json` entries regenerates Telemetry C++ headers and rebuilds a wide set of files. Deferred to the batched pass.
11. **Not checked in depth**: `devtools/` (only `UIState` name collisions found, no Sync dependency), `remote/` expectations (no Sync hits in `git grep` of `remote/`), `services/settings` allowlist (no Sync collections found), enterprise policies (`DisableFirefoxAccounts` only mentions "Sync" in schema descriptions; no handler touches `services.sync.*`), `toolkit/components/glean/docs`, telemetry `docs/`, Android and GeckoView branches of `storage-geckoview.sys.mjs` (dead on macOS; they still carry the sync metadata fields).
12. **Search cost.** Whole-tree `git grep` takes 2 minutes on this machine while the baseline build runs. The hit lists used here are saved at `/tmp/p6sync/g1.txt` (browser, devtools, root), `g3.txt` (toolkit, tools, testing, services); they will not survive a reboot.

## Critic addendum

Independent review at base cb002d4874, read-only. Searches used: basenames of every file to delete, directory names, ping and Glean category names, `send_in_pings` values across all `metrics.yaml`, manifest `head=` and `support-files` entries, `skip-if` mozinfo keys (`sync`), `moz-src:///` imports, tools/m6e scripts, asset basenames, element ids and CSS classes, pref names from the deleted JS files, C++ `PrefHasUserValue`, docs. The analyst's main structure holds. The items below are missed or wrong. Severity first.

### A. Build breakers the report missed

| File | Reference | Action |
|---|---|---|
| `xpcom/metrics.yaml` (lines ~1303, 1325, 1346, 1368, 1390, 1412, 1436, 1460) | `system.os.{name,version,locale,distro,distro_version,libstdcxx_version,windows_build_number,windows_ubr}` list `sync` in `send_in_pings` | Remove `- sync` from each. The `sync` ping is defined only in `services/sync/pings.yaml`; once that file and its `metrics_index.py` line go, glean_parser fails on an unknown ping. The metrics stay (they also go to `metrics`, `update`, `third-party-modules`). Staying code, 1e must do it. |
| `toolkit/components/passwordmgr/metrics.yaml` (1152, 1194, 1220) | `pwmgr.login_page_safety`, `pwmgr.prompt_remember_action`, `pwmgr.prompt_update_action` list `sync` in `send_in_pings` | Remove `- sync`. Same reason. |
| `services/fxaccounts/metrics.yaml` (138, 177, 218, 250) | `fxa.sendtab_sent`, `sendtab_received`, `closetab_sent`, `closetab_received` list `sync` in `send_in_pings` | Later feature (1f) but it is still present when 1e merges, so 1e must edit it (or delete the four Send/Close Tab metrics). Section 5 said only that the `fxa` group lives there. |
| `browser/components/firefoxview/tests/browser/FirefoxViewTestUtils.sys.mjs` (151-155) | `withFirefoxView` imports `firefox-view-tabs-setup-manager.sys.mjs` and calls `TabsSetupFlowManager.resetInternalState()` (default `resetFlowManager = true`) | Delete the `resetFlowManager` option and the import. `withFirefoxView` is used by `FirefoxViewTestUtils` itself and by `customizableui/test/browser_1856572_ensure_Fluent_works_in_customizeMode.js`, so deleting `firefox-view-tabs-setup-manager.sys.mjs` breaks any test that opens Firefox View through it. |
| `tools/m6e/branding/fox-art.txt` (lines 48-49, 103-105) | Lists `browser/components/firefoxview/content/synced-tabs-empty.svg`, `synced-tabs-error.svg`, `browser/themes/shared/sidebar/kit-qr-tabs-devices-empty.svg`, `kit-tabs-devices-error.svg`, `kit-tabs-devices.svg` | Drop the five lines. `replace-fox-art.sh` and the merge check `check_fox_art` ("listed in fox-art.txt but gone; replace-fox-art.sh stops on it") fail on missing files. |
| `browser/themes/shared/sidebar/kit-tabs-devices.svg`, `kit-tabs-devices-error.svg`, `kit-qr-tabs-devices-empty.svg` | Only users are `SyncedTabsController.sys.mjs` and `firefox-view-synced-tabs-error-handler.sys.mjs` | Delete the three files and `browser/themes/shared/jar.inc.mn` lines 312-314. Missing from section 2. |
| `browser/locales/filter.py` (line 20) | `"services/sync"` in the l10n module allowlist | Drop the entry. |
| `tools/lint/clippy.yml` (154) | `toolkit/components/places/bookmark_sync/` exclude | Drop when the crate goes. |
| `tools/lint/extra-js-modules/allowlist.txt` | Line 357 `ExtensionStorageSyncKinto.sys.mjs` and line 459 `SyncedBookmarksMirror.sys.mjs` (the report listed 58-60 and 230-262 only) | Drop both. |
| `browser/components/sharing/tests/browser/browser.toml` (41) | `support-files = ["!/browser/base/content/test/browser-sync/head.js"]` under `browser_testSharePanelSendToDevice.js` | Drop with the test entry (section 2 deletes the test but not the manifest entry). |
| `toolkit/components/extensions/test/xpcshell/` manifests | `head_sync.js` is in the `head =` list of `xpcshell.toml`, `xpcshell-remote.toml` and `xpcshell-serviceworker.toml`; the `test_StorageSyncService.js` entry (`skip-if "!sync"`) is in `xpcshell.toml` line 54, not in `xpcshell-common.toml` as the report says | `head_sync.js` is the Kinto test context helper (`KintoExtContext`, `withContext`); its only user is `test_ext_storage_sync_kinto.js`. Delete it and edit all three head lists. Remove the `xpcshell.toml` entry for `test_StorageSyncService.js`. |
| `browser/extensions/formautofill/test/unit/xpcshell.toml` (120-154) | Entries for `test_rust_autofill_address_sync_engine.js`, `..._credit_card_sync_engine.js`, `test_sync.js`, `test_sync_deprecate_*` have `head=` lines pointing into `services/sync/tests/unit/` and `prefs = ["services.sync.engine.*.available=true"]`; also the `test_rust_autofill_*_bridged_engine.js` entries | Remove the entries with the test files. Section 7 does not name this manifest. |
| `toolkit/components/telemetry/tests/unit/xpcshell.toml` (75) and `test_SyncPingIntegration.js` | Tests `TelemetryController.registerSyncPingShutdown` | Delete the test and the entry, and remove `registerSyncPingShutdown` (line ~257), `Impl._fnSyncPingShutdown` (321, 895, 994-998, 1190) from `toolkit/components/telemetry/app/TelemetryControllerParent.sys.mjs`. The only caller is `services/sync/modules/telemetry.sys.mjs`. Not in the report at all. |
| `browser/components/enterprisepolicies/tests/browser/browser_policy_disable_fxaccounts.js` | Asserts `gSync.FXA_ENABLED` | Delete or rewrite to check the locked pref. Not in section 7. |
| `browser/components/customizableui/test/head.js` (156) | `CustomizableUI.removeWidgetFromArea("sync-button")` | Drop the line (widget goes). Report names only `browser.toml`. |
| `browser/components/urlbar/tests/browser-engagementTelemetry/` | `head-groups.js` (`doRemoteTabTest`, `UrlbarProviderRemoteTabs` at 233-238), `browser_glean_telemetry_abandonment_groups.js`, `..._engagement_groups.js`, `..._engagement_selected_result.js` (remote tab cases) | Edit out the remote tab cases. Report lists only `head.js`. |
| `browser/components/urlbar/tests/browser-tabs/browser.toml` (38), `browser-telemetry/browser.toml` (27), `unit/xpcshell.toml` (230-233, `skip-if "!sync"`) | Entries for `browser_remotetab.js` and `test_remote_tabs.js` | Drop with the test files. Also `unit/head.js` `makeRemoteTabResult` becomes unused (keep or drop). |

### B. Wrong or doubtful claims in the report

1. Section 3b, `browser/modules/metrics.yaml` `preferences_pane_sync` and Scalars `preferences_paneSync`: do not drop. The `sync` pane stays (the report says so in 3c), and `browser/modules/BrowserUsageTelemetry.sys.mjs` line 165 still lists `"paneSync"`, so clicks in that pane record `browser.ui.interaction.preferences_paneSync`. Dropping the metric makes the recording a no-op or a lint error in tests (`browser_UsageTelemetry_interaction.js`).
2. Section 5 strings: `pane-sync-title3` must stay. `preferences.xhtml:130` (`category-sync` nav button, which the report keeps) uses it, and `browser/components/aiwindow/models/scripts/generate_prefs_nav_map.py` (earlier feature, ignore) names it. `preferences.js:619` overrides it with `pane-account-sync-title2` only in redesign mode.
3. Section 5: "31 `fxa-*` messages in `sync.ftl` used only by gSync" is wrong. `browser/base/content/appmenu-viewcache.inc.xhtml` (FxA panel DOM, left for 1f) uses `fxa-menu-signed-out-sign-in-button`, `fxa-menu-manage-account-subtitle`, `fxa-menu-turn-on-sync`, `fxa-menu-sign-in-promo-*`, `fxa-menu-sign-out`, `fxa-menu-send-to-mobile-*`, `fxviewtabrow-send-to-mobile-*`, and `FxAccountsWebChannel.sys.mjs` loads `browser/sync.ftl` and uses `sync-setup-verify-*`, `sync-account-in-use-*`, `sync-button-*`, `sync-profile-different-account-*`. Deleting `sync.ftl` is runtime-neutral only because FxA is locked off and `sync.ftl` is not linked from `browser.xhtml`. It leaves dangling ids until 1f. Either delete `sync.ftl` in the merged 1e+1f commit or move it.
4. Section 5: "`toolkit/about/aboutSupport.ftl` sync lines" must not be touched blindly. The `support-remote-settings-sync-*` strings (aboutSupport.xhtml 579-590) are Remote Settings, not Sync. Only prefix `services.sync` entries in `Troubleshoot.sys.mjs` are Sync.
5. Section 7: `browser/components/urlbar/tests/browser-tips/head.js` is not a Sync file. Its "sync manager" is `nsIUpdateSyncManager` (updater, 1g). Remove it from the edit list.
6. Section 3a, `nsContentSecurityUtils.cpp`: the allowlist has seven Sync/FxA entries, not six. `fxaPairDevice.xhtml` appears at lines 1266 and 1352; `sidebar-syncedtabs.html` 1316 and 1358; `syncedtabs/sidebar.html` 1360 and 1406; `syncChooseWhatToSync.xhtml` 1264. Section 9 item 9 says "remove only the five". Line 1830-1833 `about:sync` (mozilla-signed developer add-on, no code in tree) is also stale; leave or drop.
7. Section 8, `services/common` "FxA and Remote Settings consumers": `tokenserverclient.sys.mjs` has no consumer outside `services/sync` (checked across browser, toolkit, services, dom, devtools, remote). After 1e it, `services/common/tests/unit/test_tokenserverclient.js`, the `services.common.log.logger.tokenserverclient` pref (all.js 3745) and `identity.sync.tokenserver.uri` are dead. Delete in 1e or leave on purpose. `services/crypto/modules/WeaveCrypto.sys.mjs` and its two tests (`test_crypto_crypt.js`, `test_crypto_random.js`) also have no importer even now; it reads `services.sync.log.`. Candidate for deletion with the Sync pref block.
8. Section 3d, `FxAccountsConfig.sys.mjs` lines 52 and 296 write/read `identity.sync.tokenserver.uri`, which section 5 deletes from `firefox.js`. The pref is a `CONFIG_PREFS` member read in a loop; verify it uses a default or leave the pref until 1f. Not in 3f.
9. Section 3c, `AboutLoginsParent.sys.mjs` line 114: the `AboutLogins:SyncEnable` message handler (`#syncEnable` calls `gSync.openFxAEmailFirstPage`) is reachable from the about:logins content page through the `AboutLoginsSyncEnable` event (`DesktopActorRegistry.sys.mjs` line 123, `AboutLoginsChild.sys.mjs` 91 and 235, `fxaccounts-button.mjs` 33). The report says only to drop the gSync call at 224. Remove the whole message path, or the entry point throws.

### C. Other staying-code references not in the report

| File | Reference | Action |
|---|---|---|
| `browser/components/sidebar/browser-sidebar.js` (193-196) | Existing `M6E:` patch `this._sidebars.delete("viewTabsSidebar")` | Remove with the sidebar definition (report drops 21 and 150-167 only). |
| `browser/components/preferences/config/account-sync.mjs` (850) | Existing `M6E:` comment in the `accountDisabled` group | Goes with the group; keep the M6E marker policy in mind. |
| `browser/base/content/main-popupset.js` (325-326) | `case "sidebar-switcher-tabs"` calls `SidebarController.show("viewTabsSidebar")` | Drop; the `sidebar-switcher-tabs` item in `main-popupset.inc.xhtml` (409-411) goes too. |
| `browser/base/content/appmenu-viewcache.inc.xhtml` (725-757) | `PanelUI-fxa-menu-sync-status-button`, `...-sync-status-off-card` and children | Sync-specific, add to the drop list in 3b. |
| `browser/components/preferences/preferences.js` (611-613, 619-625) | Comment about `sync-pane-loaded`; the notification is fired only by `sync.js` (line 132) in legacy mode | After deleting `sync.js` nothing fires `sync-pane-loaded`. About 33 test sites still wait for it (formautofill `browser_privacyPreferences.js`, `browser_creditCard_osAuth.js`, aboutlogins `browser_osAuthDialog.js`, `siteData/head.js`, `browser_bug1020245...`, `mozscreenshots/.../Preferences.sys.mjs` and others). Under redesign they already could not receive it; `head-common.js` picks `privacy-pane-loaded` when FxA is off. Not new breakage but note it, and fix the comment. |
| `toolkit/xre/nsAppRunner.cpp` (2643-2676, 2703) | Reads `PrefHasUserValue("services.sync.username")`, sets `nsIToolkitProfileService::hasSync`, and `hasSync` goes into the downgrade ping (`TestDowngradePing.cpp`, `downgrade-ping.md`) | `profileDowngrade.js` must keep working: dropping `AppConstants.MOZ_SERVICES_SYNC &&` would show the `sync` text when an old profile has the pref. Set `hasSync = false` in JS (or hide `#sync` in `profileDowngrade.xhtml`), and remove `profiledowngrade-sync2` from `profileDowngrade.ftl`. The C++ and IDL can stay (safe read, no default needed). |
| `toolkit/components/passwordmgr/LoginManagerPrompter.sys.mjs` (516-638) | Five `Services.obs.notifyObservers(... "weave:telemetry:histogram" ...)` calls | Dead notifications; optional removal. |
| `browser/components/customizableui/CustomizableWidgets.sys.mjs` (634, 669) | Observers on `sync-ui-state:update` | Go with the widget block already listed. |
| `browser/themes/shared/preferences/fxaPairDevice.css` | Used only by `fxaPairDevice.xhtml`; `browser/themes/shared/jar.inc.mn` line 178; `stylelint-rollouts.config.js` line 183 | Delete file, jar line and lint entry. |
| `browser/themes/shared/fxa/send.svg` (panelUI-shared.css 1311) and `send-to-device.svg` (1413) | Sync/Send Tab panel icons | Drop with the CSS blocks; jar lines 198 and 212. Also `monitor.svg` and `circle.svg` are FxA, leave. |
| `browser/tools/mozscreenshots/mozscreenshots/extension/configurations/Nova.sys.mjs` (254-260) | `sidebarSyncedTabs` configuration shows `viewTabsSidebar` | Drop the configuration. |
| `browser/components/migration/tests/marionette/test_refresh_firefox.py` (296-301, 555-565) | `createSync` / `checkSync` set and read `services.sync.username`; the refresh logic in `FirefoxProfileMigrator.sys.mjs` (416) keeps it | Safe (reads have defaults). Leave, or drop the Sync parts with the FxA ones in 1f. |
| `browser/app/profile/firefox.js` 3024 | `identity.fxaccounts.toolbar.syncSetup.panelAccessed` | FxA prefix, leave for 1f (its readers are in `browser-sync.js`; drop together). |
| `browser/locales/en-US/browser/appmenu.ftl` (333-346) | `appmenu-sync-promo-*`, `appmenu-bookmarks-sync-promo-*` | Add to the appmenu prefix list in section 5. |
| `python/mozbuild/mozbuild/action/test_archive.py` (151-161), `taskcluster/config.yml` (199, `'TPS': 'Sync tests'`) | TPS archive and treeherder group | Drop (the report covers test_archive only). |
| `testing/condprofile/condprof/scenarii/sync.js` and `full.py` (FxSync upload, lines 45, 80, 132) | Condprofile scenario that signs into Sync | Automation only; delete or leave, no build effect. |
| `docs/writing-rust-code/xpcom.md` (110-113), `docs/rust-components/developing-rust-components/index.md` (26) | Link to `/services/sync/rust-engines.md`, `bookmark_sync`, `TabsStore.sys.mjs` | The `/services/sync/rust-engines.md` link breaks the Sphinx build once `services/sync/docs` is gone. Edit both. Also `browser/docs/overview.md` (51) "Firefox Sync and Application Services", `browser/components/asrouter/docs/targeting-attributes.md` (`usesFirefoxSync`), `browser/components/places/docs/*overview.md` and `toolkit/components/telemetry/docs/start/remove-telemetry.md` mention Sync; wording only. |
| `toolkit/components/telemetry/Scalars.yaml` (`deletion.request.sync_device_id`, ~3537) | Mirror target of Glean `deletion.request.sync_device_id` in the deleted `metrics.yaml` | Dead scalar after 1e; batch with the other telemetry yaml removals. The report's section 5 lists Events and Histograms only. |
| `toolkit/modules/LogManager.sys.mjs` | Has "about:sync-log" comments | Stays: used by `SessionLogger.sys.mjs` and `FxAccountsCommon.sys.mjs`. Add to section 8. Fix comments only if desired. |
| `services/interfaces/moz.build` | `BUG_COMPONENT = ("Firefox", "Sync")` | Directory stays (`mozIInterruptible.idl` is shared); metadata only. |
| Glean categories missing from section 5 | `sync.client` (sync ping client info) and `deletion.request` (`sync_device_id`) are also defined in `services/sync/modules/metrics.yaml` | Deleted with the file; no consumers outside. |

### D. Verified claims

- `identity.fxaccounts.enabled` is locked false in `browser/app/profile/m6e.js` (line 20), `browser.settings-redesign.enabled` is true (`firefox.js` 3680), `gSync` has `onFxaDisabled()` (browser-sync.js 3974).
- `EventEmitter` swap for `webrtcUI.sys.mjs`: `toolkit/modules/EventEmitter.sys.mjs` has `on`, `off`, `emit` and `new` usage is compatible.
- `dogear` stays in `Cargo.lock` (the `places` crate at line 6396 depends on it); `bookmark_sync` unique deps (`atomic_refcell`, `cstr`, `storage_variant`) are shared with other crates (counts above 1).
- `PlacesSyncUtils.sys.mjs` imports only `Log` and `PlacesUtils`; `git grep services-sync toolkit/components/places` returns only `tests/sync/` and the deleted mirror.
- `BrowserUtils.INVALID_SHAREABLE_SCHEMES` default comes from `all.js` line 3771 as the report says; `getShareableURL` is also used by `tabgroup-menu.js`, so the pref replacement matters beyond the Share menu.
- `ext-storage.js` line 20 calls `getBoolPref("webextensions.storage.sync.kinto")` without a default, so removing the pref without editing the file throws, as the report assumes. `webextensions.storage.sync.enabled` (ext-storage.js 36) is defined nowhere in the tree, so there is nothing to keep.
- No hits for the feature in `devtools/`, `remote/`, `security/manager`, `services/settings`, `browser/installer`, `browser/confvars.sh`, `build/` mozconfigs, or the enterprise policy handlers (only `DisableFirefoxAccounts`, which locks `identity.fxaccounts.enabled`).
