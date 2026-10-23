/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// M6E: with the Privacy Shield off, HTTPS-First applies to every site, and a
// site the user turned the Tracker Blocker on for gets no plain http fallback
// (D20).

const TEST_URL = "http://untrusted.example.com/";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["m6e.privacyShield.enabled", false],
      ["dom.security.https_only_mode", false],
      ["dom.security.https_first", true],
    ],
  });
  registerCleanupFunction(() =>
    Services.perms.removeByType("m6e-protect")
  );
});

add_task(async function test_unprotected_site_falls_back_to_http() {
  await BrowserTestUtils.withNewTab("about:blank", async browser => {
    let loaded = BrowserTestUtils.browserLoaded(browser, false, null, true);
    BrowserTestUtils.startLoadingURIString(browser, TEST_URL);
    await loaded;
    is(
      browser.currentURI.scheme,
      "http",
      "HTTPS-First falls back to http for an unprotected site"
    );
  });
});

add_task(async function test_protected_site_has_no_http_fallback() {
  for (let scheme of ["http", "https"]) {
    Services.perms.addFromPrincipal(
      Services.scriptSecurityManager.createContentPrincipalFromOrigin(
        `${scheme}://untrusted.example.com`
      ),
      "m6e-protect",
      Services.perms.ALLOW_ACTION,
      Ci.nsIPermissionManager.EXPIRE_SESSION
    );
  }
  await BrowserTestUtils.withNewTab("about:blank", async browser => {
    let errorPage = BrowserTestUtils.waitForErrorPage(browser);
    BrowserTestUtils.startLoadingURIString(browser, TEST_URL);
    await errorPage;
    is(
      browser.currentURI.scheme,
      "https",
      "The protected site shows the https error instead of loading over http"
    );
  });
});
