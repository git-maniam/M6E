// Plain Node tests of M6ETorrentProtocol.sys.mjs: node --test <this file>

import assert from "node:assert/strict";
import { test } from "node:test";

import {
  LineFramer,
  RequestTable,
  TorrentError,
  PREF_DEFAULTS,
  MAX_TORRENT_BYTES,
  buildConfig,
  buildLimits,
  candidateFilePaths,
  classifyPref,
  encodeRequest,
  httpsOrigin,
  limitsToPrefs,
  parseBencode,
  parseTrustedSites,
  partialToPrefs,
  precheckTorrent,
  randomPeerIdPrefix,
  sanitizeSource,
  validateMagnet,
  validatePrefValue,
} from "../../../../M6ETorrentProtocol.sys.mjs";

const enc = new TextEncoder();

function bencode(value) {
  if (typeof value == "number") {
    return enc.encode(`i${value}e`);
  }
  if (typeof value == "string") {
    const bytes = enc.encode(value);
    return Uint8Array.from([...enc.encode(`${bytes.length}:`), ...bytes]);
  }
  if (value instanceof Uint8Array) {
    return Uint8Array.from([...enc.encode(`${value.length}:`), ...value]);
  }
  if (Array.isArray(value)) {
    return Uint8Array.from([
      0x6c,
      ...value.flatMap(v => [...bencode(v)]),
      0x65,
    ]);
  }
  const keys = Object.keys(value).sort();
  return Uint8Array.from([
    0x64,
    ...keys.flatMap(k => [...bencode(k), ...bencode(value[k])]),
    0x65,
  ]);
}

function torrent(info) {
  return bencode({ announce: "http://t.invalid/a", info });
}

const single = { name: "a.iso", length: 5, "piece length": 16384 };

function code(fn) {
  try {
    fn();
  } catch (e) {
    return e.code;
  }
  return null;
}

test("a plain torrent passes", () => {
  precheckTorrent(torrent(single));
  precheckTorrent(
    torrent({
      name: "dir",
      files: [
        { length: 1, path: ["a", "b.txt"] },
        { length: 2, path: ["c"] },
      ],
    })
  );
});

test("a v2 file tree passes and an unsafe key fails", () => {
  precheckTorrent(
    torrent({
      name: "v2",
      "meta version": 2,
      "file tree": { sub: { "a.txt": { "": { length: 3 } } } },
    })
  );
  assert.equal(
    code(() =>
      precheckTorrent(
        torrent({
          name: "v2",
          "file tree": { "..": { "a.txt": { "": { length: 3 } } } },
        })
      )
    ),
    "unsafe-path"
  );
});

test("unsafe paths are rejected", () => {
  const bad = ["", ".", "..", "a/b", "/abs", "a:b", "a\\b", "a\0b"];
  for (const part of bad) {
    assert.equal(
      code(() =>
        precheckTorrent(
          torrent({ name: "x", files: [{ length: 1, path: ["ok", part] }] })
        )
      ),
      "unsafe-path",
      JSON.stringify(part)
    );
    assert.equal(
      code(() => precheckTorrent(torrent({ name: part || "", length: 1 }))),
      "unsafe-path",
      "name " + JSON.stringify(part)
    );
  }
  assert.equal(
    code(() =>
      precheckTorrent(
        torrent({
          name: "x",
          files: [{ length: 1, path: ["a"], "path.utf-8": ["..", "b"] }],
        })
      )
    ),
    "unsafe-path"
  );
  assert.equal(
    code(() =>
      precheckTorrent(torrent({ name: "x", files: [{ length: 1, path: [] }] }))
    ),
    "invalid-torrent"
  );
});

test("dot names that are not dots pass", () => {
  precheckTorrent(
    torrent({ name: "x", files: [{ length: 1, path: [".hidden", "...", "a.b"] }] })
  );
});

test("an oversize file is too large", () => {
  assert.equal(
    code(() => precheckTorrent(new Uint8Array(MAX_TORRENT_BYTES + 1))),
    "too-large"
  );
  assert.equal(
    code(() => precheckTorrent(new Uint8Array(MAX_TORRENT_BYTES))),
    "invalid-torrent"
  );
});

test("nesting limit", () => {
  const nest = n => "l".repeat(n) + "e".repeat(n);
  const deep = (n, tail) =>
    enc.encode(`d4:infod4:name1:x6:lengthi1e5:extra${nest(n)}${tail}ee`);
  // Root dictionary is level 1, info is level 2, the list levels follow.
  precheckTorrent(deep(62, ""));
  assert.equal(code(() => precheckTorrent(deep(63, ""))), "invalid-torrent");
  assert.equal(code(() => precheckTorrent(deep(1000, ""))), "invalid-torrent");
  assert.equal(code(() => parseBencode(enc.encode(nest(64)))), null);
  assert.equal(code(() => parseBencode(enc.encode(nest(65)))), "invalid-torrent");
});

test("strict bencode", () => {
  const bad = [
    "i01e",
    "i-0e",
    "ie",
    "i-e",
    "i1",
    "01:a",
    "5:abc",
    "-1:",
    "d1:ai1e1:ai2ee",
    "di1ei2ee",
    "l",
    "d3:abc",
    "i1ei2e",
    "x",
    "",
    "i9999999999999999999e",
  ];
  for (const text of bad) {
    assert.equal(
      code(() => parseBencode(enc.encode(text))),
      "invalid-torrent",
      JSON.stringify(text)
    );
  }
  assert.equal(parseBencode(enc.encode("i-12e")), -12);
  assert.equal(parseBencode(enc.encode("i0e")), 0);
  assert.deepEqual([...parseBencode(enc.encode("0:"))], []);
});

test("missing pieces are invalid", () => {
  assert.equal(code(() => precheckTorrent(bencode({ a: 1 }))), "invalid-torrent");
  assert.equal(
    code(() => precheckTorrent(torrent({ name: "x" }))),
    "invalid-torrent"
  );
  assert.equal(
    code(() => precheckTorrent(torrent({ length: 1 }))),
    "invalid-torrent"
  );
  assert.equal(code(() => precheckTorrent(bencode([1]))), "invalid-torrent");
});

test("framing across chunks", () => {
  const f = new LineFramer(50);
  assert.deepEqual(f.push('{"event":"re'), []);
  assert.deepEqual(f.push('ady"}\n{"id":1,'), [{ message: { event: "ready" } }]);
  assert.deepEqual(f.push('"ok":true}\n\nnot json\n[1]\n'), [
    { message: { id: 1, ok: true } },
    { invalid: "not json" },
    { invalid: "[1]" },
  ]);
  assert.throws(() => f.push("x".repeat(60)), { code: "protocol-error" });
  const g = new LineFramer(10);
  assert.throws(() => g.push("x".repeat(20) + "\n"), { code: "protocol-error" });
});

test("framing keeps multibyte text", () => {
  const f = new LineFramer();
  const out = f.push('{"event":"log","message":"é中"}\n');
  assert.equal(out[0].message.message, "é中");
});

test("request encoding", () => {
  assert.equal(
    encodeRequest(7, "pause", { tid: "ab" }),
    '{"id":7,"op":"pause","args":{"tid":"ab"}}\n'
  );
});

test("request table correlation", async () => {
  const timers = { setTimeout, clearTimeout };
  const t = new RequestTable(timers);
  const a = t.add("add", 0);
  const b = t.add("list", 0);
  assert.notEqual(a.id, b.id);
  assert.equal(t.size, 2);
  assert.equal(t.settle({ id: b.id, ok: true, result: { torrents: [] } }), true);
  assert.deepEqual(await b.promise, { torrents: [] });
  assert.equal(t.settle({ id: 999, ok: true }), false);
  t.settle({
    id: a.id,
    ok: false,
    error: { code: "duplicate", message: "dup", tid: "abc" },
  });
  await assert.rejects(a.promise, { code: "duplicate", tid: "abc" });
  assert.equal(t.size, 0);
});

test("request table timeout and rejectAll", async () => {
  const t = new RequestTable({ setTimeout, clearTimeout });
  const a = t.add("add", 10);
  await assert.rejects(a.promise, { code: "timeout" });
  const b = t.add("x", 0);
  const c = t.add("y", 0);
  t.rejectAll(() => new TorrentError("helper-exited", "gone"));
  await assert.rejects(b.promise, { code: "helper-exited" });
  await assert.rejects(c.promise, { code: "helper-exited" });
  assert.equal(t.size, 0);
});

test("defaults map to limits and config", () => {
  const p = { ...PREF_DEFAULTS };
  assert.deepEqual(buildLimits(p), {
    downKBps: 0,
    upKBps: 0,
    turtle: { enabled: false, downKBps: 50, upKBps: 10 },
    maxActiveDownloads: 3,
    maxActiveTorrents: 5,
  });
  const c = buildConfig(p);
  assert.equal(c.encryption, "enabled");
  assert.equal(c.dht, true);
  assert.equal(c.lpd, false);
  assert.equal(c.portMapping, false);
  assert.equal(c.listenPort, 0);
  assert.equal(c.bindInterface, null);
  assert.equal(c.ipFilterPath, null);
  assert.deepEqual(c.seedGoal, { ratio: 1, hours: null });
  assert.equal(c.peerIdPrefix, null);
  assert.equal(c.userAgent, null);
  assert.deepEqual(c.proxy, {
    type: "none",
    host: "",
    port: 1080,
    username: "",
    password: "",
    peers: true,
  });
});

test("config for changed keys only", () => {
  const p = {
    ...PREF_DEFAULTS,
    "proxy.type": "socks5",
    "proxy.host": "127.0.0.1",
    seedRatioPercent: 0,
    seedHours: 12,
    randomizePeerId: true,
    bindInterface: "utun3",
  };
  const c = buildConfig(p, ["proxy", "seedGoal", "peerIdPrefix", "bindInterface"], {
    password: "pw",
    peerIdPrefix: "-AB1234-",
  });
  assert.deepEqual(Object.keys(c).sort(), [
    "bindInterface",
    "peerIdPrefix",
    "proxy",
    "seedGoal",
  ]);
  assert.equal(c.proxy.password, "pw");
  assert.equal(c.proxy.type, "socks5");
  assert.deepEqual(c.seedGoal, { ratio: null, hours: 12 });
  assert.equal(c.peerIdPrefix, "-AB1234-");
  assert.equal(c.bindInterface, "utun3");
});

test("every pref is classified or deliberately local", () => {
  const local = new Set([
    "enabled",
    "resumeAtStartup",
    "confirmQuit",
    "sequentialDefault",
    "trustedSites",
    "downloadDir",
    "askWhereToSave",
    "startNow",
  ]);
  for (const name of Object.keys(PREF_DEFAULTS)) {
    assert.equal(!!classifyPref(name), !local.has(name), name);
  }
  assert.deepEqual(classifyPref("proxy.password"), { group: "config", key: "proxy" });
});

test("pref validation", () => {
  assert.equal(validatePrefValue("downKBps", 100), 100);
  for (const [name, value] of [
    ["downKBps", -1],
    ["downKBps", 1.5],
    ["downKBps", "1"],
    ["listenPort", 70000],
    ["encryption", "maybe"],
    ["proxy.type", "http"],
    ["proxy.host", "a b"],
    ["bindInterface", "en0; rm"],
    ["ipFilterPath", "relative.txt"],
    ["downloadDir", "relative"],
    ["trustedSites", "{}"],
    ["trustedSites", "[1]"],
    ["nope", 1],
    ["dht", 1],
  ]) {
    assert.throws(() => validatePrefValue(name, value), { code: "bad-request" }, name);
  }
  assert.equal(validatePrefValue("trustedSites", '["https://a.example"]'), '["https://a.example"]');
  assert.equal(validatePrefValue("ipFilterPath", "/tmp/x.p2p"), "/tmp/x.p2p");
});

test("setConfig and setLimits arguments map to prefs", () => {
  assert.deepEqual(
    partialToPrefs({
      dht: false,
      proxy: { type: "socks5", host: "h", port: 9, password: "pw" },
      seedGoal: { ratio: 1.5, hours: null },
    }),
    {
      dht: false,
      "proxy.type": "socks5",
      "proxy.host": "h",
      "proxy.port": 9,
      "proxy.password": "pw",
      seedRatioPercent: 150,
      seedHours: 0,
    }
  );
  assert.deepEqual(
    limitsToPrefs({ downKBps: 5, turtle: { enabled: true, upKBps: 2 } }),
    { downKBps: 5, "turtle.enabled": true, "turtle.upKBps": 2 }
  );
  assert.throws(() => limitsToPrefs({ dht: false }), { code: "bad-request" });
  assert.throws(() => partialToPrefs(null), { code: "bad-request" });
});

test("magnet validation", () => {
  const hex = "a".repeat(40);
  validateMagnet(`magnet:?xt=urn:btih:${hex}&dn=x&tr=http%3A%2F%2Ft.invalid%2Fa`);
  validateMagnet(`MAGNET:?dn=x&xt=urn:btih:${"A2".repeat(16)}`);
  validateMagnet(`magnet:?xt=urn:btmh:1220${"b".repeat(64)}`);
  for (const bad of [
    "",
    "http://x",
    "magnet:?dn=x",
    `magnet:?xt=urn:btih:${"a".repeat(39)}`,
    `magnet:?xt=urn:btih:${hex}\n&dn=x`,
    `magnet:?xt=urn:btih:${hex} x`,
    `magnet:?xt=urn:btih:${hex}&dn=${"x".repeat(20000)}`,
    null,
  ]) {
    assert.throws(() => validateMagnet(bad), TorrentError, String(bad).slice(0, 40));
  }
});

test("sources are sanitized", () => {
  assert.equal(httpsOrigin("https://example.org/path?q"), "https://example.org");
  assert.equal(httpsOrigin("https://example.org:8443/x"), "https://example.org:8443");
  assert.equal(httpsOrigin("http://example.org"), null);
  assert.equal(httpsOrigin("file:///x"), null);
  assert.equal(httpsOrigin("garbage"), null);
  assert.deepEqual(sanitizeSource({ kind: "page", origin: "https://a.example/x" }), {
    kind: "page",
    origin: "https://a.example",
  });
  assert.deepEqual(sanitizeSource({ kind: "evil", origin: "http://a.example" }), {
    kind: "unknown",
    origin: null,
  });
  assert.deepEqual(sanitizeSource(undefined), { kind: "unknown", origin: null });
});

test("trusted sites parse", () => {
  assert.deepEqual(parseTrustedSites(""), []);
  assert.deepEqual(parseTrustedSites('["a"]'), ["a"]);
  assert.equal(parseTrustedSites("nope"), null);
  assert.equal(parseTrustedSites('{"a":1}'), null);
});

test("peer id prefix", () => {
  assert.match(randomPeerIdPrefix(), /^-[A-Z]{2}[0-9]{4}-$/);
  assert.equal(randomPeerIdPrefix(() => 0), "-AA0000-");
});

test("candidate file paths", () => {
  const md = { name: "dir", files: [{ path: "a/b" }, { path: "c" }] };
  assert.deepEqual(candidateFilePaths("/save/", md, md.files[0]), [
    "/save/a/b",
    "/save/dir/a/b",
  ]);
  const one = { name: "x.iso", files: [{ path: "x.iso" }] };
  assert.deepEqual(candidateFilePaths("/save", one, one.files[0]), ["/save/x.iso"]);
});
