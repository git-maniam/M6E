# P6.1a analysis: VPN (IP protection) and the addons-search-detection built-in extension

Base commit cb002d4874. Integration order 1, so there are no "earlier features". Read-only analysis; nothing was built or run.
Line numbers are as of the base commit and are only there to help find the spot.

## Summary

- Four directories go: `browser/components/ipprotection` (94 files), `toolkit/components/ipprotection` (58), `browser/extensions/ipp-activator` (31) and `browser/extensions/search-detection` (13). Plus about 20 loose files (FTL, preferences dialog, three test files, ten Fluent migration scripts).
- The VPN is already unreachable at runtime. P1.9 (`f49f2658bb`) removed the two `BrowserComponents.manifest` lines that loaded `IPProtectionHelpers.sys.mjs` (`IPProtectionActivator.init` at first window, `.uninit` at quit), and `m6e.js` locks `browser.ipProtection.enabled` false. The only remaining entry points into the feature modules are lazy imports in `preferences/privacy.js`, `preferences/config/privacy.mjs` and `SpecialMessageActions.sys.mjs`, plus the two built-in extensions that still start with the browser.
- No C++, Rust, IPDL, XPIDL, `components.conf`, `StaticPrefList.yaml`, `moz.configure` or Cargo change is needed. The feature is pure JS/CSS/SVG/FTL/Glean YAML. The netwerk MASQUE / CONNECT-UDP code, which IPPChannelFilter used, is general proxy infrastructure and stays (see Must stay).
- Staying code that must be edited: 3 `moz.build` DIRS lines, `preferences` (privacy.js, privacy.mjs, preferences.js, preferences.xhtml, privacy.inc.xhtml, LegacyPaneMappings.mjs, permissions.js/xhtml, dialogs/jar.mn, preferences.css), browser chrome (browser.xhtml, browser.js, appmenu-viewcache, navigator-toolbox.js, CustomizableUI), 3 theme CSS files, `firefox.js` + `m6e.js` prefs, messaging-system (SpecialMessageActions, blocklist, two schema JSON+md pairs), `FeatureCalloutMessages`, enterprise policy, `ProfileDataUpgrader`, Nimbus `FeatureManifest.yaml` (a later feature but references this one), Glean `metrics_index.py`, `taskcluster` build-extensions, lint configs for deleted paths, `mots.yaml`, `toolkit/docs/index.md`, about:logging preset, test profiles, and `tools/m6e`.
- Build type: front-end (`./mach build faster`) is enough for the JS/CSS/jar changes, but the Glean metrics registry (generated from `metrics.yaml` files, compiled into libxul) changes, so run a full `./mach build` once for the P6.1a commit. No configure rerun.

## Files to delete (directories and loose files)

Directories (`git rm -r`):

| Path | Files | Notes |
|---|---|---|
| `browser/components/ipprotection` | 94 | UI: toolbar button, panel, alert/infobar managers, bandwidth/status/locations/site-rules custom elements, assets (SVG), `jar.mn`, `metrics.yaml`, tests (browser + xpcshell) |
| `toolkit/components/ipprotection` | 58 | Service, proxy manager, channel filter (nsIProtocolProxyChannelFilter, MASQUE), Guardian client, auth providers (fxa, enterprise, gpi, android), site rules, `metrics.yaml`, `docs/` (Sphinx tree `/toolkit/ipprotection`), tests, `tests/IPPDummyAuthProvider.sys.mjs` (exposed as `resource://testing-common/ipprotection/`) |
| `browser/extensions/ipp-activator` | 31 | Built-in extension `ipp-activator@mozilla.com`, `xpi-build/` (XPI_NAME `ipp-activator`), tests |
| `browser/extensions/search-detection` | 13 | Built-in extension `addons-search-detection@mozilla.com` (hidden, telemetry only), `metrics.yaml` (category `addons.search_detection`), tests |

Loose files (`git rm`):

- `browser/locales/en-US/browser/ipProtection.ftl` (all `ipprotection-*`, `ip-protection-*`, `ipp-activator-breakage-*`, `upgrade-vpn-button`, `confirmation-hint-ipprotection-*` messages are defined here; checked no other FTL defines them).
- `browser/components/preferences/dialogs/vpnSiteRule.xhtml`, `vpnSiteRule.js`, `vpnSiteRule.css`.
- `browser/components/preferences/tests/privacy/browser_privacy_ipprotection.js`, `browser_privacy_ipprotection_siterules.js`.
- `toolkit/components/messaging-system/schemas/SpecialMessageActionSchemas/test/browser/browser_sma_ipprotection_enroll.js`.
- `browser/components/tests/unit/test_browserGlue_migration_ippL10nCoverage.js`.
- Ten Fluent migration recipes whose source is `browser/browser/ipProtection.ftl`, in `python/l10n/fluent_migrations/`: `bug_2022165_ip_protection_site_exclusion_toggle.py`, `bug_2039080_ip_protection_status_aria_label.py`, `bug_2039896_ip_protection_remove_button_label.py`, `bug_2040414_ip_protection_no_thanks_button_label.py`, `bug_2040965_ip_protection_not_now_button_label.py`, `bug_2048035_ip_protection_get_started_title.py`, `bug_2054136_ip_protection_summer_promo_offramp_buttons.py`, `bug_2060236_ip_protection_android_promo_callout_button.py`, `bug_2074289_ipprotection_site_inclusions_callout_buttons.py`, `bug_2082660_ipprotection_holiday_promo_buttons.py`.

Not deleted on purpose: `browser/base/content/logos/vpn.svg`, `vpn-promo-logo.svg`, `preferences/vpn-logo.svg`, `privatebrowsing/content/assets/vpn-logo.svg`, `moz-vpn.svg` (Mozilla VPN paid-product promo art, see Risks).

## References from staying code

"Action" is what to do in the P6.1a branch. Paths relative to repo root.

### Build system and manifests

| File | What | Action |
|---|---|---|
| `browser/components/moz.build` | `"ipprotection"` in DIRS | remove the line |
| `toolkit/components/moz.build` | `if MOZ_BUILD_APP != "comm/mail": DIRS += ["ipprotection"]` | remove the 2-line block |
| `browser/extensions/moz.build` | `"ipp-activator"`, `"search-detection"` in DIRS | remove both lines |
| `browser/components/preferences/dialogs/jar.mn` | 3 lines for `vpnSiteRule.{xhtml,js,css}` | remove |
| `toolkit/components/glean/metrics_index.py` | `toolkit/components/ipprotection/metrics.yaml`, `browser/components/ipprotection/metrics.yaml`, `browser/extensions/search-detection/metrics.yaml` | remove the 3 entries (full build needed, see Build-system edits) |
| `taskcluster/kinds/build-extensions/kind.yml` | artifact `public/browser-extensions/ipp-activator.xpi` | remove the 3-line artifact entry |
| `toolkit/docs/index.md` | `ipprotection/index` toctree entry | remove |
| `mots.yaml` | module "IP Protection" (includes both ipprotection dirs) | remove the module block |
| `eslint-rollouts.config.mjs` | `toolkit/components/ipprotection/IPPSessionPrefManager.sys.mjs`; `browser/extensions/search-detection/extension/background.js` | remove both entries |
| `eslint-subdirs.config.mjs` | `convertConfigurationFile("browser/extensions/search-detection/tests/browser")` | remove (the `.eslintrc.mjs` in that dir goes with it) |
| `stylelint-rollouts.config.js` | `browser/components/ipprotection/content/ipprotection-content.css` | remove |
| `tools/@types/generated/lib.gecko.modules.d.ts`, `tspaths.json`, `lib.gecko.glean.d.ts` (ipprotection, addonsSearchDetection), `tools/@types/subs/FeatureManifest.sys.d.mts` | generated TS types listing the deleted modules and Glean categories | remove the entries by hand or regenerate with the generator (`./mach ts`-style task); cosmetic, but a lint may compare |
| `taskcluster/test/params/mc-nightly-all.yml` (14 lines, ~6138-6147 and ~7329-7332) | test-selection params naming ipprotection files (already stale: `IPPEnrollAndEntitleManager.sys.mjs` does not exist) | optional; remove the 14 lines or leave to P6.2 |
| `taskcluster/config.yml` | `'feature-ipprotection'` (Android component name, no such dir in tree) | leave |
| `tools/lint/extra-js-modules/allowlist.txt` | `mobile/shared/modules/geckoview/GeckoViewIPProtection.sys.mjs` (no `mobile/` in tree) | leave (stale already; P6.2) |

### Preferences (about:preferences)

| File | What | Action |
|---|---|---|
| `browser/components/preferences/privacy.js` | lazy getters `IPProtection` and `BANDWIDTH` (~78-80); `initSettingGroup("ipprotection")` (~625) | remove the 3 lines and the call |
| `browser/components/preferences/config/privacy.mjs` | lazy getters `IPProtection`, `IPPPermissionRules`, `IPPPrincipalRules`, `BANDWIDTH` (33-39); pref list "Firefox VPN" (413-423); `ipprotection` group (1538-1637) and `vpnSiteRules` group (1638-1647); all `ipProtection*` `Preferences.addSetting` blocks and helper functions (2138-~2575, from `ipProtectionVisible` to `ipProtectionLinks`); `Glean.ipprotection.rulesPageOpened` | remove all of it. Check nothing else in the file uses `Intl.Collator ippSiteRuleCollator`/`IPP_VPN_PERMISSION`. Keep `gpcEnabled` etc. that follow |
| `browser/components/preferences/privacy.inc.xhtml` | comment + `<html:setting-group groupid="ipprotection" ... data-subcategory="vpn">` (400-401) | remove |
| `browser/components/preferences/preferences.js` | `"ipprotection"` in `privacy.groupIds` (~452); whole `vpnSiteRules` pane entry (~525-530) | remove both |
| `browser/components/preferences/preferences.xhtml` | stylesheet `vpn-site-rules-list.css` (41), `<link rel="localization" href="browser/ipProtection.ftl"/>` (56), module scripts `bandwidth-usage.mjs`, `vpn-site-rules-list.mjs` (104-105) | remove the 4 lines |
| `browser/components/preferences/config/LegacyPaneMappings.mjs` | `privacy-vpn`, `privacy-vpnsiterules` (16-17) | remove |
| `browser/components/preferences/dialogs/permissions.js` | `"ip-protection-exceptions-dialog-window"` / `"ip-protection-exclusions-desc"` entry for `"ipp-vpn"` (55-58) | remove the entry. Leave the generic `addVisible` / `btnAdd` plumbing (small diff, avoids touching upstream code; update the JSDoc/comment that says "Only for ipp-vpn" only if you remove it) |
| `browser/components/preferences/dialogs/permissions.xhtml` | `<html:link rel="localization" href="browser/ipProtection.ftl" />` (39) | remove |
| `browser/themes/shared/preferences/preferences.css` | `.dialogBox.vpnSiteRuleDialogBox` rule (~587) | remove. Keep `#mozilla-vpn` (~1181, More from Mozilla promo) |

### Browser chrome

| File | What | Action |
|---|---|---|
| `browser/base/content/browser.xhtml` | `<link rel="localization" href="browser/ipProtection.ftl"/>` (71) | remove |
| `browser/base/content/browser.js` | `showConfirmationHint` branch inserting `browser/ipProtection.ftl` for `confirmation-hint-ipprotection-navigated-to-{excluded,included}-site` (~5027-5033, with its comment) | remove the block |
| `browser/base/content/appmenu-viewcache.inc.xhtml` | `<panelview id="PanelUI-ipprotection">` and `<panelview id="PanelUI-ipprotection-locations">` (1058-1098) | remove both panelviews |
| `browser/base/content/navigator-toolbox.js` | `#ipprotection-button` in the click selector (364) and `case "ipprotection-button"` (460-462) | remove both |
| `browser/components/customizableui/CustomizableUI.sys.mjs` | lazy pref getter `ippEnabled` on `browser.ipProtection.enabled` (273-292) and `lazy.ippEnabled ? "ipprotection-button" : null` in the default navbar placements (375) | remove both |
| `browser/themes/shared/toolbarbutton-icons.css` | `#ipprotection-button` block and state classes (508-~615) | remove the whole block |
| `browser/themes/shared/toolbarbuttons.css` | `/* IP Protection */ #ipprotection-button {...}` (~697-end of that rule) | remove |
| `browser/themes/shared/customizableui/panelUI-shared.css` | `/* ----- IP Protection panel ----- */` through `#PanelUI-ipprotection-locations-header h1` (~3048-3098) | remove. Keep `#PanelUI-fxa-menu-vpn-button` (~1328, FxA menu VPN promo) |
| `browser/base/content/test/browser-static/browser_all_files_referenced.js` | allowlist entry + comment for `resource://builtin-addons/ipp-activator/breakages/tab.json` (253-254) | remove |
| `browser/base/content/test/browser-performance/browser_startup_syncIPC.js` | `PGPU::Msg_AddLayerTreeIdMapping` entry with comment "Added for the search-detection built-in add-on" (~328-334) | remove, then run the test; it is `ignoreIfUnused: true`, so leaving it is also safe if the removal turns out to matter |

### Prefs, policy, migrations

| File | What | Action |
|---|---|---|
| `browser/app/profile/firefox.js` | `pref("browser.ipProtection.*")` block with its comments (lines ~3608-3665, from `blockIPProtectionCallouts` to `bandwidthWarningDismissedThreshold`, including the `#ifdef NIGHTLY_BUILD hasSeenFeature` pair) | remove (all 28 prefs) |
| `browser/app/profile/m6e.js` | the VPN comment + `browser.ipProtection.enabled` and `blockIPProtectionCallouts` locked (23-25) | remove (nothing reads them any more) |
| `browser/components/enterprisepolicies/Policies.sys.mjs` | `IPProtectionAvailable` policy (~2451-2460) | remove |
| `browser/components/enterprisepolicies/schemas/policies-schema.json` | `IPProtectionAvailable` property (~2731-2742) | remove |
| `browser/locales/en-US/browser/policies/policies-descriptions.ftl` | `policy-IPProtectionAvailable` (178) | remove |
| `browser/components/ProfileDataUpgrader.sys.mjs` | `existingDataVersion < 180` block (hasSeenFeature, 1081-1090) and `< 184` block (clears `browser.ipProtection.locationListCache`, 1144-1146) | remove both blocks; do not renumber or lower the migration version |
| `browser/components/tests/unit/test_browserGlue_migration_remove_pref.js` | `browser_ipProtection_locationListCache` task and the cleanup line for that pref | remove them; keep `browser_fixup_alternate_enabled` |
| `browser/components/tests/unit/xpcshell.toml` | `["test_browserGlue_migration_ippL10nCoverage.js"]` (13) | remove |
| `testing/profiles/unittest-required/user.js` (14), `testing/profiles/perf/user.js` (30) | `browser.ipProtection.guardian.endpoint` dummy | remove the line in each |
| `netwerk/test/browser/head_local_network_access.js` | `["browser.ipProtection.enabled", false]` in pushPrefEnv (236) | remove the line |

### Messaging system / ASRouter

| File | What | Action |
|---|---|---|
| `toolkit/components/messaging-system/lib/SpecialMessageActions.sys.mjs` | lazy `IPProtection` (51-53); 4 `browser.ipProtection.*` entries in `setPref` `allowedPrefs` (~418-421); `case "IPPROTECTION_ENROLL"` (1234-1237) | remove all three |
| `toolkit/components/messaging-system/lib/MessagingSystemBlocklists.sys.mjs` | `"IPPROTECTION_ENROLL"` in the action denylist (66) | remove (`test_MessagingSystemBlocklists.js` does not name it; check it passes) |
| `toolkit/components/messaging-system/schemas/SpecialMessageActionSchemas/SpecialMessageActionSchemas.json` | `IPPROTECTION_ENROLL` object (~1105-1114) | remove |
| `.../SpecialMessageActionSchemas/index.md` | `### IPPROTECTION_ENROLL` section (688-693) | remove |
| `.../SpecialMessageActionSchemas/test/browser/browser.toml` | `["browser_sma_ipprotection_enroll.js"]` (47) | remove |
| `toolkit/components/messaging-system/schemas/TriggerActionSchemas/TriggerActionSchemas.json` | three trigger ids `ipProtectionReady`, `ipProtectionPanelClosed`, `ipProtectionBandwidthReset` (~573-606) | remove the 3 objects |
| `.../TriggerActionSchemas/index.md` | TOC lines 68-70 and sections 603-640 for the same three triggers | remove |
| `browser/components/asrouter/modules/FeatureCalloutMessages.sys.mjs` | three messages `IP_PROTECTION_CALLOUT_MOZILLA_VPN_UPGRADE`, `..._UPGRADE_SECONDARY`, `IP_PROTECTION_BANDWIDTH_RESET_CALLOUT` (lines 85-359, inside `MESSAGES()`); they use `#ipprotection-button`, `chrome://browser/content/ipprotection/assets/*`, deleted FTL ids | remove the three array entries |
| `browser/components/asrouter/tests/browser/browser_trigger_listeners.js` | lazy imports `IPProtection`, `IPPProxyManager`, `ProxyUsage` (19-25); tasks `test_ipprotection_ready` (~1153), `test_ipprotection_panel_closed` (~1249), `test_ipprotection_bandwidth_reset` (~1292) | remove the imports and the 3 tasks |
| `toolkit/components/nimbus/FeatureManifest.yaml` | `ipProtection:` feature (5930-5955; sets `browser.ipProtection.enabled`, `.bandwidth.maxInGb`, `.upgradeNotAvailable`) | remove. This belongs to a later feature (Nimbus, deleted in P6.1i) but references this one, so handle it here. IPPNimbusHelper (in the feature) is the only consumer |

### Other

| File | What | Action |
|---|---|---|
| `toolkit/content/aboutLogging/aboutLogging.mjs` | preset `vpn` with IPP_* modules and `javascriptTracing: true` (193-204) | remove the preset |
| `toolkit/locales/en-US/toolkit/about/aboutLogging.ftl` | `about-logging-preset-vpn-label`, `-description` (62-63) | remove |
| `toolkit/content/tests/browser/browser_about_logging.js` | tasks `testPresetChecksJavascriptTracing` (~462) and `testPersistedPresetChecksJavascriptTracing` (~516) use the `vpn` preset, which is the only preset with `javascriptTracing` | remove both tasks. Low priority: if you want to keep upstream test coverage of the mechanism, skip this row and leave the preset (inert, see Must stay) |
| `browser/components/preferences/tests/privacy/browser.toml` | entries for the two deleted tests (36, 38) | remove |
| `browser/components/preferences/tests/browser_legacy_pane_mappings.js` | rows `privacy-vpn` and `privacy-vpnsiterules` (223-224), comment mentioning ipprotection/vpn (199) | remove the rows, adjust the comment |
| `browser/components/preferences/tests/permissions/browser_capability_filter.js` | uses `ipp-vpn` as the example permission type and clears `browser.ipProtection.onboardingMessageMask` (8-9, 185-225) | switch both tasks to another type that has a `permissionExceptionsL10n` entry (for example `install` or `popup`), drop `ONBOARDING_MESSAGE_MASK_PREF` and the two clear lines. Needs a test run |
| `tools/m6e/acceptance.py` | `"VPN": ["ipprotection-button"]` (129) is still valid; `REMOVED_PREFS["VPN"]` (137) requires the pref to exist and be locked false and will fail once the pref is gone; `"ipprotection"` in `REMOVED_SETTINGS_GROUPS` (219) still valid | remove the `REMOVED_PREFS["VPN"]` entry; keep the other two |
| `tools/m6e/branding/fox-art.txt` | line `browser/components/ipprotection/assets/vpn-settings-get-started.svg` (52) | remove (the replace script would fail on a missing path) |
| `tools/m6e/merge/nightly-prefs-reviewed.txt` | `browser.ipProtection.hasSeenFeature  # the VPN is locked off` (48) | remove |
| `M6E_IMPLEMENTATION_PLAN.md`, `m6e_browser_agent_instructions.md` | progress / D30 text | update when the step is committed (plan housekeeping, not part of the code change) |

## References from earlier features (ignore)

None. VPN is integration order 1.

References from LATER features that need handling in this branch: only `toolkit/components/nimbus/FeatureManifest.yaml` (see table). Checked and clean: `services/fxaccounts` and `services/sync` do not name the feature (only the feature's own `fxa/` helpers use FxAccounts); `browser/base/content/browser-sync.js` and `services/fxaccounts` carry the Mozilla VPN promo (VPN_OAUTH_CLIENT_ID, "PanelUI-fxa-menu-vpn-button"), which is a separate product promo and does not import any ipprotection module; no hits in translations, genai/aiwindow, ml, updater or crashreporter code.

## Prefs / metrics / strings to remove

Prefs (all `browser.ipProtection.*`): `blockIPProtectionCallouts`, `enabled`, `optedOut`, `features.autoStart`, `autoStartEnabled`, `autoStartPrivateEnabled`, `autoRestoreEnabled`, `userEnabled`, `userEnableCount`, `everOpenedPanel`, `openedPanelSince160`, `hasSeenFeature`, `l10nGateVersion`, `features.siteExceptions`, `features.siteInclusions`, `siteInclusionPromptShown`, `log`, `guardian.endpoint`, `added`, `hasUpgraded`, `upgradeNotAvailable`, `bandwidth.enabled`, `bandwidth.maxInGb`, `egressLocation`, `bandwidthThreshold`, `bandwidthWarningDismissedThreshold`. Prefs with no default (written at runtime, disappear with the code): `entitlementCache`, `usageCache`, `locationListCache`, `onboardingMessageMask`, `bandwidthResetDate`. Permission type `ipp-vpn` (see Must stay). No `StaticPrefList.yaml` entries. `browser.migration.version` stays.

Glean metrics (YAML deleted with the directories, `metrics_index.py` entries removed): category `ipprotection` (`toolkit/components/ipprotection/metrics.yaml`, 251 lines, also `ipprotection.inclusions`, `ipprotection.exclusions`; `browser/components/ipprotection/metrics.yaml`, 222 lines) and `addons.search_detection` (`browser/extensions/search-detection/metrics.yaml`, 89 lines). No pings, tags or Telemetry Events/Scalars/Histograms are defined for this feature. The only code outside the feature that records `Glean.ipprotection` is `preferences/config/privacy.mjs` (removed above). Nothing outside the extension records `addonsSearchDetection`.

Strings: all of `browser/locales/en-US/browser/ipProtection.ftl`, `policy-IPProtectionAvailable`, `about-logging-preset-vpn-*`. No `l10n.toml`/`jar.mn` change: the en-US FTL is picked up by directory. Strings that stay because they belong to the Mozilla VPN promo, not to this feature: `appmenuitem-vpn-*` (appmenu.ftl), `more-from-moz-*vpn*`, protections.ftl and newtab VPN strings.

Other identifiers removed: widget id `ipprotection-button`, panel ids `PanelUI-ipprotection`, `PanelUI-ipprotection-locations`, settings group ids `ipprotection` / `vpnSiteRules`, SMA action `IPPROTECTION_ENROLL`, triggers `ipProtectionReady|PanelClosed|BandwidthReset`, Nimbus feature `ipProtection`, policy `IPProtectionAvailable`, extension ids `ipp-activator@mozilla.com` and `addons-search-detection@mozilla.com`, experiment APIs `ippActivator` and `addonsSearchDetection`, log modules `IPP_*`, resource roots `chrome://browser/content/ipprotection/`, `resource://builtin-addons/ipp-activator/`, `resource://builtin-addons/search-detection/`, `resource://testing-common/ipprotection/`, `moz-src:///browser/components/ipprotection/`, `moz-src:///toolkit/components/ipprotection/`.

## Build-system edits

| Edit | Kind |
|---|---|
| `browser/components/moz.build`, `toolkit/components/moz.build`, `browser/extensions/moz.build` DIRS; `jar.mn` in preferences/dialogs; deleted `jar.mn` / `moz.build` in the four dirs (MOZ_SRC_FILES, TESTING_JS_MODULES, SPHINX_TREES, XPI_NAME, BROWSER_CHROME / XPCSHELL manifests) | front-end (`./mach build faster`). moz.build edits regenerate the backend automatically, no configure rerun |
| `toolkit/components/glean/metrics_index.py` plus the three deleted `metrics.yaml` | Glean registry regeneration. The metric tables are compiled into libxul, so do one full `./mach build` (no source change in C++/Rust, but the generated sources change) |
| `taskcluster/kinds/build-extensions/kind.yml`, lint configs, `mots.yaml`, docs | no build effect |
| configure / `moz.configure` / `mozconfig` | none. `MOZ_ENTERPRISE` (used in the deleted moz.build and `IPProtectionHelpers.sys.mjs`) is defined in `toolkit/moz.configure` and also read by `python/mozbuild`; leave it |
| C++ / Rust / Cargo / IPDL / XPIDL / `components.conf` / `StaticPrefList.yaml` | none |
| Packaging | `browser/installer/package-manifest.in` and `allowed-dupes.mn` do not mention the feature; built-in add-ons are packed through `jar.mn` (browser.jar), so deleting the `jar.mn` files is enough |

## Tests to delete or edit

Delete with their directories: `browser/components/ipprotection/tests/{browser,xpcshell}`, `toolkit/components/ipprotection/tests/xpcshell`, `browser/extensions/ipp-activator/tests/browser`, `browser/extensions/search-detection/tests/browser`.

Delete loose: `browser_privacy_ipprotection.js`, `browser_privacy_ipprotection_siterules.js`, `browser_sma_ipprotection_enroll.js`, `test_browserGlue_migration_ippL10nCoverage.js`.

Edit: `browser/components/tests/unit/test_browserGlue_migration_remove_pref.js` and its `xpcshell.toml`; `preferences/tests/privacy/browser.toml`; `preferences/tests/browser_legacy_pane_mappings.js`; `preferences/tests/permissions/browser_capability_filter.js`; `asrouter/tests/browser/browser_trigger_listeners.js`; `messaging-system/.../test/browser/browser.toml`; `toolkit/content/tests/browser/browser_about_logging.js` (optional); `browser-static/browser_all_files_referenced.js`; `browser-performance/browser_startup_syncIPC.js` (optional); `netwerk/test/browser/head_local_network_access.js`.

Suites to run after the change (consumers): `browser/components/preferences/tests` (privacy, permissions, `browser_legacy_pane_mappings.js`), `browser/components/customizableui/test` (default navbar placements), `browser/components/asrouter/tests/browser/browser_trigger_listeners.js` and the feature callout tests, `toolkit/components/messaging-system` (xpcshell + SMA browser tests), `browser/components/enterprisepolicies/tests` (policy schema/descriptions consistency), `browser/components/tests/unit`, `browser/base/content/test/static` (all-files-referenced, parsable CSS, unreferenced FTL), `browser/base/content/test/performance` (startup, `browser_startup_syncIPC.js`), `toolkit/content/tests/browser/browser_about_logging.js`, `toolkit/components/glean` tests, `tools/m6e/run_tests.sh` against the P6.0 baseline.

Tests that mention the feature in comments only and stay as they are: `netwerk/test/unit/test_trr_preemptive_proxy_auth.js` (names IPPChannelFilter in a comment; the test is generic), `accessible/tests/browser/mac/browser_confirmation_hint.js`, `browser/components/sessionstore/test/unit/data/sessionstore_complete.json` (fixture with two form field ids), `browser/extensions/newtab/test/jest/...`.

## Must stay (with proof)

1. **Netwerk MASQUE / CONNECT-UDP proxy support** (`netwerk/base/nsProtocolProxyService.cpp`, `nsProxyInfo`, `netwerk/protocol/http/Http3ConnectUDPStream.*`, `HttpConnectionUDP.*`, `NeckoChannelParams.ipdlh`, `dom/media/webrtc/transport/ipc/WebrtcTCPSocket.cpp`, `toolkit/components/extensions/ProxyChannelFilter.sys.mjs`, `netwerk/ipc/ProxyConfigLookup.cpp` comment). Why: general upstream platform code, also reachable from the WebExtensions proxy API and used by newtab's `ActivityStream.applyFilter`/`SponsorProtection`; removal is a netwerk change outside this feature (D21). Proof of unreachability for IPP: the only code that constructs a `masque` nsIProxyInfo for the VPN is `IPPChannelFilter.sys.mjs` (deleted); M6E has no proxy configured (`network.proxy.type` default, no MASQUE prefs in `m6e.js`), so no socket is opened. The newtab image-proxy path needs `config.imageProxyHosts` from `browser.newtabpage.activity-stream.discoverystream.*` / sponsored-content config, which are off in M6E. If the user wants proof in `m6e.js`, there is nothing extra to lock for P6.1a.
2. **Permission type `ipp-vpn` in `extensions/permissions/PermissionManager.cpp` (line ~170, the list of types whose origin attributes are stripped) and `extensions/permissions/test/unit/test_permmanager_oa_strip.js`.** C++ and a unit test; no code can set or read the type once ipprotection is gone (privacy.mjs, vpnSiteRule.js, permissions.js entry and the ipp-activator API all go). Leaving it avoids a C++ rebuild and edits to an upstream unit test. Optional later: drop `"ipp-vpn"_ns` and the test string together.
3. **`browser.ipProtection.userEnabled` read in `toolkit/content/aboutNetErrorHelpers.mjs` (`VPN_ACTIVE`) and allowed in `toolkit/modules/RemotePageAccessManager.sys.mjs` (two RPMGetBoolPref allowlists); `vpnActive` in `toolkit/content/errors/error-lookup.mjs`, `net-error-card.mjs`, `errors/net-errors.mjs` (`fp-neterror-vpn-error-*` in `netError.ftl`) and three tests (`browser_error_registry.js`, `browser_felt_privacy_fallback.js`).** Part of the net-error page's "felt privacy" logic. Proof: after the pref default is removed, `RPMGetBoolPref("browser.ipProtection.userEnabled", false)` returns false, so `vpnActive` is always false and the VPN-specific error branch is dead. Optional cleanup (not required): remove `VPN_ACTIVE`, the two allowlist entries, the `vpnActive` signal and the VPN error entry, and edit the 3 tests.
4. **Newtab Privacy widget VPN promos** (`browser/extensions/newtab/lib/Widgets/PrivacyMessages.sys.mjs`: `CTA_VPN { type: "IPPROTECTION_ENROLL" }`, three `newtab-privacy-message-promo-vpn-*` messages and the `m.feature !== "vpn" || (ctx.showVpnMessages && ctx.vpnEnabled)` gate; `PrivacyFeed.sys.mjs` `vpnEnabled: getBoolPref("browser.ipProtection.enabled", false)`; `common/WidgetsRegistry.mjs` `widgets.privacy.showVpnMessages`; `ActivityStream.sys.mjs` pref def; jest tests `PrivacyMessages.test.js`, `WidgetsRegistry.test.jsx`, `PrefsFeed.test.js`, `Privacy.test.jsx`). Proof: a VPN message is chosen only when `vpnEnabled` is true; it is read as `getBoolPref("browser.ipProtection.enabled", false)` and the pref no longer exists, so it is false. Also `showVpnMessages` is off by default and the widget is part of the newtab feature (separate, train-hopped code). Recommended: leave it in P6.1a. If a hard delete is wanted, remove the three messages, `CTA_VPN`, the `vpnEnabled`/`showVpnMessages` plumbing and the jest cases in one follow-up.
5. **`browser/components/asrouter/tests/NimbusRolloutMessageProvider.sys.mjs`** (two copies of the IP-protection CFR message groups at lines ~2937-3607 and ~4617-5287, referencing `#ipprotection-button`, deleted chrome:// assets and FTL ids). It is a generated snapshot of live Nimbus rollouts (`tools/rewriting/Generated.txt`, `.prettierignore`, regenerated by `asrouter/bin/import-rollouts.js`), exported only through `asrouter/moz.build` TESTING_JS_MODULES, and nothing in the tree imports it. Inert data; leave.
6. **`about:logging` `vpn` preset** if the optional about_logging edits are skipped: it only sets a `logging.config.modules` string of `IPP_*` module names that no module logs under after the deletion. Prefer removing it (see table).
7. **Mozilla VPN promo surfaces** (not IP protection code): `BrowserUtils.PromoType.VPN` / `shouldShowVPNPromo`, `browser.vpn_promo.*` and `browser.contentblocking.report.vpn_*` prefs, about:protections `vpn-card.mjs` and its metrics, More from Mozilla VPN card, about:privatebrowsing VPN promo, FxA menu `PanelUI-fxa-menu-vpn-button` (`browser-sync.js`), ASRouter captive-portal VPN promo, the logo SVGs and their tests (`browser_protections_vpn.js`, `browser_moreFromMozilla_vpn.js`). They do not import `ipprotection` modules and keep working (as dormant promos) without it. `browser.vpn_promo.*` is not locked in `m6e.js` today (see Risks).
8. **`MOZ_ENTERPRISE` flag and `AppConstants.MOZ_ENTERPRISE`**: general build flag, still defined and used by `python/mozbuild`.

## Risks and open questions

- **Mozilla VPN promos are unlocked.** `browser.vpn_promo.enabled` defaults true and is not locked in `m6e.js`. Whether the promo surfaces (item 7 above) should be deleted is a decision outside "VPN (IP protection)"; they probably belong to a separate step (promos/Mozilla-product ads) or to P6.2. Recommend the orchestrator decides explicitly; this branch does not touch them.
- **Large hand edit in `preferences/config/privacy.mjs`** (about 500 lines in three ranges). Remove by symbol, then `./mach eslint browser/components/preferences` and run the privacy preferences suite; unused imports (`Intl.Collator`-based helpers, `Services.perms` usage) are the likely leftovers.
- **Glean regeneration.** Removing the three `metrics.yaml` files changes generated code, so P6.1a needs a full `./mach build`, not only `faster`. Do it on the shared objdir only when the baseline build is finished. If another P6.1x branch also edits `metrics_index.py`, expect trivial line-level conflicts (alphabetical list).
- **Merge conflicts with other branches.** `toolkit/components/moz.build` (`nimbus` line follows `ipprotection`), `browser/components/moz.build` (`genai`, `firefoxview` neighbours), `browser/extensions/moz.build`, `browser/components/preferences/{privacy.js,preferences.js,preferences.xhtml}`, `browser/app/profile/firefox.js`, `m6e.js`, `browser/base/content/browser.xhtml`, `ProfileDataUpgrader.sys.mjs`, `FeatureManifest.yaml` and the generated `tools/@types` files are shared with other features; keep each edit tight.
- **Policy removal.** `IPProtectionAvailable` is a documented enterprise policy (x-category "Network security"); an enterprise `policies.json` that still sets it would now produce an "unknown policy" warning in the console only. No test names it.
- **`browser_capability_filter.js`** needs a real test run after choosing a replacement permission type; the dialog only knows types listed in `permissionExceptionsL10n`.
- **`browser_startup_syncIPC.js`** GPU-IPC allowance may or may not have been caused by search-detection. It is `ignoreIfUnused`, so removal is safe if the test passes; otherwise keep it.
- **`FeatureCalloutMessages.sys.mjs`** edit is array surgery in a 1300-line file; check balanced braces and that `matchIncompleteTargeting`/`matchCurrentScreenTargeting` helpers are still used elsewhere (they are, by the tab-group and PDF tour messages).
- **Generated TS types** (`tools/@types/generated/*`) are tracked; decide whether to hand-edit or regenerate. A CI lint may compare them, but it is irrelevant for the local macOS build.
- **Leftover string "VPN" in unrelated copy** (for example `preferences-doh-default-detailed-desc-4`, `browser_check_identity_state.js`) is generic wording about third-party VPNs and stays.
- **`ipp-activator` XPI**: removing `xpi-build/` drops the `dist/xpi-stage/ipp-activator@mozilla.com.xpi` output; only the taskcluster artifact list used it.
- Searches were run with `git grep` over tracked files only (excluding `third_party`/`node_modules` for the second pass); untracked files (for example `third_party/application-services/automation/kotlin-components-docs/build/`) were not searched.

## Critic addendum

Method: independent `git grep -n -I` passes at base commit over the whole tree (excluding the four deleted directories) for: `ipprotection|ip-protection|ip_protection|ip protection|ipp-activator|ippactivator|ipp-vpn|IPP_*|IPP[A-Z]*|IPPROTECTION|vpn-site-rule|vpnSiteRule|BANDWIDTH|search-detection|search_detection|addonsSearchDetection`, every FTL id in `ipProtection.ftl` (192 ids, checked as whole-word matches in `browser/`, `toolkit/`, `services/`), `vpn-serverlist`, `built-in-vpn`, `ipProtectionReady|PanelClosed|BandwidthReset`, `PanelUI-ipprotection`, `built_in_addons`. The analyst's file list is complete for every one of these angles: no staying file outside the report's tables references the feature in code. File counts check out (94+58+31+13 = 196 tracked files). The ten Fluent migrations all have `browser/browser/ipProtection.ftl` as their only source. No FTL id from `ipProtection.ftl` is used by any staying file other than the ones the report already lists (`browser.js`, `FeatureCalloutMessages`, `permissions.js`, `preferences.js`, `privacy.mjs`).

### Missed (small)

| File | Reference | Action |
|---|---|---|
| `toolkit/components/glean/tags.yaml` (line ~336) | tag `'Firefox :: IP Protection'`. The report says "no tags defined for this feature"; the tag is defined here and used only by the two `ipprotection` `metrics.yaml` files (8 uses, all in deleted dirs) | Optional: remove the 2-line entry. Safe to leave (an unused tag is not an error). If removed, nothing else uses it. This file is also touched by Glean registry regeneration, so batch it with the `metrics_index.py` edit |
| `netwerk/test/unit/test_trr_proxy_channel_filter.js` (header comment) | names "IP Protection's" proxy filter | Comment only, the test is generic. Leave. Add to the "comment only" list next to `test_trr_preemptive_proxy_auth.js` |
| `browser/components/asrouter/modules/FeatureCalloutMessages.sys.mjs` | exact range is lines 86-359 (the three IPP messages are the first three entries of the `messages` array). The report says 85-359; line 85 is `let messages = [` and must stay. The comment at 360-362 belongs to `SAVE_TAB_GROUP_ONBOARDING_CALLOUT` and must stay | Correct the range. After the cut `matchIncompleteTargeting`, `matchCurrentScreenTargeting` (used by PDFJS tour at 849/971) and `add24HourImpressionJEXLTargeting` (used at 1310 for `FIREFOX_VIEW`) are all still used, so no helper becomes dead |
| Runtime-only prefs not in the report's list | `browser.ipProtection.stateCache`, `.cacheDisabled`, `.panelOpenCount` (appear only in deleted code, deleted tests and the `NimbusRolloutMessageProvider` snapshot) | Nothing to remove; add to the "no default" list for completeness |
| `toolkit/content/aboutNetErrorHelpers.mjs`, `RemotePageAccessManager.sys.mjs` | `browser.ipProtection.userEnabled` | Already in Must stay #3. Note that `userEnabled` is in the `firefox.js` block that gets deleted, so the RPM read falls back to its `false` default. Confirmed the call passes `false` as default (`RPMGetBoolPref("browser.ipProtection.userEnabled", false)`) |
| `browser/app/profile/firefox.js` line ~2631-2661, ~3020 | `browser.vpn_promo.*`, `browser.contentblocking.report.vpn*`, `identity.fxaccounts.toolbar.pxiToolbarEnabled.vpnEnabled` | Not part of this feature. The `pxiToolbarEnabled.vpnEnabled` pref belongs to the FxA toolbar (P6.1f, later feature); note it for that analysis |

### Services / remote settings checked and clean

- `IPProtectionServerlist.sys.mjs` uses the Remote Settings collection `vpn-serverlist`. No dump under `services/settings/dumps/` and no allowlist entry in `services/settings/Utils.sys.mjs` names it. No action.
- Built-in extension registration is automatic: `toolkit/mozapps/extensions/gen_built_in_addons.py` builds `built_in_addons.json` from the installed `builtin-addons/*/manifest.json`, so deleting the two `jar.mn` files removes both extensions with no other edit. Nothing in `browser/installer/package-manifest.in` or `allowed-dupes.mn` names them.
- `components.conf`, `BrowserComponents.manifest`, `DesktopActorRegistry`, `BrowserGlue`, `StaticPrefList.yaml`, `modules/libpref/init`, `devtools/`, `remote/`, `testing/marionette`, `security/manager`, lint ignore files (`.eslintignore`, `.prettierignore`, `.stylelintignore`, `tools/lint/*.yml`) contain no reference. Confirmed P1.9 (`f49f2658bb`) removed the two `BrowserComponents.manifest` lines and no other `IPProtectionHelpers`/`IPProtectionActivator` import exists outside the feature (only a generated `tools/@types` line).

### Verified claims

- "Earlier features: none" and "later feature referencing this one: only `nimbus/FeatureManifest.yaml`": confirmed. No hits in translations, genai, ml, sync, fxaccounts, updater, crashreporter (the only translations hit is cld2 data, unrelated).
- Must stay #1 (netwerk MASQUE), #2 (`ipp-vpn` in `PermissionManager.cpp` line 170 and `test_permmanager_oa_strip.js`), #5 (`NimbusRolloutMessageProvider.sys.mjs`: referenced only by `asrouter/moz.build` TESTING_JS_MODULES, `.prettierignore`, `tools/rewriting/Generated.txt` and the import script; no importer) hold.
- `accessible/tests/browser/mac/browser_confirmation_hint.js` only says "VPN" in a header comment about the confirmation hint framework. Harmless.

### Corrections and risks to add

1. Removing the `m6e.js` locks (`browser.ipProtection.enabled`, `blockIPProtectionCallouts`) and the `firefox.js` defaults leaves one staying reader of `browser.ipProtection.enabled`: `browser/extensions/newtab/lib/Widgets/PrivacyFeed.sys.mjs` (`vpnEnabled`). The pref becomes unlocked and absent, so a user or `user.js` could set it true. The newtab VPN promo would then still need `widgets.privacy.showVpnMessages` (default off), and the `IPPROTECTION_ENROLL` action it sends would hit an unknown-action path because the `SpecialMessageActions` case is gone. So the surface is inert, but it is no longer "locked". If the "provably unreachable" bar is strict, either keep the `browser.ipProtection.enabled` lock line in `m6e.js` until the newtab privacy widget is dealt with, or delete the newtab VPN promo (`CTA_VPN`, three `newtab-privacy-message-promo-vpn-*` messages, `vpnEnabled`/`showVpnMessages`, jest cases) in P6.1a. The report's "leave it" is acceptable only if the orchestrator accepts an unlocked, unread pref.
2. `tools/m6e/acceptance.py`: besides the `REMOVED_PREFS["VPN"]` entry, check the removed-widgets check at line 129 (`"VPN": ["ipprotection-button"]`) and `REMOVED_SETTINGS_GROUPS` ("ipprotection") still pass when the id no longer exists anywhere. The report says they do; they are "must not be present" checks, so yes, but they now test nothing. Fine to keep.
3. `mots.yaml`: removing the "IP Protection" module leaves YAML anchors (`*fchasen`, `*kpatenio`, `*rking`, `*niklas`, `*basti`, `*artem`, `*mcleinman`, `*baku`) that are defined in the people list; unused anchors are valid YAML, but if `mots` lint rejects people with no module, drop them too. Low risk.
4. `browser/components/tests/unit/test_browserGlue_migration_remove_pref.js` also has a top-level cleanup line (line 13) for `browser.ipProtection.locationListCache`, as the report says; fine.
5. No wrong "must stay" claim found. The only inaccuracy is the off-by-one line range in `FeatureCalloutMessages` above.
