/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";

const L10N_IDS = {
  okHeader: "security-privacy-status-ok-header",
  problemHeader: "security-privacy-status-problem-header",
  okLabel: "security-privacy-status-ok-label",
  problemLabel: "security-privacy-status-problem-label",
  problemHelperLabel: "security-privacy-status-problem-helper-label",
  trackersPendingLabel: "security-privacy-status-pending-trackers-label",
  trackersLabel: "security-privacy-status-trackers-label",
  strictEnabledLabel: "security-privacy-status-strict-enabled-label",
  customEnabledLabel: "security-privacy-status-custom-enabled-label",
};

/**
 * Custom Element for a card holding configuration issues from the user settings
 */
export default class SecurityPrivacyCard extends MozLitElement {
  get strictEnabled() {
    return this.setting.deps.etpStrictEnabled.value;
  }

  get customEnabled() {
    return this.setting.deps.etpCustomEnabled.value;
  }

  get trackersBlocked() {
    return this.setting.deps.trackerCount.value;
  }

  get configIssueCount() {
    let filteredWarnings = [
      "etpStrictEnabled",
      "etpCustomEnabled",
      "trackerCount",
    ];
    return Object.values(this.setting.deps).filter(
      warning => !filteredWarnings.includes(warning.id) && warning.visible
    ).length;
  }

  /**
   * Scrolling to an element in about:preferences is non-trivial because the fragment is controlled
   * by the panel manager. This is just a clean abstraction to hand a callback off to another site.
   *
   * @param {string} panelHash - the ID of the panel the element we want to scroll to lives on
   * @param {string} subcategory - the ID of the subcategory to scroll to and highliht
   * @returns {Function} a callback that will perform the scroll
   */
  #spotlightSubcategoryOnPane(panelHash, subcategory) {
    return function () {
      document.location.hash = panelHash + "-" + subcategory;
    };
  }

  #openWarningCardAndScroll() {
    let accordion = document.getElementById("warningCard");
    if (!accordion) {
      return;
    }
    accordion.expanded = true;
    this.#spotlightSubcategoryOnPane("#privacy", "security-warning-card")();
  }

  getStatusImage() {
    if (this.configIssueCount > 0) {
      return html`<img
        class="status-image"
        src="chrome://global/skin/illustrations/shield-alert.svg"
        data-l10n-id="security-privacy-image-warning"
      />`;
    }
    return html`<img
      class="status-image"
      src="chrome://global/skin/illustrations/shield-check.svg"
      data-l10n-id="security-privacy-image-ok"
    />`;
  }

  /**
   * Create the bullet point for the current count of "issues" in the user profile.
   * Really only depends on `this.configIssueCount`
   *
   * @returns {TemplateResult} the HTML for the "issues" bullet of the custom element
   */
  buildIssuesElement() {
    if (this.configIssueCount == 0) {
      return html`<li class="status-ok">
        <p data-l10n-id=${L10N_IDS.okLabel}></p>
      </li>`;
    }
    return html`<li class="status-alert">
      <div>
        <p data-l10n-id=${L10N_IDS.problemLabel}></p>
        <p>
          <small
            ><a
              href=""
              @click=${() => this.#openWarningCardAndScroll()}
              data-l10n-id=${L10N_IDS.problemHelperLabel}
            ></a
          ></small>
        </p>
      </div>
    </li>`;
  }

  /**
   * Create the bullet point for the current count of trackers blocked in the past week.
   * Really only depends on `this.trackersBlocked`and `this.strictEnabled`
   *
   * @returns {TemplateResult} the HTML for the "trackers" bullet of the custom element
   */
  buildTrackersElement() {
    let trackerData = {
      trackerCount: this.trackersBlocked,
    };
    let trackerLabelElement =
      this.trackersBlocked != null
        ? html`<p
            data-l10n-id=${L10N_IDS.trackersLabel}
            data-l10n-args=${JSON.stringify(trackerData)}
          ></p>`
        : html`<p data-l10n-id=${L10N_IDS.trackersPendingLabel}></p>`;

    if (this.strictEnabled) {
      return html`<li class="status-ok">
        <div>
          ${trackerLabelElement}
          <p>
            <small
              data-l10n-id=${L10N_IDS.strictEnabledLabel}
              id="strictEnabled"
              @click=${this.#spotlightSubcategoryOnPane(
                "#etp",
                "etp-strict-control"
              )}
            >
              <a data-l10n-name="strict-tracking-protection" href=""></a
            ></small>
          </p>
        </div>
      </li>`;
    } else if (this.customEnabled) {
      return html`<li class="status-ok">
        <div>
          ${trackerLabelElement}
          <p>
            <small
              data-l10n-id=${L10N_IDS.customEnabledLabel}
              id="customEnabled"
              @click=${this.#spotlightSubcategoryOnPane(
                "#etp",
                "etp-custom-control"
              )}
            >
              <a data-l10n-name="custom-tracking-protection" href=""></a
            ></small>
          </p>
        </div>
      </li>`;
    }
    return html`<li class="status-ok">${trackerLabelElement}</li>`;
  }

  /**
   * Lit invoked callback to render a template for this component.
   * This creates a card for itself, populates it with bullets and headings,
   * and nests a <configuration-issue-card>.
   *
   * @returns {TemplateResult} the full HTML of this panel, CSS <link> and <moz-card>s included
   */
  render() {
    // Create l10n fields for the card's header
    let headerL10nId = L10N_IDS.okHeader;
    if (this.configIssueCount > 0) {
      headerL10nId = L10N_IDS.problemHeader;
    }

    // And render this template!
    return html`
      <link
        rel="stylesheet"
        href="chrome://browser/content/preferences/widgets/security-privacy-card.css"
      />
      <link
        rel="stylesheet"
        href="chrome://global/skin/design-system/text-and-typography.css"
      />
      <moz-card aria-labelledby="heading">
        <div class="card-contents">
          <div class="status-text-container">
            <h3
              id="heading"
              class="text-box-trim-start"
              data-l10n-id=${headerL10nId}
            ></h3>
            <ul>
              ${this.buildIssuesElement()} ${this.buildTrackersElement()}
            </ul>
          </div>
          ${this.getStatusImage()}
        </div>
      </moz-card>
    `;
  }
}
customElements.define("security-privacy-card", SecurityPrivacyCard);
