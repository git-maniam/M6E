"use strict";

var gTestTab;
var gContentAPI;

registerCleanupFunction(function () {
  Services.prefs.clearUserPref("browser.ai.control.default");
});

add_task(setup_UITourTest);

add_UITour_task(async function test_aiControls_reflects_pref_values() {
  Services.prefs.setStringPref("browser.ai.control.default", "blocked");
  let data = await getConfigurationPromise("aiControls");
  is(data.default, "blocked", "default should reflect pref value");
});
