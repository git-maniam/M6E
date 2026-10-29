/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// This directory contains tests that check tips and interventions.

"use strict";

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/browser/components/profiles/tests/browser/head.js",
  this
);

ChromeUtils.defineESModuleGetters(this, {
  HttpServer: "resource://testing-common/httpd.sys.mjs",
  ResetProfile: "resource://gre/modules/ResetProfile.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  TelemetryTestUtils: "resource://testing-common/TelemetryTestUtils.sys.mjs",
  UrlbarProviderInterventions:
    "moz-src:///browser/components/urlbar/UrlbarProviderInterventions.sys.mjs",
  ProvidersManager:
    "moz-src:///browser/components/urlbar/UrlbarProvidersManager.sys.mjs",
  UrlbarResult: "chrome://browser/content/urlbar/UrlbarResult.mjs",
});

ChromeUtils.defineLazyGetter(this, "UrlbarTestUtils", () => {
  const { UrlbarTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/UrlbarTestUtils.sys.mjs"
  );
  module.init(this);
  return module;
});

ChromeUtils.defineLazyGetter(this, "SearchTestUtils", () => {
  const { SearchTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/SearchTestUtils.sys.mjs"
  );
  module.init(this);
  return module;
});

// For each intervention type, a search string that trigger the intervention.
const SEARCH_STRINGS = {
  CLEAR: "firefox history",
  REFRESH: "firefox slow",
};

/**
 * Starts a search and asserts that the second result is a tip.
 *
 * @param {string} searchString
 *   The search string.
 * @param {window} win
 *   The window.
 * @returns {(result| element)[]}
 *   The result and its element in the DOM.
 */
async function awaitTip(searchString, win = window) {
  let context = await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: win,
    value: searchString,
    waitForFocus,
    fireInputEvent: true,
  });
  Assert.greaterOrEqual(
    context.results.length,
    2,
    "Number of results is greater than or equal to 2"
  );
  let result = context.results[1];
  Assert.equal(result.type, UrlbarShared.RESULT_TYPE.TIP, "Result type");
  let element = await UrlbarTestUtils.waitForAutocompleteResultAt(win, 1);
  return [result, element];
}

/**
 * Picks the current tip's button.  The view should be open and the second
 * result should be a tip.
 */
async function pickTip() {
  let result = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);
  let button = result.element.row._buttons.get("0");
  await UrlbarTestUtils.promisePopupClose(window, () => {
    EventUtils.synthesizeMouseAtCenter(button, {});
  });
}

/**
 * Sets up the profile so that it can be reset.
 */
async function makeProfileResettable() {
  await initGroupDatabase();
  Assert.ok(
    SelectableProfileService.currentProfile,
    "Should have a profile now"
  );
}

/**
 * Starts a search that should trigger a tip, picks the tip, and waits for the
 * tip's action to happen.
 *
 * @param {object} options
 *   Options for the test
 * @param {string} options.searchString
 *   The search string.
 * @param {TIPS} options.tip
 *   The expected tip type.
 * @param {string} options.title
 *   The expected tip title.
 * @param {string} options.button
 *   The expected button title.
 * @param {Function} options.awaitCallback
 *   A function that checks the tip's action.  Should return a promise (or be
 *   async).
 * @returns {*}
 *   The value returned from `awaitCallback`.
 */
function checkIntervention({
  searchString,
  tip,
  title,
  button,
  awaitCallback,
} = {}) {
  // Opening modal dialogs confuses focus on Linux just after them, thus run
  // these checks in separate tabs to better isolate them.
  return BrowserTestUtils.withNewTab("about:blank", async () => {
    // Do a search that triggers the tip.
    let [result, element] = await awaitTip(searchString);
    Assert.strictEqual(result.payload.type, tip);
    await element.ownerDocument.l10n.translateFragment(element);

    let actualTitle = element._elements.get("title").textContent;
    if (typeof title == "string") {
      Assert.equal(actualTitle, title, "Title string");
    } else {
      // regexp
      Assert.ok(title.test(actualTitle), "Title regexp");
    }

    let actualButton = element._buttons.get("0").textContent;
    if (typeof button == "string") {
      Assert.equal(actualButton, button, "Button string");
    } else {
      // regexp
      Assert.ok(button.test(actualButton), "Button regexp");
    }

    let menuButton = element._buttons.get("result-menu");
    Assert.ok(menuButton, "Menu button exists");
    Assert.ok(BrowserTestUtils.isVisible(menuButton), "Menu button is visible");

    let values = await Promise.all([awaitCallback(), pickTip()]);
    Assert.ok(true, "Refresh dialog opened");

    // Ensure the urlbar is closed so that the engagement is ended.
    await UrlbarTestUtils.promisePopupClose(window, () => gURLBar.blur());

    return values[0] || null;
  });
}

/**
 * Starts a search and asserts that there are no tips.
 *
 * @param {string} searchString
 *   The search string.
 * @param {Window} win
 *   The host window.
 */
async function awaitNoTip(searchString, win = window) {
  let context = await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window: win,
    value: searchString,
    waitForFocus,
    fireInputEvent: true,
  });
  for (let result of context.results) {
    Assert.notEqual(result.type, UrlbarShared.RESULT_TYPE.TIP);
  }
}

/**
 * Search tips helper.  Asserts that a particular search tip is shown or that no
 * search tip is shown.
 *
 * @param {window} win
 *   A browser window.
 * @param {Values<typeof UrlbarShared.SEARCH_TIP_TYPE>} expectedTip
 *   The expected search tip.  Pass a falsey value (like zero) for none.
 * @param {boolean} closeView
 *   If true, this function closes the urlbar view before returning.
 */
async function checkTip(win, expectedTip, closeView = true) {
  if (!expectedTip) {
    // Wait a bit for the tip to not show up.
    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    await new Promise(resolve => setTimeout(resolve, 100));
    Assert.ok(!win.gURLBar.view.isOpen, "View is not open");
    return;
  }

  // Wait for the view to open, and then check the tip result.
  await UrlbarTestUtils.promisePopupOpen(win, () => {});
  Assert.ok(true, "View opened");
  Assert.equal(UrlbarTestUtils.getResultCount(win), 1, "Number of results");
  let result = await UrlbarTestUtils.getDetailsOfResultAt(win, 0);
  Assert.equal(result.type, UrlbarShared.RESULT_TYPE.TIP, "Result type");
  let heuristic;
  let title;
  let name = SearchService.defaultEngine.name;
  switch (expectedTip) {
    case UrlbarShared.SEARCH_TIP_TYPE.ONBOARD:
      heuristic = true;
      title =
        `Type less, find more: Search ${name} right from your ` +
        `address bar.`;
      break;
    case UrlbarShared.SEARCH_TIP_TYPE.REDIRECT:
      heuristic = false;
      title =
        `Start your search in the address bar to see suggestions from ` +
        `${name} and your browsing history.`;
      break;
  }
  Assert.equal(result.heuristic, heuristic, "Result is heuristic");
  Assert.equal(result.displayed.title, title, "Title");
  Assert.equal(
    result.element.row._buttons.get("0").textContent,
    "Okay, Got It",
    "Button text"
  );
  Assert.ok(
    !result.element.row._buttons.has("help"),
    "Buttons in row does not include help"
  );

  Assert.ok(
    !UrlbarTestUtils.getOneOffSearchButtonsVisible(window),
    "One-offs should be hidden when showing a search tip"
  );

  if (closeView) {
    await UrlbarTestUtils.promisePopupClose(win);
  }
}

function makeTipResult({
  buttonUrl,
  helpUrl = undefined,
  descriptionL10n = undefined,
  descriptionLearnMoreTopic = undefined,
}) {
  return new UrlbarResult({
    type: UrlbarShared.RESULT_TYPE.TIP,
    source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
    payload: {
      helpUrl,
      descriptionL10n,
      descriptionLearnMoreTopic,
      type: "test",
      titleL10n: { id: "urlbar-search-tips-confirm" },
      buttons: [
        {
          url: buttonUrl,
          l10n: { id: "urlbar-search-tips-confirm" },
        },
      ],
    },
  });
}

/**
 * Search tips helper.  Opens a foreground tab and asserts that a particular
 * search tip is shown or that no search tip is shown.
 *
 * @param {window} win
 *   A browser window.
 * @param {string} url
 *   The URL to load in a new foreground tab.
 * @param {Values<typeof UrlbarShared.SEARCH_TIP_TYPE>} expectedTip
 *   The expected search tip.  Pass a falsey value (like zero) for none.
 * @param {boolean} reset
 *   If true, the search tips provider will be reset before this function
 *   returns.  See resetSearchTipsProvider.
 */
async function checkTab(win, url, expectedTip, reset = true) {
  // BrowserTestUtils.withNewTab always waits for tab load, which hangs on
  // about:newtab for some reason, so don't use it.
  let shownCount;
  if (expectedTip) {
    shownCount = UrlbarPrefs.get(`tipShownCount.${expectedTip}`);
  }

  let tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser: win.gBrowser,
    url,
    waitForLoad: url != "about:newtab",
  });

  await checkTip(win, expectedTip, true);
  if (expectedTip) {
    Assert.equal(
      UrlbarPrefs.get(`tipShownCount.${expectedTip}`),
      shownCount + 1,
      "The shownCount pref should have been incremented by one."
    );
  }

  if (reset) {
    resetSearchTipsProvider();
  }

  BrowserTestUtils.removeTab(tab);
}

/**
 * This lets us visit www.google.com (for example) and have it redirect to
 * our test HTTP server instead of visiting the actual site.
 *
 * @param {string} domain
 *   The domain to which we are redirecting.
 * @param {string} path
 *   The pathname on the domain.
 * @param {Function} callback
 *   Executed when the test suite thinks `domain` is loaded.
 */
async function withDNSRedirect(domain, path, callback) {
  // Some domains have special security requirements, like www.bing.com.  We
  // need to override them to successfully load them.  This part is adapted from
  // testing/marionette/cert.js.
  const certOverrideService = Cc[
    "@mozilla.org/security/certoverride;1"
  ].getService(Ci.nsICertOverrideService);
  Services.prefs.setBoolPref(
    "network.stricttransportsecurity.preloadlist",
    false
  );
  Services.prefs.setIntPref("security.cert_pinning.enforcement_level", 0);
  certOverrideService.setDisableAllSecurityChecksAndLetAttackersInterceptMyData(
    true
  );

  // Now set network.dns.localDomains to redirect the domain to localhost and
  // set up an HTTP server.
  Services.prefs.setCharPref("network.dns.localDomains", domain);

  let server = new HttpServer();
  server.registerPathHandler(path, (req, resp) => {
    resp.write(`Test! http://${domain}${path}`);
  });
  server.start(-1);
  server.identity.setPrimary("http", domain, server.identity.primaryPort);
  let url = `http://${domain}:${server.identity.primaryPort}${path}`;

  await callback(url);

  // Reset network.dns.localDomains and stop the server.
  Services.prefs.clearUserPref("network.dns.localDomains");
  await new Promise(resolve => server.stop(resolve));

  // Reset the security stuff.
  certOverrideService.setDisableAllSecurityChecksAndLetAttackersInterceptMyData(
    false
  );
  Services.prefs.clearUserPref("network.stricttransportsecurity.preloadlist");
  Services.prefs.clearUserPref("security.cert_pinning.enforcement_level");
  const sss = Cc["@mozilla.org/ssservice;1"].getService(
    Ci.nsISiteSecurityService
  );
  sss.clearAll();
}

function resetSearchTipsProvider() {
  Services.prefs.clearUserPref(
    `browser.urlbar.tipShownCount.${UrlbarShared.SEARCH_TIP_TYPE.ONBOARD}`
  );
  Services.prefs.clearUserPref(
    `browser.urlbar.tipShownCount.${UrlbarShared.SEARCH_TIP_TYPE.REDIRECT}`
  );
  ProvidersManager.getInstanceForSap("urlbar").getProvider(
    "UrlbarProviderSearchTips"
  ).disableTipsForCurrentSession = false;
}

async function setDefaultEngine(name) {
  let engine = (await SearchService.getEngines()).find(e => e.name == name);
  Assert.ok(engine);
  await SearchService.setDefault(engine, SearchService.CHANGE_REASON.UNKNOWN);
}
