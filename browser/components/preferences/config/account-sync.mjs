/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// preferences.js typedefs are duplicated here because @import requires an ES
// module source. preferences.js is a plain script currently.
/**
 * @typedef {object} PaneShownEventDetail
 * @property {string} category
 * @property {string} subcategory
 */

/**
 * @typedef {Omit<CustomEvent, 'detail'> & {
 *   detail: PaneShownEventDetail
 * }} PaneShownEvent
 */

import { Preferences } from "chrome://global/content/preferences/Preferences.mjs";
import { SettingGroupManager } from "chrome://browser/content/preferences/config/SettingGroupManager.mjs";

const XPCOMUtils = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
).XPCOMUtils;
const { Referrals } = ChromeUtils.importESModule(
  "moz-src:///browser/components/referrals/Referrals.sys.mjs"
);
const lazy = XPCOMUtils.declareLazy({
  BackupService: "moz-src:///browser/components/backup/BackupService.sys.mjs",
  SelectableProfileService:
    "resource:///modules/profiles/SelectableProfileService.sys.mjs",
});

// Import section

Preferences.addSetting({
  id: "data-migration",
  visible: () =>
    !Services.policies || Services.policies.isAllowed("profileImport"),
  onUserClick() {
    window.gMainPane.showMigrationWizardDialog();
  },
});

// Profiles section

Preferences.addSetting({
  id: "profilesPane",
  onUserClick(e) {
    e.preventDefault();
    window.gotoPref("paneProfiles");
  },
});
Preferences.addSetting({
  id: "profilesSettings",
  visible() {
    return lazy.SelectableProfileService.isEnabled;
  },
  onUserClick: e => {
    e.preventDefault();
    window.gotoPref("profiles");
  },
});
Preferences.addSetting({
  id: "manageProfiles",
  onUserClick: e => {
    e.preventDefault();
    // Using the existing function for now, since privacy.js also calls it
    window.gMainPane.manageProfiles();
  },
});
Preferences.addSetting({
  id: "copyProfile",
  deps: ["copyProfileSelect"],
  disabled: ({ copyProfileSelect }) => !copyProfileSelect.value,
  onUserClick: (e, { copyProfileSelect }) => {
    e.preventDefault();
    lazy.SelectableProfileService.getProfile(copyProfileSelect.value).then(
      profile => {
        profile?.copyProfile();
        copyProfileSelect.config.set("");
      }
    );
  },
});
Preferences.addSetting({
  id: "copyProfileBox",
  visible: () => lazy.SelectableProfileService.initialized,
});
Preferences.addSetting({
  id: "copyProfileError",
  _hasError: false,
  setup(emitChange) {
    this.emitChange = emitChange;
  },
  visible() {
    return this._hasError;
  },
  setError(value) {
    this._hasError = !!value;
    this.emitChange();
  },
});
Preferences.addSetting(
  class ProfileList extends Preferences.AsyncSetting {
    static id = "profileList";
    static PROFILE_UPDATED_OBS = "sps-profiles-updated";
    setup() {
      Services.obs.addObserver(
        this.emitChange,
        ProfileList.PROFILE_UPDATED_OBS
      );
      return () => {
        Services.obs.removeObserver(
          this.emitChange,
          ProfileList.PROFILE_UPDATED_OBS
        );
      };
    }

    async get() {
      let profiles = await lazy.SelectableProfileService.getAllProfiles();
      return profiles;
    }
  }
);
Preferences.addSetting({
  id: "copyProfileSelect",
  deps: ["profileList"],
  _selectedProfile: null,
  setup(emitChange) {
    this.emitChange = emitChange;
    document.l10n
      .formatValue("preferences-copy-profile-select")
      .then(result => (this.placeholderString = result));
  },
  get() {
    return this._selectedProfile;
  },
  set(inputVal) {
    this._selectedProfile = inputVal;
    this.emitChange();
  },
  getControlConfig(config, { profileList }) {
    config.options = profileList.value.map(profile => {
      return { controlAttrs: { label: profile.name }, value: profile.id };
    });

    // Put the placeholder at the front of the list.
    config.options.unshift({
      controlAttrs: { label: this.placeholderString },
      value: "",
    });

    return config;
  },
});
Preferences.addSetting({
  id: "copyProfileHeader",
  visible: () => lazy.SelectableProfileService.initialized,
});

// Backup section

Preferences.addSetting({
  id: "backupSettings",
  setup(emitChange) {
    Services.obs.addObserver(emitChange, "backup-service-status-updated");
    return () =>
      Services.obs.removeObserver(emitChange, "backup-service-status-updated");
  },
  visible: () => {
    let bs = lazy.BackupService.init();
    return bs.archiveEnabledStatus.enabled || bs.restoreEnabledStatus.enabled;
  },
});

// Referrals section
Preferences.addSetting({
  id: "referrals-link",
  setup() {
    Referrals.getReferralCode();
  },
  visible() {
    return Referrals.isEnabled;
  },
  onUserClick: () => {
    Referrals.openReferralsTab(window, "preferences");
  },
});

SettingGroupManager.registerGroups({
  defaultBrowserSync: window.createDefaultBrowserConfig({
    includeIsDefaultPane: false,
    inProgress: true,
    hiddenFromSearch: true,
  }),
  importBrowserData: {
    l10nId: "preferences-data-migration-group",
    headingLevel: 2,
    iconSrc: "chrome://browser/skin/import.svg",
    subcategory: "importBrowserData",
    items: [
      {
        id: "data-migration",
        l10nId: "preferences-data-migration-button",
        control: "moz-box-button",
      },
    ],
  },
  profilePane: {
    headingLevel: 2,
    id: "browserProfilesGroupPane",
    l10nId: "preferences-profiles-subpane-description",
    supportPage: "profile-management",
    items: [
      {
        id: "manageProfiles",
        control: "moz-box-button",
        l10nId: "preferences-manage-profiles-button",
      },
      {
        id: "copyProfileHeader",
        l10nId: "preferences-copy-profile-header",
        headingLevel: 2,
        supportPage: "profile-management",
        control: "moz-fieldset",
        items: [
          {
            id: "copyProfileBox",
            l10nId: "preferences-profile-to-copy",
            control: "moz-box-item",
            items: [
              {
                id: "copyProfileSelect",
                control: "moz-select",
                slot: "actions",
              },
              {
                id: "copyProfile",
                l10nId: "preferences-copy-profile-button",
                control: "moz-button",
                slot: "actions",
                controlAttrs: {
                  type: "primary",
                },
              },
            ],
          },
        ],
      },
    ],
  },
  profiles: {
    id: "profilesGroup",
    l10nId: "preferences-profiles-section-header",
    headingLevel: 2,
    supportPage: "profile-management",
    items: [
      {
        id: "profilesSettings",
        loadPane: "profiles",
        control: "moz-box-button",
        l10nId: "preferences-profiles-settings-button",
      },
    ],
  },
  backup: {
    l10nId: "settings-data-backup-header2",
    headingLevel: 2,
    supportPage: "firefox-backup",
    iconSrc: "chrome://global/skin/icons/reload.svg",
    subcategory: "backup",
    items: [
      {
        id: "backupSettings",
        control: "backup-settings",
      },
    ],
  },
  referrals: {
    l10nId: "referrals-section-header2",
    headingLevel: 2,
    iconSrc: "chrome://browser/content/logos/share-firefox.svg",
    hidden: !Referrals.isEnabled,
    items: [
      {
        id: "referrals-link",
        control: "moz-box-button",
        l10nId: "referrals-link2",
      },
    ],
  },
});

document.addEventListener(
  "paneshown",
  (/** @type {PaneShownEvent} */ event) => {
    if (event.detail.category !== "paneSync") {
      return;
    }
    if (Services.policies && !Services.policies.isAllowed("profileImport")) {
      return;
    }
    let { subcategory } = event.detail;
    if (subcategory == "migrate") {
      window.gMainPane.showMigrationWizardDialog();
    } else if (subcategory == "migrate-autoclose") {
      window.gMainPane.showMigrationWizardDialog({ closeTabWhenDone: true });
    }
  }
);
