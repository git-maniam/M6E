/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * M6E: the Privacy Shield (P3.5) switches between M6E's privacy hardening
 * (strict tracking protection and the hardening defaults in m6e.js) and a
 * compatibility set for sites that break. Its maximum mode adds fingerprinting
 * resistance (D18).
 *
 * Switching only changes prefs that still hold the value of the state being
 * left, so prefs the user changed by hand are kept.
 */

const PREF_CATEGORY = "browser.contentblocking.category";
const PREF_RFP = "privacy.resistFingerprinting";

/**
 * The hardening prefs from m6e.js with their Firefox values, which the
 * compatibility set uses. Keep in sync with m6e.js and
 * testing/profiles/base/user.js.
 */
const COMPATIBILITY_VALUES = {
  "dom.security.https_only_mode": false,
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

export const M6EPrivacyShield = {
  PREF_ENABLED: "m6e.privacyShield.enabled",
  PREF_MAXIMUM: "m6e.privacyShield.maximum",
  COMPATIBILITY_VALUES,

  /**
   * Applies the Shield's state to the privacy prefs.
   *
   * @param {object} state
   * @param {boolean} state.enabled Whether the Shield is on.
   * @param {boolean} state.maximum Whether fingerprinting resistance is on.
   *   Only applies while the Shield is on.
   */
  apply({ enabled, maximum }) {
    let category = Services.prefs.getStringPref(PREF_CATEGORY, "standard");
    if (enabled && category == "standard") {
      Services.prefs.setStringPref(PREF_CATEGORY, "strict");
    } else if (!enabled && category == "strict") {
      Services.prefs.setStringPref(PREF_CATEGORY, "standard");
    }

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
};
