/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This is useful for testing a pref on try.
/* globals user_pref */
// ensure webrender is set (and we don't need MOZ_WEBRENDER env variable)
user_pref("gfx.webrender.all", true);

// ensure WebGL is allowed in the parent process for no e10s/GPU process tests
user_pref("webgl.allow-in-parent", true);

user_pref("dom.input_events.security.minNumTicks", 0);
user_pref("dom.input_events.security.minTimeElapsedInMS", 0);

// Set address autofill to true for tests
user_pref("extensions.formautofill.addresses.experiments.enabled", true);

// Turn off update
user_pref("app.update.disabledForTesting", true);

// Browser restarts can cause the session restore suggestion to be shown when reusing a
// profile across a set of tests. Avoid showing this infobar by default.
user_pref("browser.startup.couldRestoreSession.count", -1);

// M6E turns on DNS over HTTPS by default (m6e.js), which would make tests
// contact the resolver.
user_pref("network.trr.mode", 0);
// M6E starts new profiles in strict tracking protection; tests expect
// Firefox's standard defaults.
user_pref("browser.contentblocking.initialCategory", "");
// M6E has no baseline fingerprinting protection in standard mode (m6e.js);
// tests expect Firefox's default.
user_pref("privacy.baselineFingerprintingProtection", true);
user_pref("privacy.fingerprintingProtection.pbmode", true);
// M6E's privacy hardening (m6e.js, P3.4) at Firefox's values, so tests run
// against the behaviour they were written for. HTTPS-Only would otherwise
// block the test harness, which loads http://mochi.test.
user_pref("dom.security.https_only_mode", false);
user_pref("privacy.globalprivacycontrol.enabled", false);
user_pref("network.http.referer.XOriginTrimmingPolicy", 0);
user_pref("media.peerconnection.ice.default_address_only", false);
user_pref("media.peerconnection.ice.proxy_only_if_behind_proxy", false);
user_pref("security.tls.enable_0rtt_data", true);
user_pref("security.ssl.require_safe_negotiation", false);
// M6E's download hardening (m6e.js, P4.8f) at Firefox's values.
user_pref("browser.download.always_ask_before_handling_new_types", false);
user_pref("browser.download.manager.addToRecentDocs", true);
user_pref("browser.xul.error_pages.expert_bad_cert", false);
user_pref("network.IDN_show_punycode", false);
user_pref("pdfjs.enableScripting", true);
user_pref("network.auth.subresource-http-auth-allow", 2);
user_pref("dom.disable_window_move_resize", false);
// M6E's BitTorrent defaults (m6e.js, P8.5), pinned so that a test profile
// never starts the engine, opens a port or contacts anything by itself. Keep
// in sync with m6e.js and PREF_DEFAULTS in M6ETorrentProtocol.sys.mjs.
user_pref("m6e.torrent.enabled", true);
user_pref("m6e.torrent.resumeAtStartup", true);
user_pref("m6e.torrent.confirmQuit", true);
user_pref("m6e.torrent.encryption", "enabled");
user_pref("m6e.torrent.dht", true);
user_pref("m6e.torrent.pex", true);
user_pref("m6e.torrent.lpd", false);
user_pref("m6e.torrent.portMapping", false);
user_pref("m6e.torrent.listenPort", 0);
user_pref("m6e.torrent.downKBps", 0);
user_pref("m6e.torrent.upKBps", 0);
user_pref("m6e.torrent.turtle.enabled", false);
user_pref("m6e.torrent.turtle.downKBps", 50);
user_pref("m6e.torrent.turtle.upKBps", 10);
user_pref("m6e.torrent.maxActiveDownloads", 3);
user_pref("m6e.torrent.maxActiveTorrents", 5);
user_pref("m6e.torrent.seedRatioPercent", 100);
user_pref("m6e.torrent.seedHours", 0);
user_pref("m6e.torrent.proxy.type", "none");
user_pref("m6e.torrent.proxy.host", "");
user_pref("m6e.torrent.proxy.port", 1080);
user_pref("m6e.torrent.proxy.username", "");
user_pref("m6e.torrent.proxy.peers", true);
user_pref("m6e.torrent.bindInterface", "");
user_pref("m6e.torrent.ipFilterPath", "");
user_pref("m6e.torrent.randomizePeerId", false);
user_pref("m6e.torrent.sequentialDefault", false);
user_pref("m6e.torrent.trustedSites", "");
user_pref("m6e.torrent.downloadDir", "");
user_pref("m6e.torrent.askWhereToSave", false);
user_pref("m6e.torrent.startNow", true);
