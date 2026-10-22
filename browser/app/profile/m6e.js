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
