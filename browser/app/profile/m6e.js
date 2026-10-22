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
