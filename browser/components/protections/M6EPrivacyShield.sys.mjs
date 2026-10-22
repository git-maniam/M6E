/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * M6E: the Privacy Shield (P3.5) sets the default for every site (D20). On, it
 * applies M6E's privacy hardening: strict tracking protection and the
 * hardening defaults in m6e.js. Its maximum mode adds fingerprinting
 * resistance (D18). Off, nothing is blocked and the hardening prefs have
 * Firefox's values. Total Cookie Protection stays, because it isolates
 * cookies rather than blocking them.
 *
 * Switching only changes prefs that still hold the value of the state being
 * left, so prefs the user changed by hand, and a custom tracking protection
 * setup, are kept.
 */

const PREF_CATEGORY = "browser.contentblocking.category";
const PREF_RFP = "privacy.resistFingerprinting";

/**
 * Every blocking feature of tracking protection, with its value when the
 * Shield is off.
 */
const NO_BLOCKING_VALUES = {
  "privacy.trackingprotection.enabled": false,
  "privacy.trackingprotection.pbmode.enabled": false,
  "privacy.trackingprotection.socialtracking.enabled": false,
  "privacy.trackingprotection.fingerprinting.enabled": false,
  "privacy.trackingprotection.cryptomining.enabled": false,
  "privacy.trackingprotection.emailtracking.enabled": false,
  "privacy.trackingprotection.emailtracking.pbmode.enabled": false,
  "privacy.annotate_channels.strict_list.enabled": false,
  "privacy.query_stripping.enabled": false,
  "privacy.query_stripping.enabled.pbmode": false,
  "privacy.fingerprintingProtection": false,
  "privacy.fingerprintingProtection.pbmode": false,
};

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
  NO_BLOCKING_VALUES,

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
    let blockingOff = Object.entries(NO_BLOCKING_VALUES).every(
      ([pref, value]) => getPref(pref) === value
    );
    if (enabled && (category == "standard" || blockingOff)) {
      // Setting the category applies its prefs (ContentBlockingPrefs).
      Services.prefs.setStringPref(PREF_CATEGORY, "strict");
    } else if (!enabled && category != "custom") {
      Services.prefs.setStringPref(PREF_CATEGORY, "custom");
      for (let [pref, value] of Object.entries(NO_BLOCKING_VALUES)) {
        Services.prefs.setBoolPref(pref, value);
      }
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
