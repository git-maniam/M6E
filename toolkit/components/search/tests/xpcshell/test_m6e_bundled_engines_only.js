/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// M6E: Google and Bing from the bundled configuration are the only engines,
// their URLs carry no partner codes, and no other engine can be added.

"use strict";

add_setup(async function () {
  Services.prefs.setBoolPref("browser.search.bundledEnginesOnly", true);
  useHttpServer();
  await SearchService.init();
});

add_task(async function test_bundled_engines() {
  let engines = await SearchService.getVisibleEngines();
  Assert.deepEqual(
    engines.map(e => e.id),
    ["google", "bing"],
    "Should only have Google and Bing, in that order"
  );
  Assert.equal(
    SearchService.defaultEngine.id,
    "google",
    "Google should be the default"
  );
  Assert.equal(
    SearchService.defaultPrivateEngine.id,
    "google",
    "Google should be the private default"
  );
});

add_task(async function test_no_partner_codes() {
  let expected = {
    google: "https://www.google.com/search?q=test",
    bing: "https://www.bing.com/search?q=test",
  };
  for (let [id, url] of Object.entries(expected)) {
    let engine = SearchService.getEngineById(id);
    Assert.equal(
      engine.getSubmission("test").uri.spec,
      url,
      `${id} search URL should not have attribution parameters`
    );
    Assert.equal(engine.partnerCode, "", `${id} should have no partner code`);
  }
});

add_task(async function test_cannot_add_engines() {
  await Assert.rejects(
    SearchService.addUserEngine({
      name: "user",
      url: "https://example.com/user?q={searchTerms}",
    }),
    e => e.type == "not-allowed",
    "Should refuse user engines"
  );
  await Assert.rejects(
    SearchService.addOpenSearchEngine(gHttpURL + "/opensearch/generic1.xml"),
    e => e.type == "not-allowed",
    "Should refuse OpenSearch engines"
  );
  Assert.ok(
    !Services.policies.isAllowed("installSearchEngine"),
    "Should hide every add engine entry point"
  );
  Assert.equal(
    (await SearchService.getEngines()).length,
    2,
    "Should still only have two engines"
  );
});
