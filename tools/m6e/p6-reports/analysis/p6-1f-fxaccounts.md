# P6.1f analysis: Mozilla accounts (services/fxaccounts)

Analyst: read-only. Base commit cb002d4874 (working tree at 4110e90366). Integration order 6: VPN, Translations, GenAI/AI window, ML and Sync are already deleted when this branch is integrated.

## Summary

- `services/fxaccounts` is a pure JS service (17 modules in `EXTRA_JS_MODULES` that load as `resource://gre/modules/FxAccounts*.sys.mjs` and `Credentials.sys.mjs`, one XPCOM component `@mozilla.org/fxaccounts/push;1`, two Glean files). No C++ or Rust of ours depends on it. The Rust crate `fxa-client` is vendored under `third_party/application-services` and only reached through the `megazords/*` workspace members, not through gkrust; it is left alone (D21).
- The service itself is easy to delete. The work is in the 100-odd staying files that import it or depend on UI that exists only for it. Hard breakage (an import of a deleted module, a `getBoolPref` with no default on a deleted pref, a Glean metric that no longer exists) comes from these groups:
  1. Browser window UI: the FxA toolbar button and avatar panel, app-menu FxA rows, menubar items (`browser-sync.js` / `gSync`, `appmenu-viewcache.inc.xhtml`, `navigator-toolbox.*`, `panelUI.js`, `browser.js`, `CustomizableUI`/`CustomizableWidgets`).
  2. Startup glue: `AccountsGlue.sys.mjs` (registered in `BrowserComponents.manifest`) and the two FxA Glean pings in `StartupTelemetry.sys.mjs`.
  3. Preferences: Sync/account pane, `fxaPairDevice` dialog, `main.js`, `preferences.js`.
  4. Content-visible entry points: `AboutWelcomeChild` exports `AWGetFxAMetricsFlowURI`, `UITour` exposes `showFirefoxAccounts`, `showConnectAnotherDevice`, `fxa`, `fxaConnections`, `SpecialMessageActions` handles `FXA_SIGNIN_FLOW`, and `FxAccountsWebChannel` is registered from `gSync.init`.
  5. Staying consumers of FxA OAuth/keys: `FirefoxRelay.sys.mjs`, `AboutProtectionsParent.sys.mjs`, `browser-trustPanel.js`, `ExtensionStorageSyncKinto.sys.mjs`, `BackupService.sys.mjs`, `TelemetryEnvironment.sys.mjs`, `ASRouterTargeting.sys.mjs`, `UITour.sys.mjs`.
  6. Password manager: `storage-json`, `LoginStore`, `LoginHelper` take `FXA_PWDMGR_HOST/REALM` from `FxAccountsCommon` and observe `fxaccounts:onlogout`.
  7. Indirect: `services/common/hawkclient|hawkrequest` import `Credentials.sys.mjs` and become dead (with `tokenserverclient`, once Sync is gone).
- Large overlap with Sync (feature 5). `browser-sync.js` (4322 lines, imports `resource://services-sync/*`), the Sync preferences pane, `firefox-view-*`, `syncedtabs/`, `sidebar-syncedtabs` and `browser-sync/` tests cannot survive the Sync deletion, so the Sync branch must delete or rewrite them first. I list them below under "Shared with Sync". Rule for the integrator: after cherry-picking Sync, re-run the sweep command at the end of this report; whatever still matches in those files is this branch's job, using the actions given here.
- Firefox Relay (`toolkit/components/satchel/integrations/FirefoxRelay.sys.mjs`) is not in any of the nine features but needs FxA OAuth for everything, and in its default state ("available") would offer to open an accounts.firefox.com sign-up. Decision needed (see Risks, item 1). Recommended: neuter its FxA calls and lock the feature pref to "disabled" in this step; delete Relay entirely as a follow-up.
- Config surface: no configure/moz.configure change (MOZ_SERVICES_SYNC belongs to Sync). No Rust change. Optional C++ cleanups only (two CSP allow-list lines, three libpref restrict-list lines). Front-end work needs `./mach build faster`; the Glean metric removal needs a regeneration but is not C++ source.

## Files to delete (directories and loose files)

Directories:
- `services/fxaccounts/` (everything: 17 `.sys.mjs`, `components.conf`, `metrics.yaml`, `pings.yaml`, `moz.build`, `tests/xpcshell` (22 tests + `head.js` + `xpcshell.toml`), `tests/browser` (4 files), `tests/mochitest` (3 files)).
- `browser/themes/shared/fxa/` except `avatar.svg` and `avatar-empty.svg` (see Must stay). Delete: `avatar-color.svg avatar-empty-circle.svg avatar-fox-circle.svg avatar-fox.svg circle.svg fxa-spinner.svg monitor.svg send-to-device.svg send.svg sync-devices.svg sync-illustration-issue.svg sync-illustration.svg sync-promo-bookmarks.svg sync-promo-connectdevice.svg sync-promo-signin.svg sync-promo-turnonsync.svg`. Note `sync-*`, `send*`, `sync-devices` are used by Sync UI (panelUI-shared.css, sync-promo.mjs, preferences.css); they go with those consumers.

Loose files, FxA-only:
- `browser/components/AccountsGlue.sys.mjs`
- `browser/components/preferences/fxaPairDevice.xhtml`, `fxaPairDevice.js`
- `browser/themes/shared/preferences/fxaPairDevice.css`
- `browser/locales/en-US/browser/preferences/fxaPairDevice.ftl`
- `browser/locales/en-US/browser/accounts.ftl`
- `toolkit/locales/en-US/services/accounts.ftl` (only user: `FxAccountsDevice.sys.mjs`)
- `browser/components/aboutlogins/content/components/fxaccounts-button.mjs`, `fxaccounts-button.css`
- `browser/components/aboutwelcome/assets/br-fxa-fox-mirror.svg` (only user: `AboutWelcomeDefaults.sys.mjs` mirror screen, which goes with the FxA screens)
- `toolkit/components/extensions/ExtensionStorageSyncKinto.sys.mjs` (FxA OAuth/keys based `storage.sync` backend; the only consumers are `ext-storage.js` behind a pref that defaults to false, and the Sync engine). Its tests: `toolkit/components/extensions/test/xpcshell/test_ext_storage_sync_kinto.js`, `test_ext_storage_sync_kinto_crypto.js`, `test_ext_storage_content_sync_kinto.js`.
- `services/common/hawkclient.sys.mjs`, `services/common/hawkrequest.sys.mjs` (import `Credentials.sys.mjs`; only users are FxAccountsClient and Sync). `services/common/tokenserverclient.sys.mjs` is Sync-only; delete here if the Sync branch left it. Tests: `services/common/tests/unit/test_hawkclient.js`, `test_hawkrequest.js`, `test_tokenserverclient.js` (+ `test_tokenauthenticatedrequest.js` if it imports tokenserverclient; check).

Tests, FxA-only (delete, and remove their manifest entries):
- `browser/components/aboutlogins/tests/browser/browser_fxAccounts.js`, `tests/chrome/test_fxaccounts_button.html` (+ `chrome.toml` entry)
- `browser/components/aboutwelcome/tests/browser/browser_aboutwelcome_fxa_signin_flow.js` (+ `browser.toml`)
- `browser/components/profiles/tests/browser/browser_fxa_menu_profiles.js`, `browser_fxa_menu_profiles_telemetry.js` (+ `browser.toml`, `browser-legacy.toml`)
- `browser/components/tests/browser/browser_browserGlue_fxa_client_info_ping.js`, `browser_browserGlue_client_association_ping.js` (+ `browser.toml`)
- `browser/components/uitour/test/browser_fxa.js`, `browser_fxa_config.js` (+ `browser.toml`)
- `browser/components/enterprisepolicies/tests/browser/browser_policy_disable_fxaccounts.js` (+ `browser.toml`) because it asserts FxA UI is hidden
- `toolkit/components/messaging-system/schemas/SpecialMessageActionSchemas/test/browser/browser_sma_show_firefox_accounts.js` (+ `browser.toml`)
- `browser/components/migration/tests/marionette/test_refresh_firefox.py`: remove `createFxa`, `testFxANoSync` and the FxAccountsStorage blocks (edit, not delete)

Shared with Sync (delete here only if the Sync branch did not):
- `browser/base/content/browser-sync.js` (+ `browser/base/jar.mn` line, `BrowserComponents.manifest` lines `gSync.init`/`gSync.uninit`, `browser.js` `gSync` getter, `browser/base/content/moz.build` `Files("browser-sync.js")`, `eslint-file-globals.config.mjs`, `eslint-rollouts.config.mjs` x2)
- `browser/base/content/test/browser-sync/` (whole directory; includes `browser_fxa_*`, `browser_appmenu_*signin*`, `browser_avatar_menu_*`, `browser_signed_out_avatar_variants.js`, `browser_sendtab_*`)
- `browser/components/preferences/sync.js`, `sync.inc.xhtml`, `config/account-sync.mjs`, `dialogs/syncChooseWhatToSync.{xhtml,js}`, `widgets/sync-device-name/`, `widgets/sync-engine-list/`, `tests/sync/`, `tests/chrome/test_sync_device_name.html`
- `browser/components/firefoxview/firefox-view-tabs-setup-manager.sys.mjs`, `firefox-view-synced-tabs-error-handler.sys.mjs`, `SyncedTabsController.sys.mjs`; `browser/components/syncedtabs/`; `browser/components/sidebar/sidebar-syncedtabs.*`; `browser/components/customizableui/content/sync-promo.mjs`
- `services/automation/` (Sync-owned, imports `FxAccountsConfig/Client`), `testing/tps/` (FxA staging flags in `mach_commands.py`, README), `testing/condprofile` FXA_USERNAME/FXA_PASSWORD (`util.py`, `scenarii/sync.js`, `tests/test_runner.py`)

## References from staying code

Legend: breaks = import of a deleted module, missing element/metric or getBoolPref without default. inert = reads a pref or id with a default, harmless but must go for cleanliness. Items marked (S) overlap with Sync; handle only if still present after Sync is integrated.

| File | What | Action |
|---|---|---|
| `services/moz.build` | `"fxaccounts"` in `DIRS` | Remove entry (breaks) |
| `toolkit/components/glean/metrics_index.py` | `services/fxaccounts/metrics.yaml` (line ~153), `services/fxaccounts/pings.yaml` (~233) | Remove both lines (breaks) |
| `toolkit/components/glean/tags.yaml` | tag `Firefox :: Firefox Accounts` | Remove (optional, nothing else uses it) |
| `browser/components/BrowserComponents.manifest` | line 26 `AccountsGlue.init`; lines 108, 150 `gSync.init/uninit` (S) | Remove the AccountsGlue line; remove gSync lines (S) |
| `browser/components/moz.build` | `"AccountsGlue.sys.mjs"` in `EXTRA_JS_MODULES`; `"syncedtabs"` DIRS (S) | Remove |
| `browser/components/StartupTelemetry.sys.mjs` ~141 | `GleanPings.fxAccountsClientInfo.setEnabled(true)` with its comment | Remove (breaks: ping gone) |
| `browser/base/content/browser.js` | lazy `fxAccounts` getter (~132); `gSync` script getter (~257, S); `gFxaToolbarEnabled`, `gFxaToolbarAccessed` pref getters (~589-603); `updateFxaToolbarMenu()` (~838-880); `TabsSetupFlowManager.didFxaTabOpen` + `_closeDeviceConnectedTab` in the Firefox View block (~5200-5226, reads `identity.fxaccounts.remote.root`); `sync-promo` element callback (~652, S) | Delete all of these |
| `browser/base/content/browser.js.globals` | `updateFxaToolbarMenu`, `gFxaToolbarEnabled`, `gFxaToolbarAccessed`, `fxAccounts` | Remove entries |
| `browser/base/content/browser-init.js` ~281 | `updateFxaToolbarMenu(gFxaToolbarEnabled, true)` | Remove call |
| `browser/base/content/navigator-toolbox.inc.xhtml` ~476-490 | `<toolbarbutton id="fxa-toolbar-menu-button">`, `fxa-avatar-image`, `fxa-avatar-label` | Remove element |
| `browser/base/content/navigator-toolbox.js` ~136, 161, 361, 448 | `#fxa-toolbar-menu-button` in selector lists; `case "fxa-toolbar-menu-button": gSync.toggleAccountPanel` | Remove |
| `browser/base/content/appmenu-viewcache.inc.xhtml` | `appMenu-fxa-status2` block and promo (lines ~27-88, `appMenu-fxa-separator`), `PanelUI-remotetabs` (524, S), `PanelUI-fxa` and the 9 `PanelUI-fxa-*` panelviews (657-965), `chrome://browser/skin/fxa/avatar-empty.svg` refs (48, 667) | Remove all FxA/synced-tabs views. `PanelUI-fxa-menu-all-profiles` and `-create-profile` host the Profiles UI (see browser-profiles.js); drop them together |
| `browser/base/content/browser-menubar.inc.xhtml` ~366-382 | `menu-tools-fxa-sign-in2`, `menu-tools-fxa-re-auth` items (and the Sync items nearby, S) | Remove items |
| `browser/base/content/browser-menubar.js` ~42-67 | `gSync.*` command cases (S) | Remove cases |
| `browser/base/content/browser-profiles.js` | FxA panel hooks: `fxaMenu*` nodes (28-80), `_onFxaMenuPanelShowing` (133-230), `updateFxAView` (386), `PanelUI-fxa-*` event cases and `gSync.emitFxaToolbarTelemetry` calls (420-560) | Remove the FxA-panel parts, keep the app-menu Profiles code |
| `browser/base/content/browser-trustPanel.js` | `FX_MONITOR_OAUTH_CLIENT_ID`, `fxAccounts` getter (15, 31), `#clearFxaOauthClientCache` + `fxaccounts:onlogout` observer (193, 256, 282, 1986), `#hasMonitorAccount()` (~1132, also uses `UIState`, S) | Make `#hasMonitorAccount()` return false or remove it and simplify `#hasMonitorAccountOrStoredPasswords`; remove observer/field/imports |
| `browser/base/content/popup-notifications.inc.xhtml` | 252 `skin/fxa/avatar.svg` (identity credential list item); 267-301 `fxa-and-relay-integration-offer-*` popupnotifications | Keep the avatar asset (Must stay). Relay notifications: remove if Relay is deleted (open question) |
| `browser/base/content/main-popupset.js`, `browser-context.js`, `browser-places.js`, `browser/components/tabbrowser/content/tab-context-menu.js`, `browser/components/sharing/SharePageAction.sys.mjs`, `SharingUtils.sys.mjs` | `gSync.*` calls (send tab to device, synced-tabs context menus, sync promo) (S) | Remove with the Sync branch; leftovers are this branch's |
| `browser/themes/shared/customizableui/panelUI-shared.css` (119 hits) | `#appMenu-fxa-*`, `#PanelUI-fxa*`, `fxastatus` avatar vars, `skin/fxa/*` urls | Delete the FxA/Sync rules |
| `browser/themes/shared/toolbarbutton-icons.css` 178 | `[badge-status="fxa-needs-authentication"]` | Remove rule |
| `browser/themes/shared/preferences/preferences.css` | `#fxaLoginStatus`, `.fxaProfileImage`, `#fxaGroup`, `.fxaSyncIllustration`, `.fxaMobilePromo` etc. (22 hits) | Remove |
| `browser/themes/shared/jar.inc.mn` | skin entries for `fxa/*` (191-213) and `preferences/fxaPairDevice.css` (178) | Remove all except `fxa/avatar.svg`, `fxa/avatar-empty.svg` |
| `browser/components/preferences/jar.mn` | `fxaPairDevice.xhtml/js` (16-17), `sync.js`, `config/account-sync.mjs`, `widgets/sync-device-name.mjs` (S) | Remove |
| `browser/components/preferences/dialogs/jar.mn` | `syncChooseWhatToSync.*` (S) | Remove |
| `browser/components/preferences/preferences.js` | import and `fxAccounts` var (61-64); `/* import-globals-from sync.js */`; `account-sync` section registration (476-488); `identity.fxaccounts.enabled` branch and `pane-account-sync-title2` (615-620) | Remove |
| `browser/components/preferences/preferences.xhtml` | `#include sync.inc.xhtml`, `sync-device-name.mjs` script (S) | Remove |
| `browser/components/preferences/main.js` ~724-735 | `FxAccounts.canConnectAccount()`/`promiseConnectAccountURI` (account-linked "Sign in" action) | Remove the handler and its UI |
| `dom/security/nsContentSecurityUtils.cpp` 1266, 1352 | `fxaPairDevice.xhtml` in two chrome allow-lists (+ `syncChooseWhatToSync.xhtml`, S) | Optional cleanup; stale entries are inert. C++ rebuild of dom/security only |
| `browser/components/aboutlogins/AboutLoginsParent.sys.mjs` | `FxAccounts` lazy (23), `FXA_ENABLED` pref (39), `openFxAEmailFirstPage` via gSync (224), `promiseManageURI` (847), `fxAccountsEnabled` state (853) | Remove; state field `fxAccountsEnabled` removed from the page's init payload |
| `browser/components/aboutlogins/content/aboutLogins.html`, `aboutLogins.mjs`, `aboutLogins.css`, `aboutLoginsUtils.mjs`, `jar.mn` | `<fxaccounts-button>` element, its template, script tag, `fxaLoggedIn` logic, jar entries, selector lists; intro strings `login-intro-instructions-fxa-*` | Remove (breaks: jar entries for deleted files) |
| `browser/components/aboutwelcome/actors/AboutWelcomeChild.sys.mjs` 75, 268 | `AWGetFxAMetricsFlowURI` exportFunction (page-visible API) | Remove |
| `browser/components/aboutwelcome/actors/AboutWelcomeParent.sys.mjs` 20, 264 | `FxAccounts` lazy, `AWPage:FXA_METRICS_FLOW_URI` handler | Remove |
| `browser/components/aboutwelcome/content-src/aboutwelcome.jsx` + generated `content/aboutwelcome.bundle.js` | `fetchFxAFlowUri` | Edit source; regenerate the bundle with `./mach newtab bundle` style flow (or hand-edit the 6 bundle lines) |
| `browser/components/aboutwelcome/modules/AboutWelcomeDefaults.sys.mjs` | `FXA_SIGNIN_FLOW`/`FXA_AIWINDOW_SIGNIN_FLOW` steps, `isFxASignedIn` targeting, `br-fxa-fox-mirror.svg` (171-808), pref check at 972 | Remove FxA screens and the pref branch |
| `browser/components/aboutwelcome/jar.mn` 36 | `br-fxa-fox-mirror.svg` | Remove |
| `browser/components/asrouter/modules/ASRouterTargeting.sys.mjs` | `FxAccounts` lazy (109), `FXA_ENABLED_PREF`, `hasAccessedFxAPanel` pref getter (138, 1088), `isFxAEnabled`/`isFxASignedIn` (168, 845-860), `attachedFxAOAuthClients` (1127), `FXA_USERNAME_PREF` | Remove the lazy and real implementations. Keep `isFxAEnabled`/`isFxASignedIn` as constant `false` getters (Nimbus parity test and ~20 message targetings reference them), `hasAccessedFxAPanel` false, `attachedFxAOAuthClients` `[]` |
| `browser/components/asrouter/modules/ASRouterPreferences.sys.mjs` 29, 74 | `services.sync.username` as targeting pref | Remove (S) |
| `browser/components/asrouter/modules/MenuMessage.sys.mjs`, `content/components/menu-message/menu-message.mjs`, `content-src/templates/OnboardingMessage/MenuMessage.schema.json`, `content-src/schemas/MessagingExperiment.schema.json` | `fxa_cta` message type, `SHOWING_FXA_MENU_MESSAGE_ATTR`, `PanelUI-fxa` / `fxa-toolbar-menu-button` lookups (201-247, breaks: `getViewNode` on a deleted view), `fxa-menu-message-close-button` l10n | Remove `fxa_cta` type and the FxA panel code paths; remove the `pxi_menu` surface if it has no other types |
| `browser/components/asrouter/modules/OnboardingMessageProvider.sys.mjs`, `PanelTestProvider.sys.mjs`, `MessagingTargetingConstants.sys.mjs` (`FXA_NOT_SIGNED_IN`), `modules/test2` | Messages with `FXA_SIGNIN_FLOW`, `#fxa-toolbar-menu-button` selectors, `FXA_ACCOUNTS_BADGE*`, `fxa_cta` | Remove those messages/constants |
| `browser/components/asrouter/docs/*.md` | targeting docs for `isFxA*`, `menu-message.md`, `sidebar-chatbot-promo.md`, `messaging-surfaces.md` | Edit text |
| `toolkit/components/messaging-system/lib/SpecialMessageActions.sys.mjs` | `FxAccounts` and `FxAccountsCommon` lazies (46, 59), `fxaSignInFlow()` (545-685), `SHOW_FIREFOX_ACCOUNTS` (1073-1081), cases `FXA_SIGNIN_FLOW`, `FXA_AIWINDOW_SIGNIN_FLOW` (1094-1097) | Remove methods and cases |
| `toolkit/components/messaging-system/lib/MessagingSystemBlocklists.sys.mjs` 64-65, 130 | action names and `identity.fxaccounts.` pref prefix in blocklists | Remove entries; update `test_MessagingSystemBlocklists.js` |
| `toolkit/components/messaging-system/schemas/SpecialMessageActionSchemas/SpecialMessageActionSchemas.json`, `index.md` | `SHOW_FIREFOX_ACCOUNTS`, `FXA_SIGNIN_FLOW`, `FXA_AIWINDOW_SIGNIN_FLOW` | Remove the schema branches and doc sections |
| `browser/extensions/newtab/lib/PrefsFeed.sys.mjs` 724 | `values.fxa_endpoint` from `browser.newtabpage.activity-stream.fxaccounts.endpoint` | Remove (value has no content consumer) |
| `browser/extensions/newtab/lib/Widgets/PrivacyMessages.sys.mjs` ~143 | `CTA_SIGNIN` with `FXA_SIGNIN_FLOW` | Remove CTA and any message using it |
| `browser/components/uitour/UITour.sys.mjs` | `FxAccounts`/`fxAccounts` lazies (19, 29), target `accountStatus` -> `#appMenu-fxa-label2` (86-91), cases `showFirefoxAccounts`, `showFirefoxAccountsForAIWindow`, `showConnectAnotherDevice` (461-540), `FxA:SignedInStateChange` (748), config cases `fxa`, `fxaConnections`, `sync` (1644-1670), `getFxA`, `getFxAConnections` (1742-1830) | Remove all of these |
| `browser/components/uitour/UITour-lib.js` + docs | `showFirefoxAccounts`, `showConnectAnotherDevice`, `fxa`, `fxaConnections` helpers and typedefs | Remove (the lib is served to web pages; deleting API surface is desired) |
| `browser/components/customizableui/CustomizableUI.sys.mjs` 288, 376, 775 | `fxa-toolbar-menu-button` in default navbar placements and in the `currentVersion < 16` migration | Remove all three (the migration branch becomes empty; drop it, keep the version number) |
| `browser/components/customizableui/CustomizableWidgets.sys.mjs` 527-690 | `if (identity.fxaccounts.enabled)` block: `sync-button` widget, `send-tab`-style widgets using `gSync` and `fxaccounts:devicelist_updated` | Remove the block (S) |
| `browser/components/customizableui/content/panelUI.js` 471-504, 811 | `#appMenu-nova-fxa-label`, `appMenu-fxa-label2` cases, `MenuMessage.SHOWING_FXA_MENU_MESSAGE_ATTR` | Remove |
| `browser/modules/BrowserUsageTelemetry.sys.mjs` 181 | `fxa-toolbar-menu-button` in the toolbar widget id list | Remove |
| `browser/components/backup/BackupService.sys.mjs` | `fxAccounts` lazy (68-71), `machineName: lazy.fxAccounts.device.getLocalName()` (3132), `UIState` account meta (3155-3158) | `machineName`: use `Services.sysinfo.get("device") \|\| Services.dns.myHostName` (or drop the field); remove `accountID`/`accountEmail`. Check backup tests that assert the fields. `MiscDataBackupResource.sys.mjs` 23: comment only |
| `browser/actors/AboutProtectionsParent.sys.mjs` | `FXA_PWDMGR_*` and `fxAccounts` lazies (11-24); `getLoginData()` (refreshDeviceList, FxA login subtraction, `mobileDeviceConnected`); Monitor OAuth token flow (`getMonitorData`, `fetchUserBreachStats`, `getMonitorScopedOAuthToken`, 214-300); `VPNSubStatus` uses `fxAccounts.activeSubscriptions()` | `getLoginData`: count logins with literal host/realm constants, `mobileDeviceConnected: false`. Remove Monitor and VPN subscription fetches and the `FetchMonitorData` / `FetchVPNSubStatus` / `ClearMonitorCache` messages, plus the matching content in `browser/components/protections/content/` and the `monitor-known-breaches` block in `protections.html` (uses `skin/fxa/avatar.svg`) |
| `toolkit/components/passwordmgr/storage-json.sys.mjs` 12-13, 50 | `FXA_PWDMGR_HOST/REALM` lazy from FxAccountsCommon, `isFXAHost()` | Replace the two lazy entries by local consts `"chrome://FirefoxAccounts"` and `"Firefox Accounts credentials"` (zero behaviour change) |
| `toolkit/components/passwordmgr/LoginStore.sys.mjs` 48-49, 111-116 | same constants in `_backupHandler` | same |
| `toolkit/components/passwordmgr/LoginHelper.sys.mjs` 437-442, 1830 | `fxaccounts:onlogout` observer resetting `signon.firefoxRelay.feature`; `isUserFacingLogin` literal | Remove the observer (keep the literal) |
| `toolkit/components/passwordmgr/storage-rust.sys.mjs` | `deleteAllExceptFxa`, `wipeLocalExceptFxa` | Keep: these are API names of the vendored Rust logins component (see Must stay) |
| `toolkit/components/satchel/integrations/FirefoxRelay.sys.mjs` | lazies `fxAccounts`, `fxAccountsCommon` (51-58); `getRelayTokenAsync`, `hasFirefoxAccountAsync`, `fetchWithReauth`, `offerRelayIntegration*`, `#fetchRelayProfileInfo` | See Risks 1. Minimum: remove both lazies, make token/session helpers return undefined/false, make `offerRelayIntegration*` and `getRelayProfileInfo` return null, drop the sign-up tab opening (`promiseConnectAccountURI`) |
| `toolkit/components/extensions/parent/ext-storage.js` 17-27, 224-250 | Kinto branch of `extensionStorageSync` and `dummyContextForKinto` | Keep only the Rust `ExtensionStorageSync` path (breaks otherwise) |
| `toolkit/components/extensions/Extension.sys.mjs` 107-111 | `storageSyncOldKintoBackend` default `true` | Remove the entry. If the pref `webextensions.storage.sync.kinto` is removed first, this default would flip to Kinto |
| `modules/libpref/init/all.js` 3555 | `pref("webextensions.storage.sync.kinto", false)` | Remove with the above |
| `toolkit/components/telemetry/app/TelemetryEnvironment.sys.mjs` 31-35, 1068, 1472-1499 | `fxAccounts` lazy, `_getFxaSignedInUser`, `_updateServicesInfo` (also uses the Sync service `@mozilla.org/weave/service;1`, S), call at 1068 | Delete all three and the `Glean.fxa.syncEnabled/accountEnabled` writes |
| `toolkit/components/telemetry/Events.yaml` 595-697 | groups `fxa`, `fxa_avatar_menu`, `fxa_app_menu`; and `synced_tabs.click` objects 1132-1137 | Remove the three groups; remove those two object names from `synced_tabs` (S) |
| `toolkit/modules/BrowserUtils.sys.mjs` 1041-1053 | `PromoType.RELAY.extraCheck` reads FxA/token server prefs | Drop `extraCheck` (inert); update `test_BrowserUtils.js` `test_shouldShowRelayPromo` |
| `toolkit/modules/Troubleshoot.sys.mjs` 77 | `identity.fxaccounts.enabled` in the reported prefs list | Remove entry |
| `browser/components/enterprisepolicies/Policies.sys.mjs` 2843 | `identity.fxaccounts.toolbar.` allowed prefix | Remove |
| `browser/components/enterprisepolicies/Policies.sys.mjs` 1114, 1263; `schemas/policies-schema.json` 1359; `content/aboutPolicies.js` 429 | `DisableAccounts`, `DisableFirefoxAccounts` policies set `identity.fxaccounts.enabled=false` | Keep (see Must stay) |
| `browser/components/firefoxview/metrics.yaml` 212-231 | `fxa_continue_sync`, `fxa_mobile_sync` (S) | Remove with the Firefox View synced-tabs code |
| `browser/components/sidebar/sidebar-opentabs.mjs` 17, 151 | `FxAccounts.config.promisePairingURI` | Remove the "pair device" action (S) |
| `browser/components/sidebar/browser-sidebar.js` 193-196 | M6E guard `getBoolPref("identity.fxaccounts.enabled")` for synced tabs | Remove together with the synced-tabs sidebar (S); this is a `getBoolPref` without default |
| `browser/app/profile/firefox.js` | See "Prefs" below | Remove the FxA blocks |
| `browser/app/profile/m6e.js` 18-20 | `identity.fxaccounts.enabled` locked false | Keep one tombstone line for this step (see Must stay) |
| `modules/libpref/init/all.js` 3839-3844 | `identity.fxaccounts.auth.uri`, `identity.fxaccounts.auth.useBearer` inside `#ifdef MOZ_SERVICES_SYNC` | Remove both (the `#ifdef` block is Sync's) |
| `modules/libpref/Preferences.cpp` 7158-7160 | `identity.fxaccounts.*` in `sRestrictFromWebContentProcesses` | Leave (inert, C++); optional cleanup in a C++ batch |
| `services/common/hawkrequest.sys.mjs` 24-29 | `identity.fxaccounts.auth.useBearer`, imports `Credentials` | File is deleted (see above) |
| `remote/shared/RecommendedPreferences.sys.mjs` 356-357; `testing/marionette/client/marionette_driver/geckoinstance.py` 155-156; `testing/profiles/unittest-required/user.js` 116-121; `testing/profiles/perf/user.js` 57; `testing/profiles/common/user.js` 22 | Dummy FxA server prefs for automation | Remove lines (inert). `remote/` tests may list the prefs; run `remote/shared/test` if present |
| `testing/geckodriver/src/prefs.rs` 145 | `browser.newtabpage.activity-stream.fxaccounts.endpoint` | Leave (geckodriver is not built into M6E; Rust) |
| `tools/m6e/acceptance.py` 116-136 | "Mozilla accounts and Sync" check expects `fxa-toolbar-menu-button`, `appMenu-fxa-status2` absent and the pref false | Keep the absent-element check (now trivially true); keep or drop the pref assertion consistently with the tombstone decision |
| `tools/m6e/branding/fox-art.txt` 8 | lists `br-fxa-fox-mirror.svg` | Remove line |
| `tools/@types/generated/{lib.gecko.glean.d.ts,lib.gecko.modules.d.ts,tspaths.json}` | generated typings: 66 + 19 + 37 hits (module paths, `Glean.fxa`, `clientAssociation`, `syncSettings`, pings) | Regenerate (`./mach ts` equivalent) or hand-remove entries |
| `tools/lint/extra-js-modules/allowlist.txt` 201-217; `.prettierignore` 1186; `eslint-ignores.config.mjs` 241; `eslint-rollouts.config.mjs` 1839 (and 59, 176 for browser-sync.js); `stylelint-rollouts.config.js` 25, 183; `eslint-file-globals.config.mjs` 107 | lint entries for deleted paths | Remove (P6.2 also sweeps stale entries) |
| `Cargo.toml` 28, 124; `Cargo.lock` | workspace members `fxa-client`, `examples/fxa-client` and lock entry | Leave (vendored, D21; not linked by gkrust) |
| `docs/`, `dom/docs/push/index.md` 68, `browser/docs/overview.md`, `docs/contributing/*`, `docs/overview/ios.md` | prose mentions of Firefox Accounts / `FxAccountsPush` | Edit `dom/docs/push/index.md` (mermaid node); others are prose and may stay |
| `python/l10n/fluent_migrations/bug_2032640_send_tab_account_menu.py` | migration for `browser/sync.ftl` | Leave (historic) |

Other FTL (strings that become unused; removal is cosmetic and follows the consumers): `browser/locales/en-US/browser/{appmenu,sync,menubar,firefoxView,featureCallout,aboutLogins,syncedTabs,firefoxRelay,preferences/preferences,newtab/asrouter,newtab/onboarding}.ftl`. Delete FxA message ids by prefix: `appmenu-fxa-*`, `menu-tools-fxa-*`, `fxa-menu-message-*`, `fxaccounts-*`, `login-intro-instructions-fxa-*`, `firefoxview-tabpickup-fxa-*`, `sync-account-*`, `sync-device-name-*`, `account-*` (accounts.ftl), `cfr-doorhanger-bookmark-fxa-header`, `firefox-relay-and-fxa-*` (Relay).

## References from earlier features (ignore)

- VPN: `toolkit/components/ipprotection/fxa/*` (GuardianClient, IPPSignInWatcher, IPPFxaActivateAuthProvider, Android providers), `toolkit/components/ipprotection/docs/Components.md`, `browser/components/ipprotection/*`, `toolkit/components/ipprotection/tests/xpcshell/test_IPPFxaActivateAuthProvider.js`, `toolkit/content/aboutLogging/aboutLogging.mjs` 195 (`IPP_FxaActivateAuthProvider` log module), `browser/components/preferences/tests/privacy/browser_privacy_ipprotection.js`, `SpecialMessageActionSchemas/index.md` 690, `VPN_OAUTH_CLIENT_ID` uses in `browser-sync.js`.
- GenAI / AI window: `browser/components/aiwindow/**` (`models/AuthProvider.sys.mjs`, `browser_utils_fxAccount.js`, `browser_aiwindow_account_auth.js`, metrics), `browser/locales/en-US/browser/aiWindow.ftl`, `UITour` `showFirefoxAccountsForAIWindow`, `FXA_AIWINDOW_SIGNIN_FLOW` (SpecialMessageActions, schemas, MessagingSystemBlocklists, AboutWelcomeDefaults), `browser_sma_fxa_aiwindow_signin_flow.js`, `browser_smartwindow_*` tests, `test_restore_aiwindow.py`.
- Sync (everything under `services/sync`: `UIState`, `SyncedTabs`, `sync_auth`, `modules-testing/fxa_utils`, tps `auth/fxaccounts.sys.mjs`, engines incl. `extension-storage.sys.mjs`, tests, `aboutSyncLog`), and the Sync-owned UI listed under "Shared with Sync" once the Sync branch has handled it.
- Translations, ML: no references found.

## Prefs/metrics/strings to remove

Prefs (all defaults; remove from `browser/app/profile/firefox.js` unless noted):
- `identity.fxaccounts.enabled` (firefox.js 2374; keep tombstone in m6e.js, see Must stay)
- `identity.fxaccounts.remote.root`, `.contextParam`, `.remote.profile.uri`, `.remote.oauth.uri`, `.pairing.enabled`, `.pairing.version`, `.remote.pairing.uri`, `.autoconfig.uri`, `.commands.missed.fetch_interval`, `.commands.remoteTabManagement.enabled`, `.telemetry.clientAssociationPing.enabled`, `.telemetry.clientInfoPing.enabled`
- `identity.fxaccounts.toolbar.enabled`, `.accessed`, `.defaultVisible`, `.pxiToolbarEnabled` (+ `.monitorEnabled`, `.relayEnabled`, `.vpnEnabled`), `.syncSetup.panelAccessed`, `.appMenuSignInPromo.dismissed`
- `identity.fxaccounts.auth.uri`, `identity.fxaccounts.auth.useBearer` (all.js, Sync's ifdef)
- Runtime-only (no default; written by the service): `identity.fxaccounts.account.device.name`, `.account.telemetry.sanitized_uid`, `.lastSignedInUserHash`, `.allowHttp`, `.lastSignedInUserHash`; test prefs referencing them are inert
- `identity.sendtabpromo.url`, `identity.sendtab.deviceissues.url`, `identity.mobilepromo.android`, `identity.mobilepromo.ios`, `identity.sync.tokenserver.uri` (users are only `browser-sync.js` and `preferences/sync.js`)
- `browser.engagement.fxa-toolbar-menu-button.has-used`
- `browser.newtabpage.activity-stream.fxaccounts.endpoint` (firefox.js 1917; plus `testing/profiles/common/user.js`)
- `webextensions.storage.sync.kinto` (all.js) together with its readers

Glean (all defined in files that are deleted, so the definitions go with them; remove every use in staying code):
- metrics.yaml categories: `client_association` (`uid`, `legacy_client_id`), `fxa` (`connect_account`, `disconnect_account`, `sync_enabled`, `account_enabled`, `sendtab_sent/received`, `closetab_sent/received`), `sync_settings` (`open_choose_what_to_sync_menu`, `save`)
- pings: `fx-accounts` (`GleanPings.fxAccounts`), `fx-accounts-client-info` (`GleanPings.fxAccountsClientInfo`)
- Legacy telemetry events in `Events.yaml`: `fxa`, `fxa_avatar_menu`, `fxa_app_menu`
- Defined in Sync's `services/sync/modules/metrics.yaml` (go with Sync): `fxa_avatar_menu`, `fxa_app_menu`, `synced_tabs`, `sync_merge_dialog`
- Nimbus (later feature, see next list): targeting-context metrics `is_fx_a_enabled`, `is_fx_a_signed_in`

Nimbus references (feature 9 is integrated after this one; per the rule they are handled here). Since Nimbus is deleted wholesale later, the lowest-cost handling is to leave them if the Nimbus parity checks still pass; if you want a clean tree at this step:
- `toolkit/components/nimbus/FeatureManifest.yaml`: features `fxaButtonVisibility` (4177), `fxaClientAssociation` (4228), `fxaAppMenuItem` (4241), `fxaAvatarMenuItem` (4261), `expandSignInButton` (~5849), `remoteTabManagement` `fallbackPref` (5361); `tools/@types/subs/FeatureManifest.sys.d.mts` (generated)
- `toolkit/components/nimbus/lib/TargetingContextRecorder.sys.mjs` 153-154, 205-206 and `metrics.yaml` 158-159, 312, 634-650: `isFxAEnabled`, `isFxASignedIn`. These must stay while `ASRouterTargeting` still exposes them (recommended constant-false getters), otherwise the Nimbus parity test (`toolkit/components/nimbus/test/python`, `dump-targeting-context.js`) fails.
- Updater and crash reporter: no references (the `fxa` hits in `toolkit/crashreporter/google-breakpad` and `toolkit/mozapps/update/docs/*.svg` are unrelated).

## Build-system edits

Front-end only (`./mach build faster`):
- `services/moz.build` DIRS; `services/fxaccounts/moz.build` goes with the directory (EXTRA_JS_MODULES, XPCOM_MANIFESTS, three test manifests).
- `browser/components/moz.build` (`AccountsGlue.sys.mjs`), `BrowserComponents.manifest`, `browser/components/aboutlogins/jar.mn`, `browser/components/aboutwelcome/jar.mn`, `browser/components/preferences/jar.mn`, `browser/components/preferences/dialogs/jar.mn` (S), `browser/themes/shared/jar.inc.mn`, `browser/base/jar.mn` (S), `services/common/moz.build` (hawk*, tokenserver), `toolkit/components/extensions/moz.build` (`ExtensionStorageSyncKinto.sys.mjs`), `toolkit/components/extensions/test/xpcshell/xpcshell-common.toml` (3 kinto tests), `services/common/tests/unit/xpcshell.toml`, test manifests listed under "Tests".
- `toolkit/components/glean/metrics_index.py` (2 lines): this is metrics-registry input read at configure/build time by the Glean generator; it is a Python edit with no mozconfig/moz.configure change, but it regenerates `GleanMetrics`/`GleanPings` C++/JS bindings and `ping` lists, so the next `./mach build` (not only `faster`) is the one that picks it up. Plan for a full `./mach build` after this step, not for a configure rerun.
- `modules/libpref/init/all.js` and `browser/app/profile/firefox.js`: preference preprocessing; picked up by a normal build.
- `tools/@types` regeneration, lint configs: no build impact.

Configure-time: none from this feature. (`MOZ_SERVICES_SYNC` in `toolkit/moz.configure`, `browser/moz.configure`, `modules/libpref/moz.build`, `toolkit/modules/moz.build`, `AppConstants` belongs to the Sync branch; this branch relies on the Sync branch for the `#ifdef MOZ_SERVICES_SYNC` block in `all.js` and must edit the two FxA prefs inside it.)

C++/Rust: none required. Optional C++ cleanups (rebuild of dom/security and libpref): `dom/security/nsContentSecurityUtils.cpp` 1266 and 1352, `modules/libpref/Preferences.cpp` 7158-7160. No Cargo/Cargo.lock change; `fxa-client` stays in the vendored workspace and is not a dependency of `toolkit/library/rust`.

## Tests to delete or edit

Delete (see "Files to delete"): all of `services/fxaccounts/tests/**`; the FxA-only tests listed above.

Edit:
- `toolkit/modules/tests/xpcshell/test_BrowserUtils.js`: `test_shouldShowRelayPromo` (autoconfig pref cases)
- `toolkit/components/messaging-system/lib/test/unit/test_MessagingSystemBlocklists.js`
- `browser/components/asrouter/tests/**`: `browser_asrouter_menu_messages.js` (59 FxA hits), `browser_asrouter_targeting.js` (23), `browser_asrouter_toolbarbadge.js` (14), `unit/ToolbarBadgeHub.test.js` (18), `unit/ASRouter.test.js`, `unit/unit-entry.js`, `head.js`, `NimbusRolloutMessageProvider.sys.mjs` (41), `browser_asrouter_newtab_messages.js`, `browser_feature_callout.js`, `browser_asrouter_toast_notification.js`
- `browser/components/aboutwelcome/tests/browser/{browser_aboutwelcome_multistage_default.js,browser_aboutwelcome_multistage_mr.js,browser_aboutwelcome_import.js,head.js}`, `tests/unit/unit-entry.js`, `browser.toml`
- `browser/components/customizableui/test/*`: `browser_884402_customize_from_overflow.js`, `browser_1042100_default_placements_update.js`, `browser_2041653_non_removable_overflow_reset.js`, `browser_923857_customize_mode_event_wrapping_during_reset.js`, `browser_panelUINotifications.js`, `browser_914138_widget_API_overflowable_toolbar.js`, `browser_proton_toolbar_hide_toolbarbuttons.js`, `unit/test_unified_extensions_migration.js` (remove `fxa-toolbar-menu-button` from expected placements); Sync-related ones `browser_synced_tabs_menu.js`, `browser_history_sync_promo.js`, `browser_bookmarks_sync_promo.js` (S)
- `browser/base/content/test/browser-keyboard/browser_toolbarKeyNav.js` (9), `browser-performance/{head.js,browser_startup.js,browser_appmenu.js,browser_tab*.js,browser_windowopen.js}` (remove FxA button from allowlists), `browser/modules/test/browser/{browser_UsageTelemetry_toolbars.js,browser_UsageTelemetry_interaction.js}`
- `browser/components/urlbar/tests/browser-trustPanel/browser_trust_panel.js`, `browser/components/taskbartabs/test/browser/browser_taskbarTabs_chromeTest.js`, `browser/components/privatebrowsing/test/browser/browser_privatebrowsing_about_search_banner.js`, `browser/components/sharing/tests/browser/browser_testSharePanelSendToDevice.js` (S)
- `browser/components/preferences/tests/{browser_legacy_pane_mappings.js,head-common.js}` and ~35 tests that set `identity.fxaccounts.account.device.name` (inert; optional); `moreFromMozilla/head.js` (`mockDefaultFxAInstance`, 14 hits), `browser_moreFromMozilla*.js`
- `browser/components/uitour/test/browser_UITour_sync.js` (S), `browser.toml`
- `browser/components/extensions/test/xpcshell/test_ext_browsingData_passwords.js` (uses literal `chrome://FirefoxAccounts`; still valid, no change needed)
- `toolkit/components/passwordmgr/test/browser/browser_deleteLoginsBackup.js` (imports FxAccountsCommon constants: replace with literals)
- `toolkit/components/extensions/test/xpcshell/{test_ext_storage_content_sync.js,test_ext_storage_sync.js,test_ext_api_events_listener_calls_exceptions.js,head_storage.js}`, `test/mochitest/test_ext_storage_cleanup.html` (drop the `webextensions.storage.sync.kinto` setting)
- `toolkit/components/passwordmgr/test/browser/browser_relay_*.js` (5 files) and `moreFromMozilla/browser_moreFromMozilla_relay.js`: delete or rewrite depending on the Relay decision
- `browser/components/backup/tests/**` (assert `accountID`, `machineName` if any)
- `browser/components/migration/tests/marionette/test_refresh_firefox.py`
- `taskcluster/test/data/bugbug-push-schedules.json`, `taskcluster/test/params/*.yml`: data fixtures; leave

Add: none. Suggested regression run for this step: the preferences, customizableui, uitour, aboutlogins, aboutwelcome, asrouter, passwordmgr, telemetry (`test_TelemetryEnvironment.js`), extensions storage, backup, enterprisepolicies suites and `browser/base/content/test/performance`, plus `tools/m6e/run_tests.sh` and `tools/m6e/acceptance.py`.

## Must stay (with proof)

1. Locked `identity.fxaccounts.enabled=false` in `browser/app/profile/m6e.js`, as a temporary tombstone. Reason: several staying readers use `getBoolPref` without a default; policies still set the pref. Proof of unreachability after the branch: no module exists to import, no listener (the service opens no sockets; its only network clients were `FxAccountsClient`/`ProfileClient`/`PairingChannel` WebSocket, all deleted), `components.conf` push category removed. Re-evaluate in P6.2: if `git grep -n 'identity.fxaccounts'` outside `m6e.js` and policies is empty, delete the tombstone too.
2. `DisableAccounts` and `DisableFirefoxAccounts` enterprise policies (`Policies.sys.mjs`, `policies-schema.json`, `aboutPolicies.js`): they only lock prefs to false and `browser.aboutwelcome.enabled` to false, so they are inert and remove no capability. Keeps the policy schema stable. Delete only `browser_policy_disable_fxaccounts.js`.
3. `chrome://browser/skin/fxa/avatar.svg` and `avatar-empty.svg` (and their `jar.inc.mn` lines): used by staying code that is not account UI: FedCM identity credential popup (`popup-notifications.inc.xhtml`), about:protections (`protections.html`), `toolkit/components/formautofill/ProfileAutoCompleteResult.sys.mjs` 484 (the settings footer icon) and formautofill tests. They are plain image assets, with no code or network. Optionally move them out of the `fxa/` directory in a later cleanup.
4. Vendored Rust: `third_party/application-services/components/fxa-client`, `examples/fxa-client`, the `Cargo.toml`/`Cargo.lock` entries, and the generated UniFFI names `deleteAllExceptFxa` / `wipeLocalExceptFxa` in `toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustLogins.sys.mjs` and `storage-rust.sys.mjs`. D21: vendored code is left alone. Proof: `fxa-client` is reachable only through `megazords/full|ios-rust|fenix-dylib` workspace members; `toolkit/library/rust/shared/Cargo.toml` and the uniffi-bindgen-gecko-js config do not depend on or generate bindings for it (checked with `git grep` for `fxa-client|fxa_client`). It is not compiled into M6E.
5. Password manager FxA host/realm literals (`chrome://FirefoxAccounts`, `Firefox Accounts credentials`) in `storage-json`, `LoginStore`, `LoginHelper.isUserFacingLogin`: keep, inlined, so any pre-existing FxA login record in an old profile stays hidden and not wiped by "remove all user-facing logins" semantics. No dependency on the deleted module.
6. `ASRouterTargeting` attributes `isFxAEnabled`, `isFxASignedIn`, `hasAccessedFxAPanel`, `attachedFxAOAuthClients` as constant false/empty values: needed until Nimbus (feature 9) is deleted; they contain no code path to FxA.
7. Schema enums and prose naming FxA in JSON schemas, if left (`MessagingExperiment.schema.json`, `MenuMessage.schema.json`): data-only. Preferred is to remove them, but they are not reachable without a handler.
8. `python/l10n/fluent_migrations/...` and taskcluster fixtures: historical data.

Relay (if Option B below): locked `signon.firefoxRelay.feature` = "disabled" in `m6e.js` is the unreachability proof. In that state `RelayDisabled` has no methods, `autocompleteItemsAsync` returns nothing, `generateUsername`/`offerRelayIntegration` are no-ops and `getRelayProfileInfo()` returns null on the first pref check, so no request to relay.firefox.com or accounts.firefox.com is possible.

## Risks and open questions

1. Firefox Relay. Not one of the nine features, yet it depends on FxA end to end and its default pref `signon.firefoxRelay.feature = "available"` makes it offer an email mask on sign-up forms, which opens an accounts.firefox.com page. Option A (cleaner, larger): delete Relay (`FirefoxRelay.sys.mjs`, keep `FirefoxRelayUtils.sys.mjs` only if `FormScenarios` still needs it, `firefoxRelay.ftl`, `signon.firefoxRelay.*` prefs, the `fxa-and-relay-integration-*` popup notifications, privacy pane checkbox in `preferences/config/privacy.mjs` and `privacy.js`, consumers `ContextMenuParent`, `FormAutofillParent`, `LoginManagerParent`, `FormHistoryParent`, `ASRouterTargeting` (`isRelayFreeTier` family), newtab `PrivacyFeed`, `BrowserUtils` Relay promo, `browser_relay_*` tests). Option B (recommended for this step): strip FxA calls from `FirefoxRelay.sys.mjs` and lock the feature pref disabled in `m6e.js`, then schedule Option A as its own step. Needs a user decision.
2. Overlap with Sync. `browser-sync.js`, the Sync preferences pane, Firefox View synced tabs and synced-tabs sidebar are claimed by both features. I assumed the Sync branch deletes or rewrites everything importing `resource://services-sync`. If the Sync branch instead keeps a trimmed `gSync`, the FxA parts of it become this branch's work (account panel, avatar, `EnsureFxAccountsWebChannel`, `emitFxaToolbarTelemetry`, pairing).
3. The FxA avatar panel hosts the Profiles UI (`PanelUI-fxa-menu-all-profiles`, `-create-profile`, `browser-profiles.js`, `browser_fxa_menu_profiles*.js`). Removing the panel removes that entry point; the app-menu `PanelUI-profiles` stays. Confirm there is no M6E-visible regression (the toolbar button was already hidden, `identity.fxaccounts.enabled` false).
4. Default toolbar layout: removing `fxa-toolbar-menu-button` from `CustomizableUI` default placements changes the placements array for fresh profiles; customizableui tests with explicit expected placement lists must be updated, and any profile that saved the id keeps a harmless dangling id. `browser.uiCustomization.state` version number must not change.
5. Generated artifacts checked into git: `tools/@types/generated/*` and `browser/components/aboutwelcome/content/aboutwelcome.bundle.js`, `asrouter-newtab-multistage.bundle.js` (and `toolkit/components/uniffi-bindgen-gecko-js/.../generated`, not touched). Regenerate or hand-edit; stale bundles still reference `AWGetFxAMetricsFlowURI` but use optional chaining, so they are inert until regenerated.
6. Glean: removing `services/fxaccounts/metrics.yaml` also removes `Glean.fxa.*`. The only staying writers are `TelemetryEnvironment._updateServicesInfo` and `AccountsGlue`; both are removed here. A missed `Glean.clientAssociation.*` / `GleanPings.fxAccounts*` use throws at runtime in the parent process (e.g. `StartupTelemetry.initFOG`), which would break startup. Verify `git grep -n 'clientAssociation\|fxAccountsClientInfo\|Glean\.fxa\b'` is empty before building.
7. `Extension.sys.mjs` `storageSyncOldKintoBackend` default is `true`; removing the pref without removing this entry flips behaviour for the `storage.sync` quota logic. Remove both together.
8. `services/common` hawk/tokenserver modules have no other user once FxA and Sync are gone; deleting them is a judgment call outside the nominal path. Required for `hawkrequest`/`hawkclient` (they import the deleted `Credentials.sys.mjs`); `tokenserverclient` only if Sync left it. `test_load_modules.js` mentions `tokenserverclient.sys.mjs` in a list.
9. `testing/tps`, `services/automation`, `testing/condprofile` FXA env handling are Sync-adjacent tooling that I did not classify with certainty; whichever branch lands first should delete them.
10. Untested assumption: the sweep below found no hit in `browser/installer/package-manifest.in`, `browser/confvars.sh`, `devtools/`, `security/manager`, `services/settings` allowlist, `remote/` expectations beyond RecommendedPreferences, or `taskcluster` configs. About-page registrations: no `about:accounts`; FxA has no JSWindowActor/ProcessActor and no process type.
11. Grep coverage caveat: results come from `git grep -I -E` on case-sensitive patterns plus targeted searches; identifiers built dynamically (string concatenation of `fxa`) would not match.

Sweep command to re-run after Sync is integrated (and again after this branch):

`git grep -n -I -E 'FxAccounts|fxaccounts|FxA[A-Z_]|fxa[A-Z_.-]|\bfxa\b|\bFxA\b|fxAccount|FXA_|Credentials\.sys\.mjs|AccountsGlue|clientAssociation|fxAccountsClientInfo|Glean\.fxa\b|syncSettings\.|gSync\b' -- . ':!third_party' ':!services/fxaccounts' ':!gfx' ':!dom/canvas' ':!dom/webgpu' ':!security/nss' ':!intl' ':!js' ':!layout' ':!*.svg' ':!*package-lock.json' ':!extensions/spellcheck' ':!security/manager' ':!services/settings/dumps' ':!toolkit/crashreporter/google-breakpad'`

## Critic addendum

Critic: read-only, base 4110e90366. Searches used: basenames and directory names, exported constants of `FxAccountsCommon` (notification topics, push scope `chrome://fxa-device-update`, permission `firefox-accounts`, `signedInUser.json`), contract id and category, Glean categories and ping names, lowercase `fxa[a-z]` identifiers (the report's sweep regex misses `fxastatus`, `fxasignedout`, `fxadisabled`, `fxatoolbarmenu`, `fxaccount-*` CSS classes), FTL id prefixes, `accounts.firefox.com` URL strings, RemotePageAccessManager allowlists, all manifest types, `gSync` call sites. Most of the report checked out (BrowserComponents.manifest lines, StartupTelemetry ping, jar.inc.mn range, storage-json/LoginStore/LoginHelper constants, Preferences.cpp and nsContentSecurityUtils lines, Glean category and ping names, no uniffi/gkrust dependency on fxa-client, no JSWindowActor). The items below are missing or wrong.

### A. Missed references (file, reference, action)

Breaks at runtime if left (high):

| File | Reference | Action |
|---|---|---|
| `browser/base/content/nsContextMenu.sys.mjs` ~1323 | `this.window.gSync.updateContentContextMenu(this)` called on every page context menu build; `syncItemsShown` used at ~1078-1080 for `context-sep-sendlinktodevice` | Remove the call and the `syncItemsShown` / `ensureHidden` handling. Throws `TypeError` on every content context menu once `gSync` is gone. Report lists `browser-context.js` but not this file. Sync-owned (S) but nobody else claims it |
| `browser/base/content/browser-context.inc.xhtml` 141-148, 484-491 | `context-sendlinktodevice`, `context-sep-sendlinktodevice`, `context-sep-sendpagetodevice`, `context-sendpagetodevice` menus and popups (class `sync-ui-item`) | Remove (S). Missing from report |
| `toolkit/modules/RemotePageAccessManager.sys.mjs` ~234-291 (`about:protections`) | Page-visible allowlist still names `OpenSyncPreferences`, `ClearMonitorCache`, `FetchMonitorData`, `FetchMobileDeviceConnected`, `FetchVPNSubStatus` and prefs `browser.contentblocking.report.monitor.*`, `.manage_devices.url`, `.sign_in_url`, `.vpn*`, `.show_mobile_app` | Remove the FxA/Monitor/mobile/VPN entries together with the `AboutProtectionsParent` handlers. This is an entry-point allowlist for web content, so it belongs in the "unreachable" proof. Not in report |
| `browser/components/aboutlogins/AboutLoginsParent.sys.mjs` 114, 568, 861 and `AboutLoginsChild.sys.mjs` 236 | `AboutLogins:SyncEnable` handler, `AboutLogins:SyncState` broadcast (built from `UIState`), `fxAccountsEnabled`, `passwordSyncEnabled` | Remove together with the `<fxaccounts-button>` work; report only lists the `FxAccounts` lazy, `FXA_ENABLED`, `openFxAEmailFirstPage`, `promiseManageURI` |
| `browser/components/migration/FirefoxProfileMigrator.sys.mjs` ~382-430 | `sync` resource ("Sync/FxA related data"): reads and copies `signedInUser.json`, sets `services.sync.username`. The report mentions only the marionette test | Delete the `sync` resource and its entry in the returned resource list; update `browser/components/migration/tests/marionette/test_refresh_firefox.py` (report has this) and any migrator unit test that lists the `sync` resource name |
| `toolkit/components/extensions/Extension.sys.mjs` ~598 | The consumer `if (!lazy.storageSyncOldKintoBackend)` (clearOnUninstall) | Report only covers the lazy-pref entry at 107-111. When the entry goes, also drop the condition (always run `clearOnUninstall`); `lazy.storageSyncOldKintoBackend` would otherwise silently be `undefined` |
| `browser/components/backup/tests/xpcshell/test_BackupService.js` 470-520, `head.js` 79-80 | `test_createBackup_signed_out/signed_in` stub `UIState.get` and assert `meta.accountID/accountEmail`; fixtures contain `accountID`/`accountEmail` | Delete or rewrite both tasks. Note `BackupManifest.{1,2}.schema.json` list `machineName` as required, so the replacement for `fxAccounts.device.getLocalName()` must always yield a non-empty string; `accountID`/`accountEmail` are optional in the schema, leave the schema (versioned, archived backups use it) |
| `browser/components/asrouter/content-src/components/MultiStageAboutWelcome.jsx` 694, 841-858 | `SHOW_FIREFOX_ACCOUNTS` and `FXA_SIGNIN_FLOW` branches (source of `asrouter-newtab-multistage.bundle.js` lines 6566-6696, and `aboutwelcome.bundle.js` 763-893) | Edit the source and regenerate/hand-edit both bundles. Report names only `aboutwelcome.jsx` as source and mentions the bundles as inert |

Inert but should go (medium/low):

| File | Reference | Action |
|---|---|---|
| `browser/app/profile/firefox.js` 2654 | `browser.contentblocking.report.manage_devices.url` = accounts.firefox.com/settings/clients | Remove pref (also from RemotePageAccessManager above). Report prefs list omits it |
| `browser/app/profile/firefox.js` 3474 | `svg.context-properties.content.allowed-domains` contains `profile.accounts.firefox.com,profile.stage.mozaws.net` | Reduce the value to what remains needed (likely empty); grep other users first |
| `modules/libpref/init/all.js` 1898 | `browser.tabs.remote.separatedMozillaDomains` includes `accounts.firefox.com` | Remove the host (optional, process isolation list); also `dom/docs/ipc/process_model.md` 176 |
| `modules/libpref/init/all.js` 3108 | `extensions.webextensions.restrictedDomains` includes `accounts.firefox.com`, `accounts-static.cdn.mozilla.net` | Leave (restricting extensions from a domain is harmless and safer) |
| `browser/components/aboutlogins/content/aboutLogins.html` line 9 | CSP `img-src ... https://profile.accounts.firefox.com`; also elements/classes `fxaccount-manage-link`, `fxaccount-email`, `fxaccount-avatar` at 170-173 | Remove the CSP host and the elements. Removes the only network image source allowed on about:logins |
| `browser/components/asrouter/modules/ASRouterTriggerListeners.sys.mjs` ~945 | Skips "saved login" trigger when host is `accounts.firefox.com` | Remove the check (comment mentions FxA) |
| `browser/components/taskbartabs/TaskbarTabsChrome.sys.mjs` 32-33 | Sets `fxadisabled` attribute; consumed only by `panelUI-shared.css` (`:root[fxadisabled]`) and tests `browser_taskbarTabs_chromeTest.js` 55 | Remove both the setter and the test assertion with the CSS |
| `browser/themes/shared/customizableui/panelUI-shared.css` | Report names ids but not the root attributes `fxastatus`, `fxasignedout`, `fxadisabled`, `fxa-sign-in-promo-dismissed`, `fxatoolbarmenu`, `fxa-avatar-icon-variant`, ids `PanelUI-fxa-menu-*` (141 matches of `fxa`) | Delete all of them; `browser.js` 862-876 (`fxastatus`, `fxatoolbarmenu` setAttribute) and `browser-sync.js` set them |
| `browser/base/content/appmenu-viewcache.inc.xhtml` 284, 358 | `<html:sync-promo id="PanelUI-history-sync-promo">`, `PanelUI-bookmarks-sync-promo` | Remove (S); `browser/components/customizableui/content/jar.mn` line 7 (`sync-promo.mjs`) and `browser.js` 652-654 creation callback go with them. Jar line not in report |
| `browser/base/content/browser-places.js` 724, 887, 2058 | `gSync.getSyncPromoState`, `handleSyncPromoAction` | Remove (S). Report lists the file generically; these are the exact sites |
| `browser/base/content/browser-profiles.js` | 21 hits of `PanelUI-fxa-menu-*` ids; `fxaPanelView = getViewNode("PanelUI-fxa")` at 55 | Matches the report's row, but note the ids are used for the live Profiles UI in the avatar panel only; app-menu Profiles uses other ids. Verify `PanelUI-profiles` code path still works after cut |
| `browser/components/preferences/jar.mn` 32 and `preferences.js` 476-488, 619 | `config/account-sync.mjs` entry and the `account-sync-section` registration / `pane-account-sync-title2` | Already in report as (S); also remove FTL `account-sync-section` and `pane-account-sync-title2` from `preferences.ftl` |
| `browser/extensions/newtab/common/Actions.mjs` 172, 177 | Action type names `SHOW_FIREFOX_ACCOUNTS`, `SKIPPED_SIGNIN` | Remove the names (inert constants); check `browser/extensions/newtab` tests that enumerate Actions |
| `browser/components/asrouter/docs/messaging-surfaces.md` 67 | `AWGetFxAMetricsFlowURI` row | Edit doc |
| `browser/components/asrouter/docs/menu-message.md`, `targeting-attributes.md` 789-914 | `fxa_cta`, `isFxAEnabled`, `isFxASignedIn`, `hasAccessedFxAPanel` | Report lists docs; targeting-attributes entries must stay consistent with the constant-false decision |
| `toolkit/components/nimbus/test/unit/reference_aboutwelcome_experiment_content.json` 33 | `SHOW_FIREFOX_ACCOUNTS` action in a reference fixture | Leave (data fixture, Nimbus is feature 9) |
| `tools/m6e/branding/fox-art.txt` 88-89 | `browser/themes/shared/fxa/avatar-fox-circle.svg` and `avatar-fox.svg` | Remove both lines (report only lists line 8). `replace-fox-art.sh` iterates this list; a missing file will make it fail |
| `testing/tps/mach_commands.py` 47 | `"webextensions.storage.sync.kinto": False` | Remove with the pref (or with the TPS tool if Sync deletes it) |
| `toolkit/components/satchel/megalist/aggregator/datasources/LoginDataSource.sys.mjs` 20, 513 | Imports `UIState` from services-sync | Sync-owned import; flag for the Sync branch, FxA branch should confirm it is gone |
| `toolkit/components/passwordmgr/LoginManager.sys.mjs` 371-388, `nsILoginManager.idl` 122-133 | Comments about "FxA Sync key" in `removeAllUserFacingLogins/removeAllLogins` | Comment text only; IDL edit would need a C++ rebuild, so leave |
| `browser/components/sharing/content/content-sharing-modal.mjs` 183-192 | Sign-in link `CONTENT_SHARING_SERVER_URL + "/accounts/fxa/login/"` (Mozilla-account login on the share server) | Not an FxAccounts-module consumer, so it does not break. But `browser.contentsharing.enabled` (firefox.js 3692) is `false` and is NOT locked in `m6e.js`. Either lock it or list it as an out-of-scope web-login surface. Related: `browser/components/sharing/tests/browser/browser_testReuseAuthCompleteTab.js` |
| Relay promo surfaces | `browser.promo.relay.enabled` (firefox.js 2644) is `true`; consumers `moreFromMozilla.js`/`config/moreFromMozilla.mjs`, `AboutPrivateBrowsingParent`, `ASRouter` 2482, newtab `PrivacyMessages.sys.mjs` 388 (`newtab-privacy-message-promo-relay-1`) | Not covered by report's Relay option B, which locks only `signon.firefoxRelay.feature`. If Relay is "disabled", these still advertise relay.firefox.com. Include in the Relay decision |

FTL id prefixes the report's list does not cover (it has `appmenu-fxa-*`, `fxaccounts-*`, `menu-tools-fxa-*`, `fxa-menu-message-*`, `sync-account-*`, `account-*`):
- `appmenu.ftl`: `appmenu-nova-fxa-sign-in`, `appmenuitem-fxa-*`, `appmenuitem-sign-in-account`, `appmenu-account-header`, and the large `fxa-menu-*` family (sync-status, add-device, manage-devices, device-recent-tabs-panel, create-profile-*, etc.; lines 134-184 and 455-496). Only `fxa-menu-message-*` is in the report, which is a different family
- `browserContext.ftl`: `toolbar-button-account`
- `featureCallout.ftl`: `fxa-adoption-passwords-*`
- `aiWindow.ftl`: `fxa-signout-dialog-body-aiwindow` (earlier feature, ignore)
- `browser/locales/en-US/browser/accounts.ftl` is also imported by `browser-sync.js:1227` and `AccountsGlue`; no other user once both are gone

### B. Wrong or incomplete claims in the report

1. "Translations, ML: no references found" is wrong. Translations: `toolkit/components/translations/tests/browser/shared-head.js` 4518 sets `identity.fxaccounts.account.device.name`. ML: `toolkit/components/ml/tests/browser/browser_ml_openai.js` (`test_openai_fxaccount_token`, FxA token header), `toolkit/components/ml/eval/evals.py` and `mach_commands.py` (`MOZ_FXA_BEARER_TOKEN`). All are inside code the earlier features delete, so no action for this branch, but list them under "from earlier features" so the integrator does not hunt for them.
2. The sweep command's regex misses lowercase compounds (`fxastatus`, `fxasignedout`, `fxadisabled`, `fxatoolbarmenu`, `fxaccount-*`). Add `fxa[a-z]` to the post-merge sweep: `git grep -n -I -E 'fxa[a-z]' -- browser toolkit`. It finds the CSS attribute selectors, `TaskbarTabsChrome`, `browser.js` 862-876 and `aboutLogins.html`.
3. Risk 10 says the sweep found no hit in `remote/` beyond RecommendedPreferences, `services/settings`, `security/manager`. True for code, but `security/manager/ssl/StaticHPKPins.h` 284, 290 and `security/manager/tools/PreloadedHPKPins.json` pin `accounts.firefox.com` and `api.accounts.firefox.com`. These are generated and vendored-style data (D21): leave. Mention it so nobody removes them by hand.
4. "Locked `identity.fxaccounts.enabled=false` ... several staying readers use `getBoolPref` without default". Confirmed readers that stay and need the tombstone: `browser/components/profiles/tests/browser/browser_preferences.js` 26 and `enterprisepolicies/tests/browser/browser_policy_disable_profile_import.js` 159 (both read it with no default), plus `browser-sidebar.js` 193-196 (S). Add these two tests to the "Tests to edit" list; they pass only because of the tombstone.
5. Section "Files to delete" says `ExtensionStorageSyncKinto.sys.mjs` consumers are `ext-storage.js` and the Sync engine. Also `testing/tps/mach_commands.py` 47 and `tools/lint/extra-js-modules/allowlist.txt` 357 (the latter is covered by the lint line item). `toolkit/components/extensions/test/xpcshell/xpcshell-common.toml` lines 674, 742, 750 confirmed for the three kinto tests.
6. "Hard breakage ... `getBoolPref` with no default on a deleted pref": `ext-storage.js` 20 reads `webextensions.storage.sync.kinto` with no default; correct in report, but note that removing the pref and the `ext-storage.js` branch must land in the same commit.
7. Option B for Relay: "`getRelayProfileInfo()` returns null on the first pref check" was not verified here; `FirefoxRelay.sys.mjs` also has `offerRelayIntegrationToFxAUser` (924) and a sign-up tab path (861-870) that call `lazy.fxAccounts.getSignedInUser()` (753, 818). Delete the `fxAccounts` lazy only after those call sites are removed or the file fails at first use. The pref is `signon.firefoxRelay.feature = "available"` in firefox.js 2846 and the `all.js` 2991-2995 URL prefs; none is locked in `m6e.js` today.
8. `UIState`-based account metadata in `BackupService.sys.mjs` (3155-3158) imports `resource://services-sync/UIState.sys.mjs`, which the earlier Sync branch deletes. That import and call belong to the Sync branch under the ordering rule; the FxA branch keeps only the `fxAccounts.device.getLocalName()` part. Same for `LoginDataSource.sys.mjs`.

### C. Verified "must stay" and "earlier feature" claims

- Must stay 3 (`avatar.svg`, `avatar-empty.svg`): confirmed users `popup-notifications.inc.xhtml` 252, `protections.html` 406, `ProfileAutoCompleteResult.sys.mjs` 484 (uses `avatar-empty.svg`, so `jar.inc.mn` 204 and 206 must stay). Note `appmenu-viewcache.inc.xhtml` 48/667 and `preferences.js` 477/620 also use `avatar-empty.svg` but go away. Also `avatar-color.svg` is used only by deleted code (`preferences.css` 652, `account-sync.mjs`, `fxaccounts-button.*`).
- Must stay 4 (vendored Rust): confirmed no `fxa` in `toolkit/library`, `toolkit/components/uniffi-bindgen-gecko-js/config.toml`; the only hit is the `COMPONENT_MAPPING` entry `mozilla/appservices/fxaclient` in `uniffi-bindgen-gecko-js/mach_commands.py` 133 (a mapping table for the bindgen tool, not a build input). Leave; mention it in the Rust section.
- Must stay 5 (password manager literals): confirmed in `storage-json` (50-51, 284, 435, 693-694), `LoginStore` (48-49, 115-116), `LoginHelper` 1830.
- Earlier-feature claims (VPN `ipprotection/fxa/*`, `browser/components/ipprotection/IPProtection.sys.mjs`, GenAI `aiwindow`, Sync `services/sync/**`) are consistent with the greps. `services/sync/tps/.../auth/fxaccounts.sys.mjs` and `services/automation` are under Sync's directories or Sync-only tooling.
- `services/common` hawk/tokenserver: no other importer found outside `services/fxaccounts`, `services/sync`, `services/common/tests`. `services/common/tests/unit/test_load_modules.js` mentions only `tokenserverclient.sys.mjs` (line 11, 38), not hawk modules. `services/common/moz.build` lines 24-28 need the three entries removed and `tools/lint/extra-js-modules/allowlist.txt` 192, 193, 199.

### D. Build-system / config gaps

- `browser/components/customizableui/content/jar.mn` line 7 (`sync-promo.mjs`) and `browser/components/preferences/jar.mn` line 32 (`config/account-sync.mjs`) were not in the "Build-system edits" list (the preferences one is only implied).
- `browser/themes/shared/jar.inc.mn`: in addition to 191-213 and 178, `browser/themes/shared/syncedtabs/sidebar.css` (uses `fxa/sync-illustration*.svg`) must go or lose those urls; if the Sync branch leaves it, the jar line removal would break the CSS reference silently (CSS 404, not a build error).
- The ~400-file sweep over non-vendored trees shows no `docs/` Sphinx toctree entry for `services/fxaccounts` (no docs dir), and `taskcluster/config.yml` 374/383 `*-firefox-accounts` are Android component names, no action.
- No hit for FxA in `devtools/`, `services/settings` (non-dump), `browser/installer/*`, `browser/confvars.sh`, `moz.configure`: report's Risk 10 confirmed.
