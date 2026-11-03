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
  await BrowserTestUtils.withNewTab(TEST_URL, async browser => {
    // Canvas randomization only alters canvases that aren't a flat colour, so
    // draw two flat rectangles and count the pixels that don't read back as
    // drawn.
    let result = await SpecialPowers.spawn(browser, [], () => {
      let canvas = content.document.createElement("canvas");
      canvas.width = 100;
      canvas.height = 100;
      let context = canvas.getContext("2d");
      context.fillStyle = "#EE2222";
      context.fillRect(0, 0, 100, 100);
      context.fillStyle = "#2222EE";
      context.fillRect(20, 20, 60, 60);
      let data = context.getImageData(0, 0, 100, 100).data;
      let altered = 0;
      for (let y = 0; y < 100; y++) {
        for (let x = 0; x < 100; x++) {
          let inner = x >= 20 && x < 80 && y >= 20 && y < 80;
          let expected = inner
            ? [0x22, 0x22, 0xee, 255]
            : [0xee, 0x22, 0x22, 255];
          let i = (y * 100 + x) * 4;
          if (expected.some((value, offset) => data[i + offset] != value)) {
            altered++;
          }
        }
      }
      return { altered, concurrency: content.navigator.hardwareConcurrency };
    });
    is(result.altered, 0, "A first-party canvas reads back as drawn");
    is(
      result.concurrency,
      realConcurrency,
      "A first-party site sees the real hardware concurrency"
    );
  });
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
