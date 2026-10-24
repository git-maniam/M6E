"use strict";

var gTestTab;
var gContentAPI;

registerCleanupFunction(function () {
  Services.prefs.clearUserPref("browser.ai.control.default");
  Services.prefs.clearUserPref("browser.ai.control.smartTabGroups");
  Services.prefs.clearUserPref("browser.ai.control.pdfjsAltText");
});

add_task(setup_UITourTest);

add_UITour_task(async function test_aiControls_reflects_pref_values() {
  Services.prefs.setStringPref("browser.ai.control.default", "blocked");
  Services.prefs.setStringPref(
    "browser.ai.control.smartTabGroups",
    "available"
  );
  Services.prefs.setStringPref("browser.ai.control.pdfjsAltText", "enabled");
  let data = await getConfigurationPromise("aiControls");
  is(data.default, "blocked", "default should reflect pref value");
  is(
    data.smartTabGroups,
    "available",
    "smartTabGroups should reflect pref value"
  );
  is(data.pdfjsAltText, "enabled", "pdfjsAltText should reflect pref value");
});
