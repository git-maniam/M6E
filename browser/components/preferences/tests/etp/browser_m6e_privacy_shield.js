/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// M6E: the Privacy Shield switch in Settings, Privacy (P3.5), and the Tracker
// Blocker's two-way, session-only per-site override of it (D20).

const { M6EPrivacyShield } = ChromeUtils.importESModule(
  "moz-src:///browser/components/protections/M6EPrivacyShield.sys.mjs"
);
const TEST_URL = "https://example.com/";
const MANAGED_PREFS = [
  "privacy.resistFingerprinting",
  M6EPrivacyShield.PREF_ENABLED,
  M6EPrivacyShield.PREF_MAXIMUM,
  ...Object.keys(M6EPrivacyShield.COMPATIBILITY_VALUES),
];

add_setup(async function () {
  // Start from M6E's shipped values rather than the test profile's.
  for (let pref of MANAGED_PREFS) {
    Services.prefs.clearUserPref(pref);
  }
  registerCleanupFunction(() => {
    for (let pref of MANAGED_PREFS) {
      Services.prefs.clearUserPref(pref);
    }
    Services.perms.removeByType(M6EPrivacyShield.PROTECT_PERMISSION);
    Services.perms.removeByType("trackingprotection");
    Services.perms.removeByType("https-only-load-insecure");
  });
});

async function isOnAllowList(browser) {
  return SpecialPowers.spawn(
    browser,
    [],
    () => content.document.cookieJarSettings.isOnContentBlockingAllowList
  );
}

async function reload(browser) {
  let loaded = BrowserTestUtils.browserLoaded(browser);
  browser.reload();
  await loaded;
}

add_task(async function test_privacy_shield_setting() {
  await openPreferencesViaOpenPreferencesAPI("privacy", { leaveOpen: true });
  let doc = gBrowser.selectedBrowser.contentDocument;
  let toggle = await TestUtils.waitForCondition(
    () => doc.getElementById("privacyShieldEnabled"),
    "Wait for the Privacy Shield switch"
  );
  let maximum = doc.getElementById("privacyShieldMaximum");
  ok(toggle.pressed, "The Shield starts on");

  // A pref changed by hand is kept when the Shield switches.
  Services.prefs.setIntPref("network.http.referer.XOriginTrimmingPolicy", 1);

  toggle.click();
  await TestUtils.waitForCondition(() => !toggle.pressed);
  ok(!M6EPrivacyShield.enabled, "Off is stored");
  ok(
    !Services.prefs.getBoolPref("privacy.globalprivacycontrol.enabled"),
    "Off switches the global-only hardening"
  );
  is(
    Services.prefs.getIntPref("network.http.referer.XOriginTrimmingPolicy"),
    1,
    "A hand-changed pref is kept"
  );
  await TestUtils.waitForCondition(() => maximum.disabled);

  toggle.click();
  await TestUtils.waitForCondition(() => toggle.pressed);
  ok(
    Services.prefs.getBoolPref("privacy.globalprivacycontrol.enabled"),
    "On restores the hardening"
  );

  await TestUtils.waitForCondition(() => !maximum.disabled);
  maximum.click();
  await TestUtils.waitForCondition(() =>
    Services.prefs.getBoolPref("privacy.resistFingerprinting")
  );
  maximum.click();
  await TestUtils.waitForCondition(
    () => !Services.prefs.getBoolPref("privacy.resistFingerprinting")
  );

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function test_site_override_with_shield_on() {
  await BrowserTestUtils.withNewTab(TEST_URL, async browser => {
    ok(M6EPrivacyShield.isSiteProtected(browser), "Protected by default");
    ok(!(await isOnAllowList(browser)), "Not exempt");

    M6EPrivacyShield.setSiteProtected(browser, false);
    await reload(browser);
    ok(!M6EPrivacyShield.isSiteProtected(browser), "Turned off for the site");
    ok(await isOnAllowList(browser), "The site is exempt");
    let perm = Services.perms.getPermissionObject(
      browser.contentPrincipal,
      "trackingprotection",
      true
    );
    is(
      perm.expireType,
      Ci.nsIPermissionManager.EXPIRE_SESSION,
      "Only for the session"
    );

    M6EPrivacyShield.setSiteProtected(browser, true);
    await reload(browser);
    ok(!(await isOnAllowList(browser)), "Protected again");
  });
});

add_task(async function test_site_override_with_shield_off() {
  Services.prefs.setBoolPref(M6EPrivacyShield.PREF_ENABLED, false);
  await BrowserTestUtils.withNewTab(TEST_URL, async browser => {
    ok(!M6EPrivacyShield.isSiteProtected(browser), "Unprotected by default");
    ok(await isOnAllowList(browser), "Every site is exempt");

    M6EPrivacyShield.setSiteProtected(browser, true);
    await reload(browser);
    ok(M6EPrivacyShield.isSiteProtected(browser), "Turned on for the site");
    ok(!(await isOnAllowList(browser)), "The site is protected");
    let perm = Services.perms.getPermissionObject(
      browser.contentPrincipal,
      M6EPrivacyShield.PROTECT_PERMISSION,
      true
    );
    is(
      perm.expireType,
      Ci.nsIPermissionManager.EXPIRE_SESSION,
      "Only for the session"
    );

    M6EPrivacyShield.setSiteProtected(browser, false);
    await reload(browser);
    ok(await isOnAllowList(browser), "Exempt again");
  });
  Services.prefs.clearUserPref(M6EPrivacyShield.PREF_ENABLED);
});
