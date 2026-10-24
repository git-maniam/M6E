/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * @typedef {object} UIState
 * @property {string} status The FxA status, see STATUS_* constants.
 * @property {string} [email] The FxA email.
 * @property {string} [displayName] The user's FxA display name.
 * @property {string} [avatarURL] The user's FxA avatar URL.
 * @property {boolean} [hasSyncKeys] Whether the user has sync keys available.
 */

import { SCOPE_APP_SYNC } from "resource://gre/modules/FxAccountsCommon.sys.mjs";

const TOPICS = [
  "fxaccounts:onverified",
  "fxaccounts:onlogin", // Defined in FxAccountsCommon, pulling it is expensive.
  "fxaccounts:onlogout",
  "fxaccounts:profilechange",
  "fxaccounts:statechange",
];

const ON_UPDATE = "sync-ui-state:update";

const STATUS_NOT_CONFIGURED = "not_configured";
const STATUS_LOGIN_FAILED = "login_failed";
const STATUS_NOT_VERIFIED = "not_verified";
const STATUS_SIGNED_IN = "signed_in";

const DEFAULT_STATE = {
  status: STATUS_NOT_CONFIGURED,
};

const UIStateInternal = {
  _initialized: false,
  _state: null,

  get state() {
    if (!this._state) {
      return DEFAULT_STATE;
    }
    return Object.assign({}, this._state);
  },

  isReady() {
    if (!this._initialized) {
      this.init();
      return false;
    }
    return true;
  },

  init() {
    this._initialized = true;
    // Because the FxA toolbar is usually visible, this module gets loaded at
    // browser startup, and we want to avoid pulling in all of FxA at that
    // time, so we refresh the state after the browser has settled.
    Services.tm.idleDispatchToMainThread(() => {
      this.refreshState().catch(e => {
        console.error(e);
      });
    }, 2000);
  },

  // Used for testing.
  reset() {
    this._state = null;
    this._initialized = false;
  },

  observe() {
    this.refreshState().catch(e => {
      console.error(e);
    });
  },

  // Builds a new state from scratch.
  async refreshState() {
    const newState = {};
    await this._refreshFxAState(newState);
    // Optimize the "not signed in" case to avoid refreshing twice just after
    // startup - if there's currently no _state, and we still aren't configured,
    // just early exit.
    if (this._state == null && newState.status == DEFAULT_STATE.status) {
      return this.state;
    }
    this._state = newState;

    this.notifyStateUpdated();
    return this.state;
  },

  notifyStateUpdated() {
    Services.obs.notifyObservers(null, ON_UPDATE);
  },

  async _refreshFxAState(newState) {
    let userData = await this._getUserData();
    await this._populateWithUserData(newState, userData);
  },

  async _populateWithUserData(state, userData) {
    let status;
    if (!userData) {
      status = STATUS_NOT_CONFIGURED;
    } else {
      let loginFailed = !(await this.fxAccounts.hasLocalSession());
      if (loginFailed) {
        status = STATUS_LOGIN_FAILED;
      } else if (!userData.verified) {
        status = STATUS_NOT_VERIFIED;
      } else {
        status = STATUS_SIGNED_IN;
      }
      state.uid = userData.uid;
      state.email = userData.email;
      state.displayName = userData.displayName;
      // for better or worse, this module renames these attribues.
      state.avatarURL = userData.avatar;
      state.avatarIsDefault = userData.avatarDefault;
      state.hasSyncKeys =
        await this.fxAccounts.keys.hasKeysForScope(SCOPE_APP_SYNC);
    }
    state.status = status;
  },

  async _getUserData() {
    try {
      return await this.fxAccounts.getSignedInUser();
    } catch (e) {
      // This is most likely in tests, where we quickly log users in and out.
      // The most likely scenario is a user logged out, so reflect that.
      // Bug 995134 calls for better errors so we could retry if we were
      // sure this was the failure reason.
      console.error("Error updating FxA account info:", e);
      return null;
    }
  },

  set fxAccounts(mockFxAccounts) {
    delete this.fxAccounts;
    this.fxAccounts = mockFxAccounts;
  },
};

ChromeUtils.defineLazyGetter(UIStateInternal, "fxAccounts", () => {
  return ChromeUtils.importESModule(
    "resource://gre/modules/FxAccounts.sys.mjs"
  ).getFxAccountsSingleton();
});

for (let topic of TOPICS) {
  Services.obs.addObserver(UIStateInternal, topic);
}

export var UIState = {
  _internal: UIStateInternal,

  ON_UPDATE,

  STATUS_NOT_CONFIGURED,
  STATUS_LOGIN_FAILED,
  STATUS_NOT_VERIFIED,
  STATUS_SIGNED_IN,

  /**
   * Returns true if the module has been initialized and the state set.
   * If not, return false and trigger an init in the background.
   */
  isReady() {
    return this._internal.isReady();
  },

  /**
   * @returns {UIState} The current FxA UI State.
   */
  get() {
    return this._internal.state;
  },

  /**
   * Refresh the state. Used for testing, don't call this directly since
   * UIState already listens to FxA notifications to determine if the state
   * needs to be refreshed. ON_UPDATE will be fired once the state is refreshed.
   *
   * @returns {Promise<UIState>} Resolved once the state is refreshed.
   */
  refresh() {
    return this._internal.refreshState();
  },

  /**
   * Reset the state of the whole module. Used for testing.
   */
  reset() {
    this._internal.reset();
  },
};
