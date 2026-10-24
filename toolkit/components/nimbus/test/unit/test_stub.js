"use strict";

const { ExperimentAPI, NimbusFeatures, EnrollmentType } =
  ChromeUtils.importESModule("resource://nimbus/ExperimentAPI.sys.mjs");

const PREF = "browser.urlbar.quicksuggest.rankingMode";

add_task(async function test_never_enabled() {
  Assert.ok(!ExperimentAPI.enabled);
  Assert.ok(!ExperimentAPI.labsEnabled);
  Assert.ok(!ExperimentAPI.rolloutsEnabled);
  Assert.ok(!ExperimentAPI.studiesEnabled);
  Assert.ok(!(await ExperimentAPI.init()));
  await ExperimentAPI.ready();
  await ExperimentAPI.updateRecipesForFeatures("test", ["urlbar"]);
  Assert.equal(EnrollmentType.EXPERIMENT, "experiment");
  Assert.equal(EnrollmentType.ROLLOUT, "rollout");
});

add_task(async function test_no_enrollments() {
  for (let id of ["urlbar", "aboutwelcome", "password-autocomplete", "other"]) {
    let feature = NimbusFeatures[id];
    Assert.strictEqual(feature.getEnrollmentMetadata(), null, id);
    Assert.deepEqual(feature.getAllEnrollments(), [], id);
    Assert.deepEqual(feature.getAllEnrollmentMetadata(), [], id);
    Assert.strictEqual(feature.getVariable("noSuchVariable"), undefined, id);
    feature.recordExposureEvent({ once: true });
    await feature.ready();
  }
});

add_task(async function test_fallback_prefs() {
  registerCleanupFunction(() => Services.prefs.clearUserPref(PREF));
  Services.prefs.setStringPref(PREF, "one");

  let feature = NimbusFeatures.urlbar;
  Assert.equal(feature.getVariable("quickSuggestRankingMode"), "one");
  Assert.equal(feature.getAllVariables().quickSuggestRankingMode, "one");
  Assert.equal(
    feature.getAllVariables({ defaultValues: { extra: 1 } }).extra,
    1
  );
  Assert.equal(
    NimbusFeatures.other.getAllVariables({ defaultValues: { a: 2 } }).a,
    2
  );
});

add_task(async function test_onUpdate() {
  let feature = NimbusFeatures.urlbar;
  Assert.equal(feature.getVariable("quickSuggestRankingMode"), "one");

  let calls = [];
  let callback = (...args) => calls.push(args);
  feature.onUpdate(callback);

  Services.prefs.setStringPref(PREF, "two");
  Assert.deepEqual(calls, [["featureUpdate:urlbar", "pref-updated"]]);
  Assert.equal(feature.getVariable("quickSuggestRankingMode"), "two");

  feature.offUpdate(callback);
  Services.prefs.setStringPref(PREF, "three");
  Assert.equal(calls.length, 1);
});
