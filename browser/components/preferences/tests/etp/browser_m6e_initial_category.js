/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// M6E: a profile without a tracking protection category starts in the one
// named by browser.contentblocking.initialCategory, and a category the user
// chooses later persists.

const { ContentBlockingPrefs } = ChromeUtils.importESModule(
  "moz-src:///browser/components/protections/ContentBlockingPrefs.sys.mjs"
);
const CAT_PREF = "browser.contentblocking.category";

function restartContentBlockingPrefs() {
  ContentBlockingPrefs.uninit();
  ContentBlockingPrefs.init();
}

add_task(async function test_initial_category() {
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("browser.contentblocking.initialCategory");
    Services.prefs.setStringPref(CAT_PREF, "standard");
  });
  Services.prefs.setStringPref(
    "browser.contentblocking.initialCategory",
    "strict"
  );

  Services.prefs.clearUserPref(CAT_PREF);
  restartContentBlockingPrefs();
  is(Services.prefs.getStringPref(CAT_PREF), "strict", "Starts in strict");
  ok(ContentBlockingPrefs.prefsMatch("strict"), "The strict prefs are set");
  ok(
    !Services.prefs.getBoolPref("privacy.fingerprintingProtection"),
    "Fingerprinting Protection is off"
  );
  ok(
    Services.prefs.getBoolPref(
      "privacy.trackingprotection.fingerprinting.enabled"
    ),
    "Known fingerprinters are blocked"
  );
  ok(
    !Services.prefs.getBoolPref("privacy.resistFingerprinting"),
    "Fingerprinting resistance stays off"
  );

  Services.prefs.setStringPref(CAT_PREF, "standard");
  restartContentBlockingPrefs();
  is(
    Services.prefs.getStringPref(CAT_PREF),
    "standard",
    "A chosen category persists"
  );
  ok(
    !Services.prefs.getBoolPref("privacy.fingerprintingProtection"),
    "Standard turns Fingerprinting Protection off"
  );
});

// Profiles from before M6E dropped Fingerprinting Protection from strict
// have it stored as a user value.
add_task(async function test_stored_fingerprinting_protection_is_reset() {
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("privacy.fingerprintingProtection");
    Services.prefs.clearUserPref("privacy.fingerprintingProtection.pbmode");
  });
  ContentBlockingPrefs.uninit();
  Services.prefs.setStringPref(CAT_PREF, "strict");
  Services.prefs.setBoolPref("privacy.fingerprintingProtection", true);
  Services.prefs.setBoolPref("privacy.fingerprintingProtection.pbmode", true);
  ContentBlockingPrefs.init();

  is(Services.prefs.getStringPref(CAT_PREF), "strict", "Still strict");
  ok(ContentBlockingPrefs.prefsMatch("strict"), "The strict prefs are set");
  ok(
    !Services.prefs.getBoolPref("privacy.fingerprintingProtection"),
    "The stored Fingerprinting Protection is off"
  );
  ok(
    !Services.prefs.getBoolPref("privacy.fingerprintingProtection.pbmode"),
    "Fingerprinting Protection is off in private windows too"
  );
});
