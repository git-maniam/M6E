/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// M6E: the page telemetry button and panel (D29).

ChromeUtils.defineESModuleGetters(this, {
  M6EPageTelemetry:
    "moz-src:///browser/components/pagetelemetry/M6EPageTelemetry.sys.mjs",
});

var gM6EPageTelemetry = {
  REFRESH_MS: 1000,

  get button() {
    return document.getElementById("m6e-page-telemetry-button");
  },

  get panel() {
    return document.getElementById("m6e-page-telemetry-panel");
  },

  /**
   * Opens or closes the panel for a left click, Space or Enter on the button.
   *
   * @param {Event} event
   */
  handleButtonEvent(event) {
    event.stopPropagation();
    if (
      (event.type == "click" && event.button != 0) ||
      (event.type == "keypress" &&
        event.charCode != KeyEvent.DOM_VK_SPACE &&
        event.keyCode != KeyEvent.DOM_VK_RETURN)
    ) {
      return;
    }
    if (this.panel.state == "open" || this.panel.state == "showing") {
      this.panel.hidePopup();
      return;
    }
    this.openPanel(event);
  },

  openPanel(triggerEvent) {
    let panel = this.panel;
    if (!this._initialized) {
      this._initialized = true;
      panel.addEventListener("popupshown", () => {
        this.button.setAttribute("open", "true");
      });
      panel.addEventListener("popuphidden", () => {
        this.button.removeAttribute("open");
        clearInterval(this._refreshTimer);
        this._refreshTimer = null;
      });
    }
    this.update();
    clearInterval(this._refreshTimer);
    this._refreshTimer = setInterval(() => this.update(), this.REFRESH_MS);
    panel.openPopup(this.button, {
      position: "bottomleft topleft",
      triggerEvent,
    });
  },

  async update() {
    let browser = gBrowser.selectedBrowser;
    let data = null;
    try {
      data = await M6EPageTelemetry.collect(browser);
    } catch (e) {
      // The page navigated or its process went away while we asked.
    }
    if (browser != gBrowser.selectedBrowser) {
      return;
    }
    this.render(data);
  },

  formatBytes(bytes) {
    let units = ["byte", "kilobyte", "megabyte", "gigabyte"];
    let unit = 0;
    while (bytes >= 1000 && unit < units.length - 1) {
      bytes /= 1000;
      unit++;
    }
    return new Intl.NumberFormat(undefined, {
      style: "unit",
      unit: units[unit],
      unitDisplay: "short",
      maximumFractionDigits: unit ? 1 : 0,
    }).format(bytes);
  },

  setMilliseconds(cell, value, missingId) {
    if (value == null) {
      document.l10n.setAttributes(cell, missingId);
    } else {
      document.l10n.setAttributes(cell, "m6e-page-telemetry-milliseconds", {
        value,
      });
    }
  },

  render(data) {
    let table = document.getElementById("m6e-page-telemetry-table");
    document.getElementById("m6e-page-telemetry-unsupported").hidden = !!data;
    table.hidden = !data;
    document.getElementById("m6e-page-telemetry-loading").hidden =
      !data || data.loaded;
    if (!data) {
      return;
    }

    let cell = metric =>
      table.querySelector(`tr[data-metric="${metric}"] > td`);
    let missing = data.loaded
      ? "m6e-page-telemetry-not-available"
      : "m6e-page-telemetry-measuring";

    this.setMilliseconds(cell("ttfb"), data.ttfb, missing);
    this.setMilliseconds(cell("fcp"), data.fcp, missing);
    this.setMilliseconds(cell("tti"), data.tti, "m6e-page-telemetry-measuring");
    this.setMilliseconds(cell("scriptParse"), data.scriptParse, missing);
    this.setMilliseconds(
      cell("scriptExecution"),
      data.scriptExecution,
      missing
    );

    let tls = cell("tls");
    switch (data.tlsState) {
      case "handshake":
        this.setMilliseconds(tls, data.tls, missing);
        break;
      case "reused":
        document.l10n.setAttributes(
          tls,
          "m6e-page-telemetry-connection-reused"
        );
        break;
      default:
        document.l10n.setAttributes(tls, "m6e-page-telemetry-not-encrypted");
    }

    let size = cell("size");
    if (data.size) {
      document.l10n.setAttributes(
        size,
        data.size.cached
          ? "m6e-page-telemetry-size-cached"
          : "m6e-page-telemetry-size",
        {
          transferred: this.formatBytes(data.size.transferred),
          requests: data.size.requests,
          cached: data.size.cached,
        }
      );
    } else {
      document.l10n.setAttributes(size, "m6e-page-telemetry-not-available");
    }
  },
};
