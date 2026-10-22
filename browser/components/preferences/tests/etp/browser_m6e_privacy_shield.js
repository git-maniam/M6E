/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// M6E: the Privacy Shield switch in Settings, Privacy (P3.5).

const { M6EPrivacyShield } = ChromeUtils.importESModule(
  "moz-src:///browser/components/protections/M6EPrivacyShield.sys.mjs"
);
const CAT_PREF = "browser.contentblocking.category";
const MANAGED_PREFS = [
  CAT_PREF,
  "privacy.resistFingerprinting",
  M6EPrivacyShield.PREF_ENABLED,
  M6EPrivacyShield.PREF_MAXIMUM,
  ...Object.keys(M6EPrivacyShield.COMPATIBILITY_VALUES),
];

add_setup(async function () {
  // Start from M6E's shipped state rather than the test profile's.
  for (let pref of MANAGED_PREFS) {
    Services.prefs.clearUserPref(pref);
  }
  Services.prefs.setStringPref(CAT_PREF, "strict");
  registerCleanupFunction(() => {
    for (let pref of MANAGED_PREFS) {
      Services.prefs.clearUserPref(pref);
    }
    Services.prefs.setStringPref(CAT_PREF, "standard");
  });
});

add_task(async function test_privacy_shield() {
  await openPreferencesViaOpenPreferencesAPI("privacy", { leaveOpen: true });
  let doc = gBrowser.selectedBrowser.contentDocument;
  let toggle = await TestUtils.waitForCondition(
    () => doc.getElementById("privacyShieldEnabled"),
    "Wait for the Privacy Shield switch"
  );
  let maximum = doc.getElementById("privacyShieldMaximum");
  ok(toggle.pressed, "The Shield starts on");
  ok(!maximum.disabled, "Maximum protection can be chosen");

  // A pref changed by hand is kept when the Shield switches.
  Services.prefs.setIntPref("network.http.referer.XOriginTrimmingPolicy", 1);

  toggle.click();
  await TestUtils.waitForCondition(
    () => Services.prefs.getStringPref(CAT_PREF) == "standard"
  );
  ok(
    !Services.prefs.getBoolPref("dom.security.https_only_mode"),
    "Off turns HTTPS-Only off"
  );
  ok(
    !Services.prefs.getBoolPref("privacy.globalprivacycontrol.enabled"),
    "Off turns GPC off"
  );
  is(
    Services.prefs.getIntPref("network.http.referer.XOriginTrimmingPolicy"),
    1,
    "A hand-changed pref is kept"
  );
  await TestUtils.waitForCondition(() => maximum.disabled);

  toggle.click();
  await TestUtils.waitForCondition(
    () => Services.prefs.getStringPref(CAT_PREF) == "strict"
  );
  ok(
    Services.prefs.getBoolPref("dom.security.https_only_mode"),
    "On turns HTTPS-Only back on"
  );

  await TestUtils.waitForCondition(() => !maximum.disabled);
  maximum.click();
  await TestUtils.waitForCondition(() =>
    Services.prefs.getBoolPref("privacy.resistFingerprinting")
  );
  ok(true, "Maximum protection resists fingerprinting");
  maximum.click();
  await TestUtils.waitForCondition(
    () => !Services.prefs.getBoolPref("privacy.resistFingerprinting")
  );

  // A custom tracking protection category is left alone.
  Services.prefs.setStringPref(CAT_PREF, "custom");
  toggle.click();
  await TestUtils.waitForCondition(() => !toggle.pressed);
  is(Services.prefs.getStringPref(CAT_PREF), "custom", "Custom is kept");

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});
