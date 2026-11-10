/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { M6ETorrentRisk } = ChromeUtils.importESModule(
  "moz-src:///browser/components/torrent/M6ETorrentRisk.sys.mjs"
);
const {
  ASSESS_CASES,
  TRUSTED_CASES,
  checkAssess,
  checkTrusted,
  checkTrustedList,
} = ChromeUtils.importESModule("resource://test/risk_cases.sys.mjs");

add_task(function test_assess_cases() {
  const failures = [];
  for (const testCase of ASSESS_CASES) {
    failures.push(
      ...checkAssess(
        (...args) => M6ETorrentRisk.assess(...args),
        testCase
      )
    );
  }
  Assert.deepEqual(failures, [], "Every assess case gives the expected result");
});

add_task(function test_trusted_origins() {
  const failures = [];
  for (const testCase of TRUSTED_CASES) {
    failures.push(
      ...checkTrusted(
        (...args) => M6ETorrentRisk.isTrustedOrigin(...args),
        testCase
      )
    );
  }
  Assert.deepEqual(failures, [], "Every origin case gives the expected result");
});

add_task(function test_trusted_list() {
  Assert.deepEqual(
    checkTrustedList(M6ETorrentRisk.getTrustedSites()),
    [],
    "The bundled trusted list has no duplicates and no hosting sites"
  );
});
