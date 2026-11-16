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
  checkTrustedList,
} from "../xpcshell/risk/risk_cases.sys.mjs";

const rulesPath = fileURLToPath(
  new URL("../../m6e-torrent-rules.json", import.meta.url)
);
const rawRules = JSON.parse(readFileSync(rulesPath, "utf8"));
M6ETorrentRisk.setRules(rawRules);

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

failures.push(...checkTrustedList(M6ETorrentRisk.getTrustedSites()));
count++;
for (const site of rawRules.trustedSites) {
  count++;
  const page = new URL(site.page);
  if (
    page.protocol != "https:" ||
    !/^\d{4}-\d{2}-\d{2}$/.test(site.verified || "")
  ) {
    failures.push("trusted entry " + site.name + " needs an https page and a verified date");
  }
}

count++;
{
  const files = Array.from({ length: 4000 }, (_, index) => {
    const stem = "a" + index + ".exe";
    return { index, path: stem + " ".repeat(240 - stem.length), length: 10 };
  });
  const start = performance.now();
  assess({ name: "Tools", files }, { kind: "unknown", origin: null });
  const elapsed = performance.now() - start;
  if (elapsed > 500) {
    failures.push(
      "4000 names padded after the extension took " +
        Math.round(elapsed) +
        " ms, expected well under a second"
    );
  }
}

if (failures.length) {
  for (const failure of failures) {
    console.error("FAIL " + failure);
  }
  console.error(failures.length + " failure(s) in " + count + " cases");
  process.exit(1);
}
console.log("OK: " + count + " cases passed");
