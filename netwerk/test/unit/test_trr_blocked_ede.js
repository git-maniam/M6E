/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* import-globals-from head_trr.js */

// M6E D25: the default resolver, Cloudflare for Families (security), answers a
// blocked name with NOERROR, 0.0.0.0 (:: for AAAA) and extended DNS error 16
// (Censored). Such a name must fail without a native DNS lookup. Failures that
// carry no hard-fail error must still fall back to native DNS in mode 2.

const gOverride = Cc["@mozilla.org/network/native-dns-override;1"].getService(
  Ci.nsINativeDNSResolverOverride
);

// What a native lookup returns, so that a fallback shows up as a result. The
// IPv6 answer is only added for AAAA-only lookups, which ignore the IPv4 one.
const NATIVE_ANSWER = "9.9.9.9";
const NATIVE_ANSWER_V6 = "2001:db8::9";

const HARD_FAIL_CODES = [
  4, // Forged answer
  15, // Blocked
  16, // Censored
  17, // Filtered
];

let trrServer;

add_setup(async function setup() {
  trr_test_setup();
  Services.fog.initializeFOG();
  // Keep unrelated native lookups out of the native lookup counter.
  Services.prefs.setBoolPref("network.connectivity-service.enabled", false);
  Services.prefs.setBoolPref("network.trr.allow-rfc1918", false);

  trrServer = new TRRServer();
  await trrServer.start();
  Services.prefs.setIntPref("network.trr.mode", Ci.nsIDNSService.MODE_TRRFIRST);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );
});

registerCleanupFunction(async () => {
  gOverride.clearOverrides();
  Services.prefs.clearUserPref("network.connectivity-service.enabled");
  Services.prefs.clearUserPref("network.trr.hard_fail_on_extended_error");
  trr_clear_prefs();
  if (trrServer) {
    await trrServer.stop();
  }
});

function extendedError(code) {
  return [
    {
      name: ".",
      type: "OPT",
      class: "IN",
      options:
        code === undefined
          ? []
          : [{ code: "EDNS_ERROR", extended_error: code, text: "" }],
    },
  ];
}

/// Registers the A and AAAA answers of a name the resolver refuses.
///  code: extended DNS error, or undefined for none.
///  shape: "sinkhole" is NOERROR with 0.0.0.0 or ::, as Cloudflare for Families
///   answers; "empty" is NOERROR without answers; "servfail" is SERVFAIL.
async function registerBlocked(host, code, shape) {
  for (let [type, zero] of [
    ["A", "0.0.0.0"],
    ["AAAA", "::"],
  ]) {
    await trrServer.registerDoHAnswers(host, type, {
      answers:
        shape == "sinkhole"
          ? [{ name: host, ttl: 60, type, flush: false, data: zero }]
          : [],
      additionals: extendedError(code),
      flags: shape == "servfail" ? 2 : 0,
    });
  }
}

/// Registers an answer with the given addresses and an optional extended DNS
/// error for one record type.
async function registerAnswers(host, type, addresses, code) {
  await trrServer.registerDoHAnswers(host, type, {
    answers: addresses.map(data => ({
      name: host,
      ttl: 60,
      type,
      flush: false,
      data,
    })),
    additionals: extendedError(code),
  });
}

function addressesOf(record) {
  record.QueryInterface(Ci.nsIDNSAddrRecord);
  record.rewind();
  let addresses = [];
  while (record.hasMore()) {
    addresses.push(record.getNextAddrAsString());
  }
  return addresses;
}

// The native lookup counter lives in the socket process when there is one.
async function nativeLookups() {
  if (mozinfo.socketprocess_networking) {
    return null;
  }
  return (await Glean.networking.dnsNativeCount.regular.testGetValue()) ?? 0;
}

async function countNative(lookup) {
  let before = await nativeLookups();
  let result = await lookup();
  let after = await nativeLookups();
  return { result, native: before === null ? null : after - before };
}

async function expectBlocked(host, { flags = 0, type = "A" } = {}) {
  gOverride.addIPOverride(host, NATIVE_ANSWER);
  if (type == "AAAA") {
    gOverride.addIPOverride(host, NATIVE_ANSWER_V6);
  }
  let { result, native } = await countNative(
    () => new TRRDNSListener(host, { expectedSuccess: false, flags })
  );
  equal(
    result.inStatus,
    Cr.NS_ERROR_UNKNOWN_HOST,
    `${host} must fail instead of resolving through native DNS`
  );
  if (native !== null) {
    equal(native, 0, `${host} must not trigger a native lookup`);
  }
  Assert.greaterOrEqual(
    await trrServer.requestCount(host, type),
    1,
    `${host} was queried over DoH`
  );
}

async function expectFallback(host) {
  gOverride.addIPOverride(host, NATIVE_ANSWER);
  let { native } = await countNative(
    () => new TRRDNSListener(host, { expectedAnswer: NATIVE_ANSWER })
  );
  if (native !== null) {
    Assert.greater(native, 0, `${host} must fall back to a native lookup`);
  }
}

async function expectResolved(host, expected, { flags = 0 } = {}) {
  gOverride.addIPOverride(host, NATIVE_ANSWER);
  let { result, native } = await countNative(
    () => new TRRDNSListener(host, { expectedAnswer: expected[0], flags })
  );
  deepEqual(
    addressesOf(result.inRecord),
    expected,
    `${host} resolves through DoH`
  );
  if (native !== null) {
    equal(native, 0, `${host} must resolve without a native lookup`);
  }
}

async function checkHardFail(shape) {
  for (let code of HARD_FAIL_CODES) {
    let host = `ede${code}-${shape}.example.com`;
    await registerBlocked(host, code, shape);
    await expectBlocked(host);
  }
}

add_task(async function test_hard_fail_empty_answer() {
  await checkHardFail("empty");
});

add_task(async function test_hard_fail_servfail() {
  await checkHardFail("servfail");
});

add_task(async function test_hard_fail_sinkhole_answer() {
  await checkHardFail("sinkhole");
});

add_task(async function test_hard_fail_single_family() {
  for (let [label, flags, type] of [
    ["v4", Ci.nsIDNSService.RESOLVE_DISABLE_IPV6, "A"],
    ["v6", Ci.nsIDNSService.RESOLVE_DISABLE_IPV4, "AAAA"],
  ]) {
    let host = `ede16-${label}-only.example.com`;
    await registerBlocked(host, 16, "sinkhole");
    await expectBlocked(host, { flags, type });
  }
});

add_task(async function test_allowed_name_resolves() {
  let host = "allowed.example.com";
  await trrServer.registerDoHAnswers(host, "A", {
    answers: [
      { name: host, ttl: 55, type: "A", flush: false, data: "1.2.3.4" },
    ],
    additionals: extendedError(),
  });
  gOverride.addIPOverride(host, NATIVE_ANSWER);
  let { native } = await countNative(
    () => new TRRDNSListener(host, { expectedAnswer: "1.2.3.4" })
  );
  if (native !== null) {
    equal(native, 0, `${host} must resolve without a native lookup`);
  }
});

add_task(async function test_other_failures_fall_back() {
  let cases = [
    ["servfail", undefined, "servfail"],
    ["nodata", undefined, "empty"],
    ["ede0-empty", 0, "empty"], // Other
    ["ede0-servfail", 0, "servfail"],
    ["ede14-empty", 14, "empty"], // Not ready
    ["ede0-sinkhole", 0, "sinkhole"],
    ["ede14-sinkhole", 14, "sinkhole"],
    ["no-ede-sinkhole", undefined, "sinkhole"],
  ];
  for (let [name, code, shape] of cases) {
    let host = `${name}.example.com`;
    await registerBlocked(host, code, shape);
    await expectFallback(host);
  }
});

add_task(async function test_hard_fail_pref_off() {
  Services.prefs.setBoolPref("network.trr.hard_fail_on_extended_error", false);
  for (let shape of ["empty", "sinkhole"]) {
    let host = `pref-off-${shape}.example.com`;
    await registerBlocked(host, 16, shape);
    await expectFallback(host);
  }
  Services.prefs.clearUserPref("network.trr.hard_fail_on_extended_error");
});

add_task(async function test_sinkhole_address_next_to_real_address() {
  for (let [name, addresses] of [
    ["zero-first", ["0.0.0.0", "1.2.3.4"]],
    ["zero-last", ["1.2.3.4", "0.0.0.0"]],
  ]) {
    let host = `mixed-${name}.example.com`;
    await registerAnswers(host, "A", addresses, 16);
    await registerAnswers(host, "AAAA", ["::"], 16);
    await expectResolved(host, ["1.2.3.4"]);
  }

  let host = "mixed-v6.example.com";
  await registerAnswers(host, "A", ["0.0.0.0"], 16);
  await registerAnswers(host, "AAAA", ["::", "2001:db8::1"], 16);
  await expectResolved(host, ["2001:db8::1"]);
});

add_task(async function test_one_family_blocked() {
  let host = "v4-blocked.example.com";
  await registerAnswers(host, "A", ["0.0.0.0"], 16);
  await registerAnswers(host, "AAAA", ["2001:db8::2"]);
  await expectResolved(host, ["2001:db8::2"]);

  host = "v6-blocked.example.com";
  await registerAnswers(host, "A", ["1.2.3.5"]);
  await registerAnswers(host, "AAAA", ["::"], 16);
  await expectResolved(host, ["1.2.3.5"]);
});

// Only an all-zero address is skipped, so a private address still fails the
// whole answer, with or without an extended error.
add_task(async function test_private_addresses_not_allowed() {
  for (let [name, code] of [
    ["no-ede", undefined],
    ["ede16", 16],
  ]) {
    let host = `private-${name}.example.com`;
    await registerAnswers(host, "A", ["192.168.1.1"], code);
    await registerAnswers(host, "AAAA", ["fd00::1"], code);
    await expectFallback(host);
  }
});

// With network.trr.allow-rfc1918 the sinkhole address is returned as is, so
// the block degrades to a connection failure instead of a DNS failure.
add_task(async function test_private_addresses_allowed() {
  Services.prefs.setBoolPref("network.trr.allow-rfc1918", true);
  try {
    let host = "private-allowed.example.com";
    await registerAnswers(host, "A", ["192.168.1.1"]);
    await expectResolved(host, ["192.168.1.1"]);

    host = "sinkhole-allowed.example.com";
    await registerAnswers(host, "A", ["0.0.0.0"], 16);
    await expectResolved(host, ["0.0.0.0"], {
      flags: Ci.nsIDNSService.RESOLVE_DISABLE_IPV6,
    });
  } finally {
    Services.prefs.setBoolPref("network.trr.allow-rfc1918", false);
  }
});
