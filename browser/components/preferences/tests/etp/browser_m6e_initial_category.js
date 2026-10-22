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
    Services.prefs.getBoolPref("privacy.fingerprintingProtection"),
    "Fingerprinting Protection is on"
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
