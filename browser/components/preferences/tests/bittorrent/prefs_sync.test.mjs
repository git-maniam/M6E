// Plain Node test: node --test <this file>
// The m6e.torrent.* prefs of m6e.js, the test profile (user.js) and
// PREF_DEFAULTS of M6ETorrentProtocol.sys.mjs must agree.

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";

import { PREF_DEFAULTS } from "../../../torrent/M6ETorrentProtocol.sys.mjs";

const ROOT = new URL("../../../../../", import.meta.url);

function readPrefs(path, call) {
  const text = readFileSync(new URL(path, ROOT), "utf8");
  const out = {};
  const re = new RegExp(
    `^${call}\\("m6e\\.torrent\\.([^"]+)",\\s*(.+?)(?:,\\s*locked)?\\);\\s*$`,
    "gm"
  );
  for (const [, name, value] of text.matchAll(re)) {
    assert.ok(!(name in out), `${name} is set twice in ${path}`);
    out[name] = JSON.parse(value);
  }
  return out;
}

test("m6e.js matches PREF_DEFAULTS", () => {
  const prefs = readPrefs("browser/app/profile/m6e.js", "pref");
  assert.deepEqual(prefs, PREF_DEFAULTS);
});

test("the test profile matches PREF_DEFAULTS", () => {
  const prefs = readPrefs("testing/profiles/base/user.js", "user_pref");
  assert.deepEqual(prefs, PREF_DEFAULTS);
});

test("the privacy defaults of D36", () => {
  assert.equal(PREF_DEFAULTS.portMapping, false);
  assert.equal(PREF_DEFAULTS.lpd, false);
  assert.equal(PREF_DEFAULTS.dht, true);
  assert.equal(PREF_DEFAULTS.pex, true);
  assert.equal(PREF_DEFAULTS.listenPort, 0);
  assert.equal(PREF_DEFAULTS.seedRatioPercent, 100);
  assert.equal(PREF_DEFAULTS.seedHours, 0);
  assert.equal(PREF_DEFAULTS["proxy.type"], "none");
  assert.equal(PREF_DEFAULTS.bindInterface, "");
  assert.equal(PREF_DEFAULTS.ipFilterPath, "");
  assert.equal(PREF_DEFAULTS.randomizePeerId, false);
});

test("the proxy password is not a pref", () => {
  assert.ok(!Object.keys(PREF_DEFAULTS).some(n => /password/i.test(n)));
});
