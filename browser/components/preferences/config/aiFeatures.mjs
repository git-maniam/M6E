/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";
import { Preferences } from "chrome://global/content/preferences/Preferences.mjs";
import { SettingGroupManager } from "chrome://browser/content/preferences/config/SettingGroupManager.mjs";
import { OnDeviceModelManager } from "chrome://browser/content/preferences/OnDeviceModelManager.mjs";

/**
 * @import { OnDeviceModelFeaturesEnum } from "chrome://browser/content/preferences/OnDeviceModelManager.mjs"
 * @typedef {typeof AiControlGlobalStates[keyof typeof AiControlGlobalStates]} AiControlGlobalStatesEnum
 */

Preferences.addAll([
  // browser.ai.control.* prefs defined in main.js
  { id: "browser.preferences.aiControls.showUnavailable", type: "bool" },
]);

Preferences.addSetting({
  id: "aiControlsShowUnavailable",
  pref: "browser.preferences.aiControls.showUnavailable",
});

Preferences.addSetting({ id: "aiControlsDescription" });
Preferences.addSetting({ id: "blockAiGroup" });
Preferences.addSetting({ id: "blockAiDescription" });
Preferences.addSetting({ id: "onDeviceFieldset" });
Preferences.addSetting({
  id: "onDeviceGroup",
  deps: [
    "aiControlTranslationsSelect",
    "aiControlPdfjsAltTextSelect",
    "aiControlSmartTabGroupsSelect",
  ],
  getControlConfig(config, deps) {
    for (let option of config.options) {
      let control = option.items[0];
      if (control.id in deps) {
        option.controlAttrs = option.controlAttrs || {};
        option.controlAttrs.class = deps[control.id].visible
          ? ""
          : "setting-hidden";
      }
    }
    return config;
  },
});
Preferences.addSetting({ id: "aiStatesDescription" });
Preferences.addSetting({
  id: "aiBlockedMessage",
  deps: ["aiControlDefaultToggle"],
  visible: deps => {
    return deps.aiControlDefaultToggle.value;
  },
});

const AiControlStates = Object.freeze({
  default: "default",
  enabled: "enabled",
  blocked: "blocked",
  available: "available",
});

const AiControlGlobalStates = Object.freeze({
  available: "available",
  blocked: "blocked",
});

/**
 * @param {AiControlGlobalStatesEnum} state
 */
function updateAiControlDefault(state) {
  let isBlocked = state == AiControlGlobalStates.blocked;
  for (let feature of Object.values(OnDeviceModelManager.features)) {
    if (isBlocked) {
      // Reset to default (blocked) state unless it was already blocked.
      OnDeviceModelManager.block(feature);
    } else if (!isBlocked && !OnDeviceModelManager.isEnabled(feature)) {
      // Reset to default (available) state unless it was manually enabled.
      OnDeviceModelManager.makeAvailable(feature);
    }
  }
  if (isBlocked) {
    Services.prefs.setStringPref(
      "browser.ai.control.default",
      AiControlGlobalStates.blocked
    );
  }
  // There's no feature-specific dropdown for extensions since it's still a
  // trial feature, so just turn it off/on based on the global switch.
  Services.prefs.setBoolPref("extensions.ml.enabled", !isBlocked);
  Glean.browser.globalAiControlToggled.record({ blocked: isBlocked });
}

class BlockAiConfirmationDialog extends MozLitElement {
  static properties = {
    headingL10nId: { type: String },
    descriptionL10nId: { type: String },
    isGlobal: { type: Boolean },
  };

  #resolvers = Promise.withResolvers();
  #confirmed = false;

  constructor() {
    super();
    this.isGlobal = true;
  }

  get dialog() {
    return this.renderRoot.querySelector("dialog");
  }

  get confirmButton() {
    return this.renderRoot.querySelector('moz-button[type="primary"]');
  }

  get cancelButton() {
    return this.renderRoot.querySelector('moz-button:not([type="primary"])');
  }

  /**
   * @param {object} options
   * @param {boolean} options.all - Show the global block dialog with all features listed
   * @param {string} [options.headingL10nId] - Custom heading l10n ID for feature-specific dialogs
   * @param {string} [options.descriptionL10nId] - Custom description l10n ID for feature-specific dialogs
   * @returns {Promise<boolean>} - Resolves true if the user confirmed, false if cancelled
   */
  showModal({ all, headingL10nId, descriptionL10nId }) {
    this.#resolvers = Promise.withResolvers();
    this.#confirmed = false;
    this.isGlobal = !!all;
    this.headingL10nId = headingL10nId;
    this.descriptionL10nId = descriptionL10nId;
    this.updateComplete.then(() => this.dialog.showModal());
    return this.#resolvers.promise;
  }

  handleCancel() {
    this.#confirmed = false;
    this.dialog.close();
  }

  handleConfirm() {
    this.#confirmed = true;
    this.dialog.close();
  }

  globalTemplate() {
    return html`
      <p
        data-l10n-id="preferences-ai-controls-block-confirmation-description"
      ></p>
      <p
        class="ul-prefix-p"
        data-l10n-id="preferences-ai-controls-block-confirmation-features-start"
      ></p>
      <ul>
        <li
          data-l10n-id="preferences-ai-controls-block-confirmation-translations"
        ></li>
        <li
          data-l10n-id="preferences-ai-controls-block-confirmation-speech-recognition"
        ></li>
        <li
          data-l10n-id="preferences-ai-controls-block-confirmation-pdfjs"
        ></li>
        <li
          data-l10n-id="preferences-ai-controls-block-confirmation-tab-group-suggestions"
        ></li>
      </ul>
      <p
        data-l10n-id="preferences-ai-controls-block-confirmation-features-after"
      ></p>
      <a is="moz-support-link" support-page="firefox-ai-controls"></a>
    `;
  }

  descriptionTemplate() {
    return html`<p data-l10n-id=${this.descriptionL10nId}></p>`;
  }

  onToggle() {
    if (!this.dialog.open) {
      this.#resolvers.resolve(this.#confirmed);
    }
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://global/skin/in-content/common.css"
      />
      <link
        rel="stylesheet"
        href="chrome://browser/skin/preferences/preferences.css"
      />
      <link
        rel="stylesheet"
        href="chrome://browser/content/preferences/config/block-ai-confirmation-dialog.css"
      />
      <dialog
        aria-labelledby="heading"
        aria-describedby="content"
        @toggle=${this.onToggle}
      >
        <div class="dialog-header">
          ${this.isGlobal
            ? html`<img
                class="dialog-header-icon"
                src="chrome://global/skin/icons/block.svg"
                alt=""
              />`
            : nothing}
          <h2
            id="heading"
            class="text-box-trim-start"
            data-l10n-id=${this.isGlobal
              ? "preferences-ai-controls-block-confirmation-heading"
              : this.headingL10nId}
          ></h2>
        </div>
        <div id="content" class="dialog-body">
          ${this.isGlobal ? this.globalTemplate() : this.descriptionTemplate()}
        </div>
        <moz-button-group>
          <moz-button
            data-l10n-id="preferences-ai-controls-block-confirmation-cancel"
            @click=${this.handleCancel}
          ></moz-button>
          <moz-button
            autofocus
            type="primary"
            data-l10n-id="preferences-ai-controls-block-confirmation-confirm"
            @click=${this.handleConfirm}
          ></moz-button>
        </moz-button-group>
      </dialog>
    `;
  }
}
customElements.define(
  "block-ai-confirmation-dialog",
  BlockAiConfirmationDialog
);

const AI_CONTROL_OPTIONS = [
  {
    value: AiControlStates.available,
    l10nId: "preferences-ai-controls-state-available",
  },
  {
    value: AiControlStates.enabled,
    l10nId: "preferences-ai-controls-state-enabled",
  },
  {
    value: AiControlStates.blocked,
    l10nId: "preferences-ai-controls-state-blocked",
  },
];

Preferences.addSetting({
  id: "aiControlDefaultToggle",
  pref: "browser.ai.control.default",
  setup() {
    document.body.append(
      document.createElement("block-ai-confirmation-dialog")
    );
  },
  get: prefVal =>
    prefVal in AiControlGlobalStates
      ? prefVal == AiControlGlobalStates.blocked
      : AiControlGlobalStates.available,
  set(inputVal, _, setting) {
    if (inputVal) {
      // Restore the toggle to not pressed, we're opening a dialog
      setting.onChange();
      let dialog = /** @type {BlockAiConfirmationDialog} */ (
        document.querySelector("block-ai-confirmation-dialog")
      );
      dialog.showModal({ all: true }).then(confirmed => {
        if (confirmed) {
          updateAiControlDefault(AiControlGlobalStates.blocked);
        }
      });
    } else {
      updateAiControlDefault(AiControlGlobalStates.available);
    }
    return AiControlGlobalStates.available;
  },
});

/**
 * @param {object} options
 * @param {string} options.id Setting id to create
 * @param {string} options.pref Pref id for the state
 * @param {OnDeviceModelFeaturesEnum} options.feature Feature id for removing models
 * @param {SettingConfig['getControlConfig']} [options.getControlConfig] A getControlConfig implementation.
 * @param {() => Promise<boolean>} [options.onBeforeBlock] Optional async callback to show a modal before blocking
 */
function makeAiControlSetting({
  id,
  pref,
  feature,
  getControlConfig,
  onBeforeBlock,
  setup,
  visible,
}) {
  function recordTelemetry(selection) {
    Glean.browser.aiControlChanged.record({ feature, selection });
  }

  Preferences.addSetting({
    id,
    pref,
    deps: ["aiControlDefault", "aiControlsShowUnavailable"],
    setup(emitChange) {
      /**
       * @param {nsISupports} _
       * @param {string} __
       * @param {string} changedFeature
       */
      const featureChange = (_, __, changedFeature) => {
        if (changedFeature == feature) {
          emitChange();
        }
      };
      Services.obs.addObserver(featureChange, "OnDeviceModelManagerChange");
      const teardownSetup = setup?.(emitChange);
      return () => {
        Services.obs.removeObserver(
          featureChange,
          "OnDeviceModelManagerChange"
        );
        teardownSetup?.();
      };
    },
    get(prefVal, deps) {
      const aiControlState = OnDeviceModelManager.getAiControlState(feature);

      if (
        prefVal == AiControlStates.blocked ||
        (prefVal == AiControlStates.default &&
          deps.aiControlDefault.value == AiControlGlobalStates.blocked) ||
        aiControlState == AiControlStates.blocked
      ) {
        return AiControlStates.blocked;
      }

      if (
        OnDeviceModelManager.hasDistinctEnabledState(feature) &&
        (prefVal == AiControlStates.enabled ||
          aiControlState == AiControlStates.enabled)
      ) {
        return AiControlStates.enabled;
      }

      return AiControlStates.available;
    },
    set(prefVal, _, setting) {
      if (prefVal == AiControlStates.blocked && onBeforeBlock) {
        setting.onChange();
        onBeforeBlock().then(confirmed => {
          if (confirmed) {
            OnDeviceModelManager.block(feature);
            recordTelemetry(AiControlStates.blocked);
          }
        });

        return setting.value;
      }

      if (prefVal == AiControlStates.available) {
        OnDeviceModelManager.makeAvailable(feature);
      } else if (prefVal == AiControlStates.enabled) {
        OnDeviceModelManager.enable(feature);
      } else if (prefVal == AiControlStates.blocked) {
        OnDeviceModelManager.block(feature);
      }
      return prefVal;
    },
    disabled() {
      return OnDeviceModelManager.isManagedByPolicy(feature);
    },
    visible(deps) {
      if (visible && !visible(deps)) {
        return false;
      }
      return (
        OnDeviceModelManager.isAllowed(feature) ||
        deps.aiControlsShowUnavailable.value
      );
    },
    onUserChange(selection, _, setting) {
      // Only record telemetry if the selection was actually saved
      // since selecting "blocked" shows a block confirmation dialog that the user may cancel
      if (selection === setting.value) {
        recordTelemetry(selection);
      }
    },
    getControlConfig(config, deps, setting) {
      if (!OnDeviceModelManager.hasDistinctEnabledState(feature)) {
        config.options = config.options.filter(
          option => option.value != AiControlStates.enabled
        );
      }

      return getControlConfig
        ? getControlConfig(config, deps, setting)
        : config;
    },
  });
}
makeAiControlSetting({
  id: "aiControlTranslationsSelect",
  pref: "browser.ai.control.translations",
  feature: OnDeviceModelManager.features.Translations,
  getControlConfig(config, _, setting) {
    let isBlocked = setting.value == AiControlStates.blocked;
    let moreSettingsLink = config.options.at(-1);
    moreSettingsLink.hidden = isBlocked;
    config.supportPage = isBlocked ? "website-translation" : null;
    return config;
  },
});
makeAiControlSetting({
  id: "aiControlPdfjsAltTextSelect",
  pref: "browser.ai.control.pdfjsAltText",
  feature: OnDeviceModelManager.features.PdfAltText,
});
makeAiControlSetting({
  id: "aiControlSmartTabGroupsSelect",
  pref: "browser.ai.control.smartTabGroups",
  feature: OnDeviceModelManager.features.TabGroups,
  setup(emitChange) {
    const onTabGroupsEnabledChange = (_, __, changedPref) => {
      if (changedPref == "browser.tabs.groups.enabled") {
        emitChange();
      }
    };
    Services.prefs.addObserver(
      "browser.tabs.groups.enabled",
      onTabGroupsEnabledChange
    );
    return () =>
      Services.prefs.removeObserver(
        "browser.tabs.groups.enabled",
        onTabGroupsEnabledChange
      );
  },
  visible() {
    return Services.prefs.getBoolPref("browser.tabs.groups.enabled", true);
  },
});
makeAiControlSetting({
  id: "aiControlSpeechRecognitionSelect",
  pref: "browser.ai.control.speechRecognition",
  feature: OnDeviceModelManager.features.SpeechRecognition,
});

SettingGroupManager.registerGroups({
  aiControlsDescription: {
    card: "never",
    items: [
      {
        id: "aiControlsDescription",
        control: "moz-card",
        controlAttrs: {
          class: "ai-controls-description",
        },
        options: [
          {
            control: "p",
            options: [
              {
                control: "span",
                l10nId: "preferences-ai-controls-description",
              },
              {
                control: "span",
                controlAttrs: {
                  ".textContent": " ",
                },
              },
              {
                control: "a",
                controlAttrs: {
                  is: "moz-support-link",
                  "support-page": "firefox-ai-controls",
                },
              },
            ],
          },
          {
            control: "img",
            controlAttrs: {
              src: "chrome://browser/skin/preferences/fox-ai.svg",
            },
          },
        ],
      },
    ],
  },
  aiStatesDescription: {
    card: "never",
    items: [
      {
        id: "aiStatesDescription",
        control: "footer",
        controlAttrs: {
          class: "text-deemphasized",
        },
        options: [
          {
            control: "span",
            l10nId: "preferences-ai-controls-state-description-before",
          },
          {
            control: "ul",
            options: [
              {
                control: "li",
                l10nId: "preferences-ai-controls-state-description-available",
              },
              {
                control: "li",
                l10nId: "preferences-ai-controls-state-description-enabled",
              },
              {
                control: "li",
                l10nId: "preferences-ai-controls-state-description-blocked",
              },
            ],
          },
        ],
      },
    ],
  },
  aiFeatures: {
    card: "always",
    items: [
      {
        id: "blockAiGroup",
        control: "moz-box-item",
        items: [
          {
            id: "aiControlDefaultToggle",
            l10nId: "preferences-ai-controls-block-ai",
            control: "moz-toggle",
            headingLevel: 3,
            controlAttrs: {
              inputlayout: "inline-end",
            },
            options: [
              {
                l10nId: "preferences-ai-controls-block-ai-description",
                control: "span",
                slot: "description",
                options: [
                  {
                    control: "a",
                    controlAttrs: {
                      "data-l10n-name": "link",
                      "support-page": "firefox-ai-controls",
                      is: "moz-support-link",
                    },
                  },
                ],
              },
            ],
          },
          {
            id: "aiBlockedMessage",
            control: "moz-message-bar",
            l10nId: "preferences-ai-controls-blocked-message",
            controlAttrs: {
              role: "status",
            },
          },
        ],
      },
      {
        id: "onDeviceFieldset",
        l10nId: "preferences-ai-controls-on-device-group",
        supportPage: "on-device-models",
        control: "moz-fieldset",
        headingLevel: 2,
        controlAttrs: {
          iconsrc: "chrome://browser/skin/device-desktop.svg",
        },
        items: [
          {
            id: "onDeviceGroup",
            control: "moz-box-group",
            options: [
              {
                control: "moz-box-item",
                items: [
                  {
                    id: "aiControlTranslationsSelect",
                    l10nId: "preferences-ai-controls-translations-control",
                    control: "moz-select",
                    controlAttrs: {
                      inputlayout: "inline-end",
                    },
                    options: [
                      ...AI_CONTROL_OPTIONS.filter(
                        opt => opt.value != AiControlStates.enabled
                      ),
                      {
                        control: "a",
                        l10nId:
                          "preferences-ai-controls-translations-more-link",
                        slot: "support-link",
                        controlAttrs: {
                          href: "#general-translations",
                        },
                      },
                    ],
                  },
                ],
              },
              {
                control: "moz-box-item",
                items: [
                  {
                    id: "aiControlSpeechRecognitionSelect",
                    l10nId:
                      "preferences-ai-controls-speech-recognition-control",
                    control: "moz-select",
                    controlAttrs: {
                      inputlayout: "inline-end",
                    },
                    supportPage: "speech-recognition-firefox",
                    options: [...AI_CONTROL_OPTIONS],
                  },
                ],
              },
              {
                control: "moz-box-item",
                items: [
                  {
                    id: "aiControlPdfjsAltTextSelect",
                    l10nId: "preferences-ai-controls-pdfjs-control",
                    control: "moz-select",
                    controlAttrs: {
                      inputlayout: "inline-end",
                    },
                    supportPage: "pdf-alt-text",
                    options: [...AI_CONTROL_OPTIONS],
                  },
                ],
              },
              {
                control: "moz-box-item",
                items: [
                  {
                    id: "aiControlSmartTabGroupsSelect",
                    l10nId:
                      "preferences-ai-controls-tab-group-suggestions-control",
                    control: "moz-select",
                    controlAttrs: {
                      inputlayout: "inline-end",
                    },
                    supportPage: "how-use-ai-enhanced-tab-groups",
                    options: [...AI_CONTROL_OPTIONS],
                  },
                ],
              },
            ],
          },
        ],
      },
    ],
  },
});
