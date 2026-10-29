/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* import-globals-from utilityOverlay.js */

// Services = object with smart getters for common XPCOM services
var { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

function init() {
  let defaults = Services.prefs.getDefaultBranch(null);
  let distroId = defaults.getCharPref("distribution.id", "");
  let distroAbout = defaults.getStringPref("distribution.about", "");
  // Only show distribution info when there is about text. An id-only
  // distribution is used for attribution and is shown in about:support.
  if (distroId && distroAbout) {
    let distroField = document.getElementById("distribution");
    distroField.value = distroAbout;
    distroField.style.display = "block";

    let distroVersion = defaults.getCharPref("distribution.version", "");
    if (distroVersion) {
      distroId += " - " + distroVersion;
    }

    let distroIdField = document.getElementById("distributionId");
    distroIdField.value = distroId;
    distroIdField.style.display = "block";
  }

  // Include the build ID and display warning if this is an "a#" (nightly or aurora) build
  let versionIdMap = new Map([
    ["base", "aboutDialog-version"],
    ["base-nightly", "aboutDialog-version-nightly"],
    ["base-arch", "aboutdialog-version-arch"],
    ["base-arch-nightly", "aboutdialog-version-arch-nightly"],
  ]);
  let versionIdKey = "base";
  let versionAttributes = {
    version: AppConstants.MOZ_APP_VERSION_DISPLAY,
  };

  let arch = Services.sysinfo.get("arch");
  if (["x86", "x86-64"].includes(arch)) {
    versionAttributes.bits = Services.appinfo.is64Bit ? 64 : 32;
  } else {
    versionIdKey += "-arch";
    versionAttributes.arch = arch;
  }

  let version = Services.appinfo.version;
  if (/a\d+$/.test(version)) {
    versionIdKey += "-nightly";
    let buildID = Services.appinfo.appBuildID;
    let year = buildID.slice(0, 4);
    let month = buildID.slice(4, 6);
    let day = buildID.slice(6, 8);
    versionAttributes.isodate = `${year}-${month}-${day}`;

    document.getElementById("experimental").hidden = false;
    document.getElementById("communityDesc").hidden = true;
  }

  // Use Fluent arguments for append version and the architecture of the build
  let versionField = document.getElementById("version");

  document.l10n.setAttributes(
    versionField,
    versionIdMap.get(versionIdKey),
    versionAttributes
  );

  // Show a release notes link if we have a URL.
  let relNotesLink = document.getElementById("releasenotes");
  let relNotesPrefType = Services.prefs.getPrefType(
    "app.releaseNotesURL.aboutDialog"
  );
  if (relNotesPrefType != Services.prefs.PREF_INVALID) {
    let relNotesURL = Services.urlFormatter.formatURLPref(
      "app.releaseNotesURL.aboutDialog"
    );
    if (relNotesURL != "about:blank") {
      relNotesLink.href = relNotesURL;
      relNotesLink.hidden = false;
    }
  }

  if (AppConstants.IS_ESR) {
    document.getElementById("release").hidden = false;
  }

  document
    .getElementById("aboutDialogEscapeKey")
    .addEventListener("command", () => {
      window.close();
    });
}

init();
