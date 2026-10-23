/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * M6E: the Privacy Shield (P3.5) sets the default for every site, and the
 * Tracker Blocker's per-site switch overrides it for the session (D20).
 *
 * The blocking prefs keep their strict values whichever way the Shield is
 * set. With the Shield off, C++ hooks treat every site as exempt from
 * tracking protection and fingerprinting protection, unless the site has the
 * "m6e-protect" permission:
 * - ContentBlockingAllowList::Check();
 * - nsContentUtils::ETPSaysShouldNotResistFingerprinting().
 * HTTPS-Only falls back to Firefox's HTTPS-First, and
 * nsHTTPSOnlyUtils::PotentiallyDowngradeHttpsFirstRequest() refuses the
 * plain http fallback for "m6e-protect" sites.
 * The hardening prefs that only apply globally are switched here instead.
 *
 * Switching the Shield only changes prefs that still hold the value of the
 * state being left, so prefs the user changed by hand are kept.
 */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  ContentBlockingAllowList:
    "resource://gre/modules/ContentBlockingAllowList.sys.mjs",
});

const PREF_ENABLED = "m6e.privacyShield.enabled";
const PREF_RFP = "privacy.resistFingerprinting";
const PROTECT_PERMISSION = "m6e-protect";
const HTTPS_ONLY_PERMISSION = "https-only-load-insecure";

/**
 * The hardening prefs from m6e.js that can't apply per site, with their
 * Firefox values, which the Shield uses when it is off. Keep in sync with
 * m6e.js and testing/profiles/base/user.js.
 */
const COMPATIBILITY_VALUES = {
  "privacy.globalprivacycontrol.enabled": false,
  "network.http.referer.XOriginTrimmingPolicy": 0,
  "media.peerconnection.ice.default_address_only": false,
  "media.peerconnection.ice.proxy_only_if_behind_proxy": false,
  "security.tls.enable_0rtt_data": true,
  "security.ssl.require_safe_negotiation": false,
  "network.IDN_show_punycode": false,
  "pdfjs.enableScripting": true,
  "network.auth.subresource-http-auth-allow": 2,
  "dom.disable_window_move_resize": false,
  "dom.security.https_only_mode": false,
};

function getPref(pref, branch = Services.prefs) {
  switch (branch.getPrefType(pref)) {
    case branch.PREF_BOOL:
      return branch.getBoolPref(pref);
    case branch.PREF_INT:
      return branch.getIntPref(pref);
    case branch.PREF_STRING:
      return branch.getStringPref(pref);
  }
  return undefined;
}

function setPref(pref, value) {
  if (typeof value == "boolean") {
    Services.prefs.setBoolPref(pref, value);
  } else if (typeof value == "number") {
    Services.prefs.setIntPref(pref, value);
  } else {
    Services.prefs.setStringPref(pref, value);
  }
}

/**
 * The http and https principals of a site, since permissions are per origin
 * and HTTPS-Only checks the http one.
 *
 * @param {nsIPrincipal} principal
 * @returns {nsIPrincipal[]}
 */
function sitePrincipals(principal) {
  return ["http", "https"].map(scheme =>
    Services.scriptSecurityManager.createContentPrincipal(
      Services.io.newURI(`${scheme}://${principal.hostPort}`),
      principal.originAttributes
    )
  );
}

export const M6EPrivacyShield = {
  PREF_ENABLED,
  PREF_MAXIMUM: "m6e.privacyShield.maximum",
  PROTECT_PERMISSION,
  COMPATIBILITY_VALUES,

  get enabled() {
    return Services.prefs.getBoolPref(PREF_ENABLED, true);
  },

  /**
   * Applies the Shield's state to the global privacy prefs.
   *
   * @param {object} state
   * @param {boolean} state.enabled Whether the Shield is on.
   * @param {boolean} state.maximum Whether fingerprinting resistance is on.
   *   Only applies while the Shield is on.
   */
  apply({ enabled, maximum }) {
    let defaults = Services.prefs.getDefaultBranch("");
    for (let [pref, compatValue] of Object.entries(COMPATIBILITY_VALUES)) {
      let value = getPref(pref);
      if (enabled && value === compatValue) {
        Services.prefs.clearUserPref(pref);
      } else if (!enabled && value === getPref(pref, defaults)) {
        setPref(pref, compatValue);
      }
    }

    if (enabled && maximum) {
      Services.prefs.setBoolPref(PREF_RFP, true);
    } else {
      Services.prefs.clearUserPref(PREF_RFP);
    }
  },

  /**
   * Whether the Tracker Blocker protects the site shown in a browser.
   *
   * @param {MozBrowser} browser
   * @returns {boolean}
   */
  isSiteProtected(browser) {
    if (!lazy.ContentBlockingAllowList.canHandle(browser)) {
      return true;
    }
    if (lazy.ContentBlockingAllowList.includes(browser)) {
      return false;
    }
    if (this.enabled) {
      return true;
    }
    let principal =
      lazy.ContentBlockingAllowList._basePrincipalForAntiTrackingCommon(
        browser
      );
    return (
      Services.perms.testPermissionFromPrincipal(
        principal,
        PROTECT_PERMISSION
      ) == Services.perms.ALLOW_ACTION
    );
  },

  /**
   * Turns the Tracker Blocker on or off for the site shown in a browser,
   * until the session ends. With the Shield on, off adds the tracking
   * protection and HTTPS-Only exceptions; with the Shield off, on adds the
   * "m6e-protect" permission.
   *
   * @param {MozBrowser} browser
   * @param {boolean} protect
   */
  setSiteProtected(browser, protect) {
    let principal =
      lazy.ContentBlockingAllowList._basePrincipalForAntiTrackingCommon(
        browser
      );
    if (!principal) {
      return;
    }
    // Clear the overrides of both Shield states first, since the Shield may
    // have been switched since they were added.
    lazy.ContentBlockingAllowList.remove(browser);
    let principals = sitePrincipals(principal);
    for (let p of principals) {
      Services.perms.removeFromPrincipal(p, HTTPS_ONLY_PERMISSION);
      Services.perms.removeFromPrincipal(p, PROTECT_PERMISSION);
    }

    let session = Ci.nsIPermissionManager.EXPIRE_SESSION;
    if (this.enabled && !protect) {
      lazy.ContentBlockingAllowList.add(browser);
      for (let p of principals) {
        Services.perms.addFromPrincipal(
          p,
          HTTPS_ONLY_PERMISSION,
          Ci.nsIHttpsOnlyModePermission.LOAD_INSECURE_ALLOW_SESSION,
          session
        );
      }
    } else if (!this.enabled && protect) {
      for (let p of principals) {
        Services.perms.addFromPrincipal(
          p,
          PROTECT_PERMISSION,
          Services.perms.ALLOW_ACTION,
          session
        );
      }
    }
  },
};
