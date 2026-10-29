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

add_task(async function test_notifyUpdate() {
  let feature = NimbusFeatures.urlbar;
  let calls = [];
  let callback = (...args) => calls.push(args);
  feature.onUpdate(callback);
  feature.notifyUpdate();
  feature.offUpdate(callback);
  Assert.deepEqual(calls, [["featureUpdate:urlbar", "pref-updated"]]);
});

add_task(async function test_feature_override() {
  const { overrideFeature } = ChromeUtils.importESModule(
    "resource://testing-common/FeatureOverride.sys.mjs"
  );
  Services.prefs.setStringPref(PREF, "one");

  let feature = NimbusFeatures.urlbar;
  let cleanup = await overrideFeature({
    featureId: "urlbar",
    value: { quickSuggestRankingMode: "two", other: 3 },
  });
  Assert.equal(feature.getVariable("quickSuggestRankingMode"), "two");
  Assert.equal(feature.getVariable("other"), 3);
  Assert.equal(feature.getAllVariables().other, 3);

  await cleanup();
  Assert.equal(feature.getVariable("quickSuggestRankingMode"), "one");
  Assert.strictEqual(feature.getVariable("other"), undefined);
});

add_task(async function test_no_network_or_telemetry_surface() {
  let source = await (
    await fetch("resource://nimbus/ExperimentAPI.sys.mjs")
  ).text();
  for (let token of [
    "RemoteSettings",
    "remote-settings",
    "fetch(",
    "XMLHttpRequest",
    "Glean",
    "Telemetry",
    "ServerSocket",
    "setStringPref",
    "setBoolPref",
    "setIntPref",
    "setCharPref",
  ]) {
    Assert.ok(!source.includes(token), `Stub source does not use ${token}`);
  }
  Assert.deepEqual(
    [...source.matchAll(/^import .*$/gm)].map(m => m[0]),
    ['import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";'],
    "Stub imports only XPCOMUtils"
  );

  for (let name of [
    "ExperimentManager",
    "ExperimentStore",
    "RemoteSettingsExperimentLoader",
  ]) {
    // Importing a missing module crashes in automation (CheckForBrokenChromeURL),
    // so check that the file does not exist.
    await Assert.rejects(
      fetch(`resource://nimbus/lib/${name}.sys.mjs`),
      /NetworkError|NS_ERROR|Failed/,
      `${name} does not exist`
    );
  }
});
