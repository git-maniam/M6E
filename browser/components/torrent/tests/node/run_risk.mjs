/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Runs the Shady Torrent cases against the pure logic under plain Node:
//   node browser/components/torrent/tests/node/run_risk.mjs

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";

import { M6ETorrentRisk } from "../../M6ETorrentRisk.sys.mjs";
import {
  ASSESS_CASES,
  TRUSTED_CASES,
  checkAssess,
  checkTrusted,
} from "../xpcshell/risk/risk_cases.sys.mjs";

const rulesPath = fileURLToPath(
  new URL("../../m6e-torrent-rules.json", import.meta.url)
);
M6ETorrentRisk.setRules(JSON.parse(readFileSync(rulesPath, "utf8")));

const assess = (...args) => M6ETorrentRisk.assess(...args);
const isTrusted = (...args) => M6ETorrentRisk.isTrustedOrigin(...args);

const failures = [];
let count = 0;
for (const testCase of ASSESS_CASES) {
  count++;
  failures.push(...checkAssess(assess, testCase));
}
for (const testCase of TRUSTED_CASES) {
  count++;
  failures.push(...checkTrusted(isTrusted, testCase));
}

if (failures.length) {
  for (const failure of failures) {
    console.error("FAIL " + failure);
  }
  console.error(failures.length + " failure(s) in " + count + " cases");
  process.exit(1);
}
console.log("OK: " + count + " cases passed");
