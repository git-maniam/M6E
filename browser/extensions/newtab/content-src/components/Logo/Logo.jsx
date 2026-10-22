/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React from "react";

// M6E: the home page shows the M6E splash image above the search box, in
// place of the logo and wordmark.
function Logo() {
  return (
    <h1 className="logo-and-wordmark-wrapper">
      <div
        className="logo-and-wordmark"
        role="img"
        data-l10n-id="newtab-logo-and-wordmark"
      >
        <img
          className="m6e-home-splash"
          src="chrome://newtab/content/data/content/assets/m6e-home-splash.jpg"
          alt=""
        />
      </div>
    </h1>
  );
}

export { Logo };
