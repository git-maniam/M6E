/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Body for tools/m6e/chrome_eval.py: D25 end-to-end check against the real
// Cloudflare for Families (security) resolver:
//
//   ./mach python tools/m6e/chrome_eval.py --binary <app>/Contents/MacOS/m6e \
//     --script tools/m6e/d25_check.js --headless --timeout 120
//
// Every entry of `checks` in the printed JSON must be true. Set FORCE_URI only
// to test a build whose default provider is not yet the security resolver.
const FORCE_URI = null;
const NS_ERROR_UNKNOWN_HOST = 0x804b001e;

const P = Services.prefs;
const initial = {
  mode: Services.dns.currentTrrMode,
  uri: Services.dns.currentTrrURI,
  defaultProviderUri: P.getDefaultBranch("").getCharPref(
    "network.trr.default_provider_uri"
  ),
  allowRfc1918: P.getBoolPref("network.trr.allow-rfc1918"),
  hardFail: P.getBoolPref("network.trr.hard_fail_on_extended_error"),
};

if (FORCE_URI) {
  P.setCharPref("network.trr.uri", FORCE_URI);
  P.setIntPref("network.trr.mode", 2);
}
// This network inspects TLS with a Cloudflare Gateway CA kept in the macOS
// keychain; the bootstrap address keeps the resolver's own name away from the
// native-is-localhost override that makes a native fallback visible.
P.setBoolPref("security.enterprise_roots.enabled", true);
P.setCharPref("network.trr.bootstrapAddr", "1.1.1.2");
P.setCharPref("network.trr.confirmationNS", "skip");
P.setBoolPref("network.dns.native-is-localhost", true);
await sleep(1500);

const lookup = host =>
  new Promise(resolve => {
    const listener = {
      onLookupComplete(req, rec, status) {
        const out = {
          host,
          status: "0x" + (status >>> 0).toString(16),
          statusCode: status >>> 0,
          addrs: [],
        };
        if (Components.isSuccessCode(status)) {
          rec.QueryInterface(Ci.nsIDNSAddrRecord);
          out.isTRR = rec.IsTRR();
          out.trrSkipReason = rec.trrSkipReason;
          while (rec.hasMore()) {
            out.addrs.push(rec.getNextAddrAsString());
          }
        }
        resolve(out);
      },
      QueryInterface: ChromeUtils.generateQI(["nsIDNSListener"]),
    };
    Services.dns.asyncResolve(
      host,
      Ci.nsIDNSService.RESOLVE_TYPE_DEFAULT,
      Ci.nsIDNSService.RESOLVE_BYPASS_CACHE,
      null,
      listener,
      Services.tm.mainThread,
      {}
    );
  });

const results = {
  initial,
  effective: {
    mode: Services.dns.currentTrrMode,
    uri: Services.dns.currentTrrURI,
  },
};
Services.dns.clearCache(true);
results.control = await lookup("example.com");
results.malware = await lookup("malware.testcategory.com");
results.phishing = await lookup("phishing.testcategory.com");

const browser = gBrowser.selectedBrowser;
browser.fixupAndLoadURIString("http://malware.testcategory.com/", {
  triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
});
// currentURI stays on the failed URL; the error page is the document URI.
for (
  let i = 0;
  i < 40 && !browser.documentURI.spec.startsWith("about:neterror");
  i++
) {
  await sleep(500);
}
results.navigation = browser.documentURI.spec;

results.checks = {
  defaultIsSecurityResolver:
    results.initial.defaultProviderUri ==
    "https://security.cloudflare-dns.com/dns-query",
  controlResolvesThroughTRR:
    results.control.statusCode === 0 &&
    results.control.isTRR === true &&
    !results.control.addrs.includes("127.0.0.1"),
  malwareFailsWithoutFallback:
    results.malware.statusCode === NS_ERROR_UNKNOWN_HOST,
  phishingFailsWithoutFallback:
    results.phishing.statusCode === NS_ERROR_UNKNOWN_HOST,
  navigationShowsDnsNotFound:
    results.navigation.startsWith("about:neterror") &&
    results.navigation.includes("e=dnsNotFound"),
};
return results;
