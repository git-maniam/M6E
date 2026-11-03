/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// M6E: in standard mode first-party sites get no fingerprinting protection,
// third-party fingerprinters stay blocked, and Maximum protection turns on
// full fingerprinting resistance.

const { ContentBlockingPrefs } = ChromeUtils.importESModule(
  "moz-src:///browser/components/protections/ContentBlockingPrefs.sys.mjs"
);
const { M6EPrivacyShield } = ChromeUtils.importESModule(
  "moz-src:///browser/components/protections/M6EPrivacyShield.sys.mjs"
);
const TEST_URL = "https://example.com/";
const CAT_PREF = "browser.contentblocking.category";
const FPP_PREFS = [
  "privacy.fingerprintingProtection",
  "privacy.fingerprintingProtection.pbmode",
  "privacy.baselineFingerprintingProtection",
];
const MANAGED_PREFS = [
  "privacy.resistFingerprinting",
  "browser.contentblocking.initialCategory",
  M6EPrivacyShield.PREF_ENABLED,
  M6EPrivacyShield.PREF_MAXIMUM,
  CAT_PREF,
  ...FPP_PREFS,
  ...Object.keys(M6EPrivacyShield.COMPATIBILITY_VALUES),
];

function getPref(pref) {
  switch (Services.prefs.getPrefType(pref)) {
    case Services.prefs.PREF_BOOL:
      return Services.prefs.getBoolPref(pref);
    case Services.prefs.PREF_INT:
      return Services.prefs.getIntPref(pref);
  }
  return Services.prefs.getStringPref(pref);
}

function setPref(pref, value) {
  switch (typeof value) {
    case "boolean":
      Services.prefs.setBoolPref(pref, value);
      break;
    case "number":
      Services.prefs.setIntPref(pref, value);
      break;
    default:
      Services.prefs.setStringPref(pref, value);
  }
}

add_setup(async function () {
  // Stop watching the prefs, or clearing them below would change the category.
  ContentBlockingPrefs.uninit();
  // Start from M6E's shipped values rather than the test profile's.
  let userValues = new Map();
  for (let pref of MANAGED_PREFS) {
    if (Services.prefs.prefHasUserValue(pref)) {
      userValues.set(pref, getPref(pref));
    }
    Services.prefs.clearUserPref(pref);
  }
  registerCleanupFunction(() => {
    for (let pref of MANAGED_PREFS) {
      Services.prefs.clearUserPref(pref);
    }
    for (let [pref, value] of userValues) {
      setPref(pref, value);
    }
    ContentBlockingPrefs.uninit();
    ContentBlockingPrefs.init();
  });

  Services.prefs.setStringPref(
    "browser.contentblocking.initialCategory",
    "strict"
  );
  ContentBlockingPrefs.uninit();
  ContentBlockingPrefs.init();
  is(Services.prefs.getStringPref(CAT_PREF), "strict", "Starts in strict");
  M6EPrivacyShield.apply({ enabled: true, maximum: false });
});

function readConcurrency() {
  return { concurrency: content.navigator.hardwareConcurrency };
}

function probeFirstParty() {
  return BrowserTestUtils.withNewTab(TEST_URL, browser =>
    SpecialPowers.spawn(browser, [], readConcurrency)
  );
}

add_task(async function test_standard_mode() {
  ok(ContentBlockingPrefs.prefsMatch("strict"), "The strict prefs are set");
  ok(
    Services.prefs.getBoolPref(
      "privacy.trackingprotection.fingerprinting.enabled"
    ),
    "Known third-party fingerprinters are blocked"
  );
  for (let pref of FPP_PREFS) {
    ok(!Services.prefs.getBoolPref(pref), `${pref} is off`);
  }
  ok(
    !Services.prefs.getBoolPref("privacy.resistFingerprinting"),
    "Fingerprinting resistance is off"
  );

  // The browser window is exempt from fingerprinting protection.
  let realConcurrency = window.navigator.hardwareConcurrency;
  let result = await probeFirstParty();
  is(
    result.concurrency,
    realConcurrency,
    "A first-party site sees the real hardware concurrency"
  );
});

add_task(async function test_standard_category() {
  Services.prefs.setStringPref(CAT_PREF, "standard");
  ContentBlockingPrefs.uninit();
  ContentBlockingPrefs.init();
  is(Services.prefs.getStringPref(CAT_PREF), "standard", "Standard persists");
  for (let pref of FPP_PREFS) {
    ok(!Services.prefs.getBoolPref(pref), `${pref} is off in Standard`);
  }
  Services.prefs.setStringPref(CAT_PREF, "strict");
  ContentBlockingPrefs.uninit();
  ContentBlockingPrefs.init();
});

add_task(async function test_maximum_mode() {
  M6EPrivacyShield.apply({ enabled: true, maximum: true });
  ok(
    Services.prefs.getBoolPref("privacy.resistFingerprinting"),
    "Fingerprinting resistance is on"
  );
  ok(
    Services.prefs.getBoolPref(
      "privacy.trackingprotection.fingerprinting.enabled"
    ),
    "Known third-party fingerprinters are still blocked"
  );

  await BrowserTestUtils.withNewTab(TEST_URL, async browser => {
    let concurrency = await SpecialPowers.spawn(
      browser,
      [],
      () => content.navigator.hardwareConcurrency
    );
    is(concurrency, 8, "A first-party site sees the spoofed concurrency");
  });

  M6EPrivacyShield.apply({ enabled: true, maximum: false });
  ok(
    !Services.prefs.getBoolPref("privacy.resistFingerprinting"),
    "Standard mode turns fingerprinting resistance off again"
  );
});
