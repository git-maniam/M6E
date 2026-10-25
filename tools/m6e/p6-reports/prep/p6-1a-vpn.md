# P6.1a prep: VPN (IP protection) and addons-search-detection

Branch `m6e-p6/vpn`, built on 4110e90366 (analysis was made at cb002d4874; the two commits between them only touch the plan). Four commits, nothing built or run, nothing pushed. Static checks done: `node --check` on all 29 edited .js/.mjs files (only `firefox.js` fails, it is a preprocessor file; its `#ifdef/#else/#endif` removal is balanced), JSON parse of the four edited JSON files, YAML parse of tags.yaml, FeatureManifest.yaml and build-extensions kind.yml (mots.yaml cannot be parsed by the local Ruby, same as before the edit), CSS brace counts, and tree-wide `git grep` for the feature names, 192 FTL ids and the chrome/resource/moz-src URLs.

## Deleted (git rm)

- Directories: `browser/components/ipprotection` (94 files), `toolkit/components/ipprotection` (58), `browser/extensions/ipp-activator` (31), `browser/extensions/search-detection` (13).
- `browser/locales/en-US/browser/ipProtection.ftl`.
- `browser/components/preferences/dialogs/vpnSiteRule.{xhtml,js,css}`.
- Tests: `preferences/tests/privacy/browser_privacy_ipprotection.js`, `browser_privacy_ipprotection_siterules.js`, `messaging-system/.../test/browser/browser_sma_ipprotection_enroll.js`, `browser/components/tests/unit/test_browserGlue_migration_ippL10nCoverage.js`.
- Ten Fluent migrations in `python/l10n/fluent_migrations/` (all sourced from `browser/browser/ipProtection.ftl`).

## Edited

- Build: `browser/components/moz.build`, `toolkit/components/moz.build`, `browser/extensions/moz.build` (DIRS), `preferences/dialogs/jar.mn`, `toolkit/components/glean/metrics_index.py` (3 yaml), `toolkit/components/glean/tags.yaml` (`Firefox :: IP Protection`), `taskcluster/kinds/build-extensions/kind.yml`, `taskcluster/test/params/mc-nightly-all.yml` (14 stale lines), `toolkit/docs/index.md`, `mots.yaml` (module "IP Protection"; the people anchors `fchasen`, `rking`, `basti`, `artem`, `mcleinman` are now unused, left in place), `eslint-rollouts.config.mjs`, `eslint-subdirs.config.mjs`, `stylelint-rollouts.config.js`.
- Preferences: `privacy.js`, `config/privacy.mjs` (lazy getters, pref list, `ipprotection` and `vpnSiteRules` groups, all `ipProtection*` settings and helpers, and the `settingsRedesignEnabled` setting that only they used), `privacy.inc.xhtml`, `preferences.js`, `preferences.xhtml`, `config/LegacyPaneMappings.mjs`, `dialogs/permissions.js` (`ipp-vpn` entry), `dialogs/permissions.xhtml`, `preferences.css`.
- Chrome: `browser.xhtml`, `browser.js`, `appmenu-viewcache.inc.xhtml`, `navigator-toolbox.js`, `CustomizableUI.sys.mjs`, `toolbarbutton-icons.css`, `toolbarbuttons.css`, `panelUI-shared.css`.
- Prefs/policy/migration: `browser/app/profile/firefox.js` (all `browser.ipProtection.*`), `m6e.js` (only the `blockIPProtectionCallouts` lock removed, see notDone), `Policies.sys.mjs` + `policies-schema.json` + `policies-descriptions.ftl` (`IPProtectionAvailable`), `ProfileDataUpgrader.sys.mjs` (the `< 180` and `< 184` blocks; version not changed), `testing/profiles/{unittest-required,perf}/user.js`, `netwerk/test/browser/head_local_network_access.js`.
- Messaging: `SpecialMessageActions.sys.mjs` (lazy getter, 4 allowed prefs, `IPPROTECTION_ENROLL` case), `MessagingSystemBlocklists.sys.mjs`, both schema JSON + md pairs (SMA action; triggers `ipProtectionReady|PanelClosed|BandwidthReset`), SMA `test/browser/browser.toml`, `FeatureCalloutMessages.sys.mjs` (3 IPP callouts, lines 86-359), `toolkit/components/nimbus/FeatureManifest.yaml` (`ipProtection` feature; belongs to a later feature but referenced this one).
- about:logging: `aboutLogging.mjs` (`vpn` preset), `aboutLogging.ftl`, `browser_about_logging.js` (two javascriptTracing tasks that used the vpn preset).
- Tests: `browser_legacy_pane_mappings.js` (2 rows + comment), `preferences/tests/privacy/browser.toml`, `permissions/browser_capability_filter.js` (now uses permission type `install`, no onboarding-mask pref), `asrouter/tests/browser/browser_trigger_listeners.js` (imports + 3 tasks), `test_browserGlue_migration_remove_pref.js` + `unit/xpcshell.toml`, `browser_all_files_referenced.js` (ipp-activator allowlist), `browser_startup_syncIPC.js` (search-detection `PGPU::Msg_AddLayerTreeIdMapping` entry).
- Generated types: `tools/@types/generated/{lib.gecko.modules.d.ts,tspaths.json,lib.gecko.glean.d.ts}`, `tools/@types/subs/FeatureManifest.sys.d.mts` edited by hand (130 lines).
- `tools/m6e`: `branding/fox-art.txt` and `merge/nightly-prefs-reviewed.txt` lines removed.

## integrationTodos

- One full `./mach build` is needed (not `build faster`): the three `metrics.yaml` removals change the Glean registry compiled into libxul. No configure rerun, no Cargo change, no Cargo.lock change (the feature has no Rust).
- Tests to run: `browser/components/preferences/tests` (privacy, permissions incl. `browser_capability_filter.js` which now uses `install`, `browser_legacy_pane_mappings.js`), `browser/components/customizableui/test`, `asrouter/tests/browser/browser_trigger_listeners.js` and feature callout tests, `toolkit/components/messaging-system` xpcshell + SMA browser tests, enterprisepolicies tests (schema/description consistency), `browser/components/tests/unit`, `browser/base/content/test/static` (all files referenced, unreferenced FTL), `browser/base/content/test/performance` (`browser_startup_syncIPC.js`), `toolkit/content/tests/browser/browser_about_logging.js`, glean tests, `tools/m6e/run_tests.sh` against the P6.0 baseline, `tools/m6e/acceptance.py`.
- Run `./mach eslint browser/components/preferences` and `./mach lint` on the edited files; unused-variable leftovers in `config/privacy.mjs` are the likeliest finding.
- Optionally regenerate `tools/@types` with the generator instead of trusting the hand edit.
- Plan housekeeping (`M6E_IMPLEMENTATION_PLAN.md`, `m6e_browser_agent_instructions.md`) left for the orchestrator.
- Merge-conflict hotspots with other features: `firefox.js`, `m6e.js`, `privacy.js`, `preferences.js`, `preferences.xhtml`, both `moz.build` files, `metrics_index.py`, `FeatureManifest.yaml`, `ProfileDataUpgrader.sys.mjs`, `browser.xhtml`, tools/@types files.

## notDone (kept on purpose)

- `browser.ipProtection.enabled` stays locked false in `m6e.js` (and `tools/m6e/acceptance.py` REMOVED_PREFS["VPN"] still passes). Reason: the staying newtab privacy widget (`PrivacyFeed.sys.mjs` `vpnEnabled`) still reads it, so the lock is what keeps the VPN promo path provably off. `blockIPProtectionCallouts` lock removed (no reader).
- Netwerk MASQUE / CONNECT-UDP proxy code: general platform code; the only constructor of the VPN proxy info (`IPPChannelFilter`) is deleted and no proxy is configured.
- Permission type `ipp-vpn` in `extensions/permissions/PermissionManager.cpp` and `test_permmanager_oa_strip.js`: C++ and unit test; nothing can set or read it now.
- `browser.ipProtection.userEnabled` read in `toolkit/content/aboutNetErrorHelpers.mjs` and allowed in `RemotePageAccessManager.sys.mjs`: the pref has no default now, so `RPMGetBoolPref(..., false)` returns false and the VPN net-error branch is dead.
- Newtab Privacy widget VPN promos (`PrivacyMessages.sys.mjs` `CTA_VPN`, `PrivacyFeed.sys.mjs`, jest tests): `IPPROTECTION_ENROLL` is now an unknown action, `showVpnMessages` is off by default and `vpnEnabled` is locked false. Inert; delete with the newtab widget work.
- `browser/components/asrouter/tests/NimbusRolloutMessageProvider.sys.mjs`: generated snapshot of live rollouts, nothing imports it.
- Mozilla VPN promo surfaces (`BrowserUtils.PromoType.VPN`, `browser.vpn_promo.*`, about:protections vpn card, More from Mozilla card, FxA menu VPN button, logo SVGs): separate paid-product promos, not locked in `m6e.js`; needs an explicit decision (promos step or P6.2). `identity.fxaccounts.toolbar.pxiToolbarEnabled.vpnEnabled` belongs to the P6.1f analysis.
- Comment-only mentions left: `netwerk/test/unit/test_trr_*`, `netwerk/ipc/ProxyConfigLookup.cpp`, `dialogs/permissions.js` ("Only for ipp-vpn" JSDoc on the generic `addVisible` plumbing), `sessionstore_complete.json` fixture, `taskcluster/config.yml` `feature-ipprotection`, `tools/lint/extra-js-modules/allowlist.txt` (stale mobile entry).
- `widget/nsPrinterCUPS.cpp` and `gfx/thebes/PrintTarget.cpp` match "IPP_" only because of the Internet Printing Protocol; unrelated.
