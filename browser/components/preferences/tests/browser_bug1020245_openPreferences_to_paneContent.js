/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Test opening to the differerent panes and subcategories in Preferences

const kDefaultPane = "paneSync";
const kDefaultHash = "#sync";
const kPrivacyPane = "panePermissionsData";
const kPrivacyHash = "#permissionsData";

add_task(async function () {
  let prefs = await openPreferencesViaOpenPreferencesAPI("panePrivacy");
  is(prefs.selectedPane, "panePrivacy", "Privacy pane was selected");
  prefs = await openPreferencesViaHash("privacy");
  is(
    prefs.selectedPane,
    "panePrivacy",
    "Privacy pane is selected when hash is 'privacy'"
  );
  prefs = await openPreferencesViaOpenPreferencesAPI("nonexistant-category");
  is(
    prefs.selectedPane,
    kDefaultPane,
    "Default pane is selected when a nonexistant-category is requested"
  );
  prefs = await openPreferencesViaHash("nonexistant-category");
  is(
    prefs.selectedPane,
    kDefaultPane,
    "Default pane is selected when hash is a nonexistant-category"
  );
  prefs = await openPreferencesViaHash();
  is(prefs.selectedPane, kDefaultPane, "Default pane is selected by default");
  prefs = await openPreferencesViaOpenPreferencesAPI("privacy-reports", {
    leaveOpen: true,
  });
  is(prefs.selectedPane, kPrivacyPane, "Privacy pane is selected by default");
  let doc = gBrowser.contentDocument;
  is(
    doc.location.hash,
    kPrivacyHash,
    "The subcategory should be removed from the URI"
  );
  await TestUtils.waitForCondition(
    () => doc.querySelector("[data-subcategory~='reports']"),
    "Wait for the reports section to render."
  );
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

// Test opening Preferences with subcategory on an existing Preferences tab. See bug 1358475.
add_task(async function () {
  let prefs = await openPreferencesViaOpenPreferencesAPI(kDefaultPane, {
    leaveOpen: true,
  });
  is(prefs.selectedPane, kDefaultPane, "Default pane is selected by default");
  let doc = gBrowser.contentDocument;
  is(
    doc.location.hash,
    kDefaultHash,
    "The subcategory should be removed from the URI"
  );
  // The reasons that here just call the `openPreferences` API without the helping function are
  //   - already opened one about:preferences tab up there and
  //   - the goal is to test on the existing tab and
  //   - using `openPreferencesViaOpenPreferencesAPI` would introduce more handling of additional about:blank and unneccessary event
  await openPreferences("privacy-reports");
  let selectedPane = gBrowser.contentWindow.gLastCategory?.category;
  is(selectedPane, kPrivacyPane, "Privacy pane should be selected");
  is(
    doc.location.hash,
    kPrivacyHash,
    "The subcategory should be removed from the URI"
  );
  await TestUtils.waitForCondition(
    () => doc.querySelector("[data-subcategory~='reports']"),
    "Wait for the reports section to render."
  );
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

function openPreferencesViaHash(aPane) {
  return new Promise(resolve => {
    let finalPrefPaneLoaded = TestUtils.topicObserved(
      "sync-pane-loaded",
      () => true
    );
    gBrowser.selectedTab = BrowserTestUtils.addTab(
      gBrowser,
      "about:preferences" + (aPane ? "#" + aPane : "")
    );
    let newTabBrowser = gBrowser.selectedBrowser;

    newTabBrowser.addEventListener(
      "Initialized",
      function () {
        newTabBrowser.contentWindow.addEventListener(
          "load",
          async function () {
            let win = gBrowser.contentWindow;
            let selectedPane = win.gLastCategory?.category;
            await finalPrefPaneLoaded;
            gBrowser.removeCurrentTab();
            resolve({ selectedPane });
          },
          { once: true }
        );
      },
      { capture: true, once: true }
    );
  });
}
