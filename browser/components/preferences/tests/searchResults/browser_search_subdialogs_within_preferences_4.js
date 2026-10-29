/*
 * This file contains tests for the Preferences search bar.
 */

/**
 * Test for searching for the "Location Permissions" subdialog.
 */
add_task(async function () {
  await openPreferencesViaOpenPreferencesAPI(DEFAULT_PANE, {
    leaveOpen: true,
  });
  await evaluateSearchResults("location permissions", "permissions");
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});
