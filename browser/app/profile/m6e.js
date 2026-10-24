/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// M6E default preferences. Every M6E pref change lives here, so upstream
// merges only touch firefox.js through the #include at its end.

// The interface ships in en-US only, without language switching or language
// pack downloads.
pref("intl.multilingual.enabled", false, locked);
pref("intl.multilingual.downloadEnabled", false, locked);
pref("intl.multilingual.aboutWelcome.languageMismatchEnabled", false, locked);

// The browser chrome is light-only, so private windows don't switch to the
// dark private-browsing theme.
pref("browser.theme.dark-private-windows", false, locked);

// Mozilla accounts and Sync are removed. These are the prefs the
// DisableFirefoxAccounts enterprise policy locks.
pref("identity.fxaccounts.enabled", false, locked);
pref("browser.aboutwelcome.enabled", false, locked);

// The Mozilla VPN integration (IP Protection) is removed.
pref("browser.ipProtection.enabled", false, locked);
pref("browser.ipProtection.blockIPProtectionCallouts", true, locked);

// Telemetry and data collection are off and cannot be turned back on. The
// upload prefs are defined here because builds without health reporting
// no longer define them.
pref("datareporting.healthreport.uploadEnabled", false, locked);
pref("datareporting.usage.uploadEnabled", false, locked);
pref("datareporting.policy.dataSubmissionEnabled", false, locked);
pref("datareporting.policy.dataSubmissionPolicyBypassNotification", true, locked);
pref("toolkit.telemetry.enabled", false, locked);
pref("toolkit.telemetry.unified", false, locked);
pref("toolkit.telemetry.archive.enabled", false, locked);
pref("toolkit.telemetry.server", "", locked);
pref("toolkit.telemetry.newProfilePing.enabled", false, locked);
pref("toolkit.telemetry.shutdownPingSender.enabled", false, locked);
pref("toolkit.telemetry.updatePing.enabled", false, locked);
pref("toolkit.telemetry.bhrPing.enabled", false, locked);
pref("toolkit.telemetry.firstShutdownPing.enabled", false, locked);
pref("browser.newtabpage.activity-stream.telemetry", false, locked);
pref("browser.discovery.enabled", false, locked);
pref("network.http.akamai_waf_telemetry.enabled", false, locked);

// Crash reporting is not built. Never offer to send crash reports.
pref("browser.crashReports.unsubmittedCheck.enabled", false, locked);
pref("browser.crashReports.unsubmittedCheck.autoSubmit2", false, locked);
pref("browser.tabs.crashReporting.sendReport", false, locked);

// No Mozilla update, study or add-on services. Extensions are installed and
// updated manually.
pref("extensions.systemAddon.update.enabled", false, locked);
pref("extensions.systemAddon.update.url", "", locked);
pref("app.normandy.enabled", false, locked);
pref("app.normandy.api_url", "", locked);
pref("app.shield.optoutstudies.enabled", false, locked);
pref("extensions.getAddons.cache.enabled", false, locked);
pref("extensions.getAddons.showPane", false, locked);
pref("extensions.htmlaboutaddons.recommendations.enabled", false, locked);
pref("extensions.update.enabled", false, locked);
pref("extensions.update.autoUpdateDefault", false, locked);
pref("extensions.abuseReport.enabled", false, locked);

// No Safe Browsing lookups. The Mozilla provider's update URL is kept: it
// delivers the tracking protection lists through Remote Settings.
pref("browser.safebrowsing.malware.enabled", false, locked);
pref("browser.safebrowsing.phishing.enabled", false, locked);
pref("browser.safebrowsing.blockedURIs.enabled", false, locked);
pref("browser.safebrowsing.downloads.enabled", false, locked);
pref("browser.safebrowsing.downloads.remote.enabled", false, locked);
pref("browser.safebrowsing.provider.google4.updateURL", "", locked);
pref("browser.safebrowsing.provider.google4.gethashURL", "", locked);
pref("browser.safebrowsing.provider.google.updateURL", "", locked);
pref("browser.safebrowsing.provider.google.gethashURL", "", locked);
pref("browser.safebrowsing.provider.mozilla.gethashURL", "", locked);

// Widevine and OpenH264 keep installing automatically, but from the download
// lists bundled in toolkit/content/gmp-sources (Google and Cisco only) rather
// than from Mozilla's update server.
pref("media.gmp-manager.url", "", locked);

// DNS over HTTPS defaults to Increased Protection with the default provider.
// The user can change or turn it off in Settings.
pref("network.trr.mode", 2);

// No captive portal, connectivity or region checks.
pref("network.captive-portal-service.enabled", false, locked);
pref("network.connectivity-service.enabled", false, locked);
pref("captivedetect.canonicalURL", "", locked);
pref("browser.region.network.url", "", locked);
pref("browser.region.update.enabled", false, locked);

// No welcome or what's-new pages from mozilla.org or firefox.com.
pref("startup.homepage_welcome_url", "", locked);
pref("startup.homepage_welcome_url.additional", "", locked);
pref("startup.homepage_override_url", "", locked);
pref("browser.startup.homepage_override.mstone", "ignore", locked);

// No sponsored content, recommended stories, weather or Firefox Suggest.
pref("browser.newtabpage.activity-stream.showSponsored", false, locked);
pref("browser.newtabpage.activity-stream.showSponsoredTopSites", false, locked);
pref("browser.newtabpage.activity-stream.showSponsoredCheckboxes", false, locked);
pref("browser.newtabpage.activity-stream.system.showSponsored", false, locked);
pref("browser.newtabpage.activity-stream.feeds.section.topstories", false, locked);
pref("browser.newtabpage.activity-stream.feeds.system.topstories", false, locked);
pref("browser.newtabpage.activity-stream.showWeather", false, locked);
pref("browser.newtabpage.activity-stream.system.showWeather", false, locked);
pref("browser.newtabpage.activity-stream.unifiedAds.tiles.enabled", false, locked);
pref("browser.newtabpage.activity-stream.unifiedAds.spocs.enabled", false, locked);
pref("browser.newtabpage.activity-stream.unifiedAds.endpoint", "", locked);
pref("browser.newtabpage.activity-stream.discoverystream.endpoints", "", locked);
pref("browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons", false, locked);
pref("browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features", false, locked);
pref("browser.topsites.contile.enabled", false, locked);
pref("browser.urlbar.quicksuggest.enabled", false, locked);
pref("browser.urlbar.suggest.quicksuggest.sponsored", false, locked);
pref("browser.urlbar.suggest.quicksuggest.nonsponsored", false, locked);
pref("browser.urlbar.merino.endpointURL", "", locked);
// Its Rust Remote Settings client bypasses the D2 allowlist.
pref("toolkit.contentRelevancy.enabled", false, locked);

// No AI features. These are the prefs the AIControls enterprise policy sets
// when every feature is blocked, plus the shared machine learning engine.
pref("browser.ai.control.default", "blocked", locked);
pref("browser.ai.control.translations", "blocked", locked);
pref("browser.ai.control.pdfjsAltText", "blocked", locked);
pref("browser.ai.control.smartTabGroups", "blocked", locked);
pref("browser.ai.control.linkPreviewKeyPoints", "blocked", locked);
pref("browser.ai.control.sidebarChatbot", "blocked", locked);
pref("browser.ai.control.smartWindow", "blocked", locked);
pref("browser.ai.control.speechRecognition", "blocked", locked);
pref("browser.ml.enable", false, locked);
pref("browser.ml.chat.enabled", false, locked);
pref("browser.ml.chat.page", false, locked);
pref("browser.ml.linkPreview.enabled", false, locked);
pref("browser.smartwindow.enabled", false, locked);
pref("browser.tabs.groups.smart.enabled", false, locked);
pref("browser.tabs.groups.smart.userEnabled", false, locked);
pref("browser.translations.enable", false, locked);
pref("browser.translations.select.enable", false, locked);
pref("pdfjs.enableAltText", false, locked);
// With every AI feature removed, there is nothing for the AI settings pane to control.
pref("browser.preferences.aiControls", false, locked);

// New Tab widgets that fetch content from Mozilla's Merino services, and
// wallpapers, which load from Remote Settings attachments outside the
// allowlist. Local widgets (clocks, lists, focus timer) stay available.
pref("browser.newtabpage.activity-stream.newtabWallpapers.enabled", false, locked);
pref("browser.newtabpage.activity-stream.widgets.system.crossword.enabled", false, locked);
pref("browser.newtabpage.activity-stream.widgets.crossword.enabled", false, locked);
pref("browser.newtabpage.activity-stream.widgets.system.solitaire.enabled", false, locked);
pref("browser.newtabpage.activity-stream.widgets.solitaire.enabled", false, locked);
pref("browser.newtabpage.activity-stream.widgets.system.horoscopes.enabled", false, locked);
pref("browser.newtabpage.activity-stream.widgets.horoscopes.enabled", false, locked);
pref("browser.newtabpage.activity-stream.widgets.system.stocks.enabled", false, locked);
pref("browser.newtabpage.activity-stream.widgets.stocks.enabled", false, locked);
pref("browser.newtabpage.activity-stream.widgets.system.travel.enabled", false, locked);
pref("browser.newtabpage.activity-stream.widgets.travel.enabled", false, locked);
pref("browser.newtabpage.activity-stream.widgets.system.weather.enabled", false, locked);
pref("browser.newtabpage.activity-stream.widgets.weather.enabled", false, locked);
pref("browser.newtabpage.activity-stream.widgets.system.weatherForecast.enabled", false, locked);
pref("browser.newtabpage.activity-stream.widgets.weatherForecast.enabled", false, locked);
pref("browser.newtabpage.activity-stream.widgets.system.pictureOfTheDay.enabled", false, locked);
pref("browser.newtabpage.activity-stream.widgets.pictureOfTheDay.enabled", false, locked);

// No DNS prefetching, link prefetching or speculative connections. These are
// defaults, not locks: the Privacy Shield (P3.5) manages hardening prefs.
pref("network.dns.disablePrefetch", true);
pref("network.dns.disablePrefetchFromHTTPS", true);
pref("network.prefetch-next", false);
pref("network.http.speculative-parallel-limit", 0);
pref("browser.places.speculativeConnect.enabled", false);
pref("browser.urlbar.speculativeConnect.enabled", false);

// No Mozilla support, feedback or web-compatibility sites. Support links hide
// themselves when app.support.baseURL is empty.
pref("app.support.baseURL", "", locked);
pref("app.feedback.baseURL", "", locked);
pref("ui.new-webcompat-reporter.enabled", false, locked);
pref("browser.preferences.moreFromMozilla", false, locked);
pref("browser.newtabpage.activity-stream.default.sites", "https://www.wikipedia.org/,https://www.youtube.com/,https://www.reddit.com/", locked);
pref("app.releaseNotesURL.aboutDialog", "about:blank", locked);

// Google and Bing, from the bundled search configuration, are the only search
// engines. OpenSearch, custom, add-on and policy engines are never added.
pref("browser.search.bundledEnginesOnly", true, locked);

// The bookmarks toolbar is hidden until the user turns it on in Settings,
// Appearance. The setting is on or off, so there is no new-tab-only default.
pref("browser.toolbars.bookmarks.visibility", "never");

// Reader View is black text on white only (P3.3). High contrast mode still
// applies its own colours.
pref("reader.color_scheme", "light", locked);
pref("reader.color_scheme.values", "[\"light\"]", locked);

// Privacy hardening (P3.4), as defaults rather than locks: the Privacy Shield
// (P3.5) switches them. Fingerprinting resistance (privacy.resistFingerprinting)
// stays off by default; the Privacy Shield's strict mode turns it on (D18).
//
// New profiles start in strict Enhanced Tracking Protection, which includes
// Fingerprinting Protection (see ContentBlockingPrefs.init).
pref("browser.contentblocking.initialCategory", "strict");

// Ask sites not to sell or share data (Global Privacy Control).
pref("privacy.globalprivacycontrol.enabled", true);

// HTTPS-Only mode, with a warning page before any HTTP-only site.
pref("dom.security.https_only_mode", true);

// Cross-origin requests send only the origin as the referrer.
pref("network.http.referer.XOriginTrimmingPolicy", 2);

// WebRTC stays on for calls, but doesn't expose local IP addresses: only the
// default route's address is used, and only the proxy when behind one.
pref("media.peerconnection.ice.default_address_only", true);
pref("media.peerconnection.ice.proxy_only_if_behind_proxy", true);

// TLS: no 0-RTT data (replayable), and refuse servers without safe
// renegotiation. Certificate pinning keeps Firefox's level, which allows
// locally trusted roots: strict pinning breaks pinned sites behind TLS
// inspection, such as corporate gateways.
pref("security.tls.enable_0rtt_data", false);
pref("security.ssl.require_safe_negotiation", true);
pref("browser.xul.error_pages.expert_bad_cert", true);

// Show internationalized domain names as punycode, against lookalike domains.
pref("network.IDN_show_punycode", true);

// No scripts in PDFs, no HTTP authentication prompts from cross-origin
// subresources, and pages can't move or resize the window.
pref("pdfjs.enableScripting", false);
pref("network.auth.subresource-http-auth-allow", 1);
pref("dom.disable_window_move_resize", true);

// The Privacy Shield (P3.5) in Settings, Privacy: on applies the hardening
// above, off switches to compatibility values, and maximum adds
// fingerprinting resistance (M6EPrivacyShield.sys.mjs).
pref("m6e.privacyShield.enabled", true);
pref("m6e.privacyShield.maximum", false);

// Tracker Blocker (P3.7, D19): the urlbar's trust icon shows how many items
// were blocked on the current page.
pref("browser.urlbar.trackerCount.featureGate", true);
pref("browser.urlbar.trackerCount.enabled", true);
// No data breach alerts: they offer scans with Mozilla Monitor.
pref("browser.urlbar.trustPanel.breachAlerts", false, locked);
pref("browser.urlbar.trustPanel.breachAlerts.featureGate", false, locked);

// The page telemetry button and panel (D29): measured on the device only.
pref("m6e.pageTelemetry.enabled", true);

// Attack-surface reduction (P4.8a, D22). M6E is built from Nightly, which
// switches on experimental, less tested web features. These take Firefox
// Release's values instead, except where Nightly's value is itself a security
// gain (COEP credentialless, opaque response blocking, schemeful SameSite,
// DOM clobbering protection, unused-permission expiry, content-script
// resource isolation, no Components shim, no inline handlers in SVG <use>,
// and the MathML and drag-event removals), which stay.
pref("apz.record_checkerboarding", false);
pref("browser.safebrowsing.globalCache.enabled", false);
pref("browser.safebrowsing.realTime.enabled", false);
pref("browser.safebrowsing.realTime.simulation.enabled", false);
pref("browser.smartwindow.aitab.enabled", false);
pref("clipboard.readCustomFormatsFromClipboard.enabled", false);
pref("docshell.shistory.sameDocumentNavigationOverridesLoadType", true);
pref("docshell.shistory.restoreSubframesOnReload", true);
pref("dom.audio_session.enabled", false);
pref("dom.audio_session.state_enabled", false);
pref("dom.audio_session.block_start_during_interrupt.enabled", false);
pref("dom.clipboard.customFormatSupport.enabled", false);
pref("dom.event.treat_ctrl_click_as_right_click.disabled", false);
pref("dom.fetch.streaming_upload", false);
pref("dom.gamepad.non_standard_events.enabled", false);
pref("dom.ipc.tabs.createKillHardCrashReports", false);
pref("dom.forms.html_color_picker.enabled", false);
pref("dom.forms.alpha.enabled", false);
pref("dom.media.muted-state.enabled", false);
pref("dom.media.webcodecs.h265.enabled", false);
pref("dom.quota_exceeded_error.enabled", false);
pref("dom.reporting.crash.enabled", false);
pref("dom.script_loader.experimental.navigation_cache", false);
pref("dom.speculation_rules.enabled", false);
pref("dom.workers.throttling.enabled", false);
pref("dom.webcrypto.encapsulation.enabled", false);
pref("dom.worker.remoteDebugger.enabled", false);
pref("general.smoothScroll.msdPhysics.enabled", false);
pref("javascript.options.experimental.legacy_regexp", false);
pref("layout.css.anchor-positioning.follows-transforms.enabled", false);
pref("layout.css.corner-shape.enabled", false);
pref("layout.css.ellipse-corners.enabled", false);
pref("layout.css.grid-subgrid-baselines.enabled", false);
pref("layout.css.display-grid-lanes.enabled", false);
pref("layout.css.random.enabled", false);
pref("layout.css.link-parameters.enabled", false);
pref("layout.dynamic-reflow-roots.enabled", false);
pref("layout.forms.button-input-align-content-block.enabled", false);
pref("layout.css.typed-om.enabled", false);
pref("mathml.a.element.enabled", false);
pref("mathml.lspace_rspace_for_child_spacing_during_mrow_layout.enabled", false);
pref("media.mediacapabilities.from-database", false);
pref("media.hardware-video-decoding-vulkan.enabled", false);
pref("media.getusermedia.browser.enabled", false);
pref("media.aboutwebrtc.auto_refresh.peerconnection_section", true);
pref("media.allowed-to-play.enabled", false);
pref("media.geckoview.audio_session_type.enabled", false);
pref("network.auth.use_redirect_for_retries", false);
pref("network.trr.async_connInfo", false);
pref("network.http.http3.retry_different_ip_family", false);
pref("network.jar.record_failure_reason", false);
pref("network.http.retry_with_another_half_open", false);
pref("network.http.happy_eyeballs_enabled", false);
pref("network.http.happy_eyeballs_upgrade_enabled", false);
pref("network.http.dictionaries.enable", false);
pref("ui.mouse.right_click.select_under_cursor", false);
// Nightly parses untrusted WebRTC SDP with two parsers in parallel.
pref("media.peerconnection.sdp.alternate_parse_mode", "never");
pref("media.decoder-doctor.notifications-allowed", "MediaWMFNeeded,MediaWidevineNoWMF,MediaCannotInitializePulseAudio,MediaCannotPlayNoDecoders,MediaUnsupportedLibavcodec,MediaPlatformDecoderNotFound");
pref("privacy.purge_trackers.logging.level", "Error");
pref("toolkit.aboutProcesses.showThreads", false);
pref("devtools.jsonview.size-profiler.enabled", false);
pref("extensions.formautofill.creditCards.storage.rust.runMigrationTest", false);
pref("browser.urlbar.autoFill.adaptiveHistory.enabled", false);
pref("browser.urlbar.newtab.featureGate", false);
pref("places.semanticHistory.featureGate", false);
pref("browser.tabs.tooltipsShowPidAndActiveness", false);
pref("browser.tabs.groups.alternateMenu", false);
pref("browser.lowMemoryResponseMask", 0);
pref("browser.netError.searchCTA.enabled", false);
pref("browser.sessionstore.loglevel", "Warn");
pref("browser.sessionstore.log.appender.file.logOnSuccess", false);
pref("sidebar.updatedBookmarks.enabled", false);
pref("browser.smartwindow.aitab.menus", false);
pref("services.sync.log.appender.file.logOnSuccess", false);
pref("services.sync.engine.bookmarks.validation.enabled", false);
pref("services.sync.engine.passwords.validation.enabled", false);
pref("devtools.performance.popup.feature-flag", false);
pref("devtools.performance.aboutprofiling.has-developer-options", false);
pref("devtools.layout.boxmodel.highlightProperty", false);
pref("devtools.webconsole.sidebarToggle", false);
pref("devtools.high-contrast-mode-support", false);
// The experimental WebDriver BiDi features of the remote agent.
pref("remote.experimental.enabled", false);
// Locked, as in Firefox Release: no SharedArrayBuffer without cross-origin
// isolation.
pref("dom.postMessage.sharedArrayBuffer.bypassCOOP_COEP.insecure.enabled", false, locked);
// No remote debugging server.
pref("devtools.debugger.remote-enabled", false, locked);
// Known trackers may never reach the local network.
pref("network.lna.block_trackers", true);

// Process isolation can't be lowered by about:config or a user.js (P4.8c):
// the strongest content sandbox on macOS, and site isolation (Fission).
pref("security.sandbox.content.level", 3, locked);
pref("fission.autostart", true, locked);

// Add-ons and downloads (P4.8f). Only signed add-ons, and only from the
// profile and the application, so other software can't side-load them.
// Unlocked, because test profiles must install the unsigned harness add-ons.
pref("xpinstall.signatures.required", true);
pref("extensions.enabledScopes", 5);
pref("extensions.autoDisableScopes", 15);
// Ask what to do with file types not seen before, instead of opening them.
pref("browser.download.always_ask_before_handling_new_types", true);
pref("browser.download.manager.addToRecentDocs", false);
// Nimbus rollouts can't be turned back on (P4.8g, D30).
pref("nimbus.rollouts.enabled", false, locked);
