/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { M6ETorrentRisk } = ChromeUtils.importESModule(
  "moz-src:///browser/components/torrent/M6ETorrentRisk.sys.mjs"
);
const {
  ASSESS_CASES,
  DISPLAY_CASES,
  EXCLUDED_CASES,
  TRUSTED_CASES,
  TRUST_SITE_CASES,
  checkAssess,
  checkDisplay,
  checkExcluded,
  checkRulesData,
  checkTrustSite,
  checkTrusted,
  checkTrustedList,
} = ChromeUtils.importESModule("resource://test/risk_cases.sys.mjs");

function runCases(cases, check) {
  const failures = [];
  for (const testCase of cases) {
    failures.push(...check(testCase));
  }
  return failures;
}

add_task(function test_assess_cases() {
  Assert.deepEqual(
    runCases(ASSESS_CASES, testCase =>
      checkAssess((...args) => M6ETorrentRisk.assess(...args), testCase)
    ),
    [],
    "Every assess case gives the expected result"
  );
});

add_task(function test_trusted_origins() {
  Assert.deepEqual(
    runCases(TRUSTED_CASES, testCase =>
      checkTrusted(
        (...args) => M6ETorrentRisk.isTrustedOrigin(...args),
        testCase
      )
    ),
    [],
    "Every origin case gives the expected result, with the real eTLD list"
  );
});

add_task(function test_trusted_list() {
  Assert.deepEqual(
    checkTrustedList(M6ETorrentRisk.getTrustedSites()),
    [],
    "The bundled trusted list has no duplicates and no hosting sites"
  );
  const raw = JSON.parse(
    Cu.readUTF8URI(
      Services.io.newURI(
        "moz-src:///browser/components/torrent/m6e-torrent-rules.json"
      )
    )
  );
  Assert.deepEqual(
    checkRulesData(raw, host => M6ETorrentRisk.isExcludedHost(host)),
    [],
    "The rules data has no duplicates and no excluded trusted host"
  );
});

add_task(function test_display_and_trust_helpers() {
  Assert.deepEqual(
    runCases(DISPLAY_CASES, testCase =>
      checkDisplay(
        (text, max) => M6ETorrentRisk.shortenForDisplay(text, max),
        testCase
      )
    ),
    [],
    "Long names are shortened in the middle"
  );
  Assert.deepEqual(
    runCases(TRUST_SITE_CASES, testCase =>
      checkTrustSite(origin => M6ETorrentRisk.canTrustSite(origin), testCase)
    ),
    [],
    "Public suffixes cannot be trusted, with the real eTLD list"
  );
  Assert.deepEqual(
    runCases(EXCLUDED_CASES, testCase =>
      checkExcluded(host => M6ETorrentRisk.isExcludedHost(host), testCase)
    ),
    [],
    "Hosts with other people's content are excluded"
  );
});
