/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AppMenuNotifications: "resource://gre/modules/AppMenuNotifications.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  ClientID: "resource://gre/modules/ClientID.sys.mjs",
  FxAccounts: "resource://gre/modules/FxAccounts.sys.mjs",
  UIState: "resource://gre/modules/UIState.sys.mjs",
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "CLIENT_ASSOCIATION_PING_ENABLED",
  "identity.fxaccounts.telemetry.clientAssociationPing.enabled",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "CLIENT_INFO_PING_ENABLED",
  "identity.fxaccounts.telemetry.clientInfoPing.enabled",
  false
);

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "AlertsService",
  "@mozilla.org/alerts-service;1",
  Ci.nsIAlertsService
);

ChromeUtils.defineLazyGetter(
  lazy,
  "accountsL10n",
  () => new Localization(["browser/accounts.ftl", "branding/brand.ftl"], true)
);

const AlertNotification = Components.Constructor(
  "@mozilla.org/alert-notification;1",
  "nsIAlertNotification",
  "initWithObject"
);

/**
 * Manages Mozilla Account and Sync related functionality
 * needed at startup. It mainly handles various account-related events and notifications.
 *
 * This module was sliced off of BrowserGlue and designed to centralize
 * account-related events/notifications to prevent crowding BrowserGlue
 */
export const AccountsGlue = {
  QueryInterface: ChromeUtils.generateQI([
    "nsIObserver",
    "nsISupportsWeakReference",
  ]),

  init() {
    let os = Services.obs;
    [
      "fxaccounts:onverified",
      "fxaccounts:device_connected",
      "fxaccounts:device_disconnected",
      "sync-ui-state:update",
      "idle-daily",
    ].forEach(topic => os.addObserver(this, topic, true));
  },

  observe(subject, topic, data) {
    switch (topic) {
      case "fxaccounts:onverified":
        this._onThisDeviceConnected();
        break;
      case "fxaccounts:device_connected":
        this._onDeviceConnected(data);
        break;
      case "fxaccounts:device_disconnected":
        data = JSON.parse(data);
        if (data.isLocalDevice) {
          this._onDeviceDisconnected();
        }
        break;
      case "sync-ui-state:update": {
        this._updateFxaBadges(
          lazy.BrowserWindowTracker.getTopWindow({
            allowFromInactiveWorkspace: true,
          })
        );

        if (lazy.CLIENT_ASSOCIATION_PING_ENABLED) {
          let fxaState = lazy.UIState.get();
          if (fxaState.status == lazy.UIState.STATUS_SIGNED_IN) {
            Glean.clientAssociation.uid.set(fxaState.uid);
            Glean.clientAssociation.legacyClientId.set(
              lazy.ClientID.getCachedClientID()
            );
          }
        }
        break;
      }
      case "browser-glue-test": // used by tests
        if (data == "mock-alerts-service") {
          // eslint-disable-next-line mozilla/valid-lazy
          Object.defineProperty(lazy, "AlertsService", {
            value: subject.wrappedJSObject,
          });
        }
        break;
      case "idle-daily":
        if (
          lazy.CLIENT_INFO_PING_ENABLED &&
          lazy.UIState.get().status == lazy.UIState.STATUS_SIGNED_IN
        ) {
          GleanPings.fxAccountsClientInfo.submit();
        }
        break;
    }
  },

  _onThisDeviceConnected() {
    const [title, body] = lazy.accountsL10n.formatValuesSync([
      "account-connection-title-2",
      "account-connection-connected",
    ]);

    let clickCallback = (subject, topic) => {
      if (topic != "alertclickcallback") {
        return;
      }
      this._openPreferences("sync");
    };
    let alert = new AlertNotification({
      title,
      text: body,
      textClickable: true,
    });
    lazy.AlertsService.showAlert(alert, clickCallback);
  },

  _openURLInNewWindow(url, privateTab = false) {
    let urlString = Cc["@mozilla.org/supports-string;1"].createInstance(
      Ci.nsISupportsString
    );
    urlString.data = url;
    let features = "chrome,all,dialog=no";
    if (privateTab) {
      features += ",private";
    }
    return new Promise(resolve => {
      let win = Services.ww.openWindow(
        null,
        AppConstants.BROWSER_CHROME_URL,
        "_blank",
        features,
        urlString
      );
      win.addEventListener(
        "load",
        () => {
          resolve(win);
        },
        { once: true }
      );
    });
  },

  _onDeviceConnected(deviceName) {
    const [title, body] = lazy.accountsL10n.formatValuesSync([
      { id: "account-connection-title-2" },
      deviceName
        ? { id: "account-connection-connected-with", args: { deviceName } }
        : { id: "account-connection-connected-with-noname" },
    ]);

    let clickCallback = async (subject, topic) => {
      if (topic != "alertclickcallback") {
        return;
      }
      let url = await lazy.FxAccounts.config.promiseManageDevicesURI(
        "device-connected-notification"
      );
      let win = lazy.BrowserWindowTracker.getTopWindow({ private: false });
      if (!win) {
        this._openURLInNewWindow(url, false);
      } else {
        win.gBrowser.addWebTab(url);
      }
    };

    try {
      let alert = new AlertNotification({
        title,
        text: body,
        textClickable: true,
      });
      lazy.AlertsService.showAlert(alert, clickCallback);
    } catch (ex) {
      console.error("Error notifying of a new Sync device: ", ex);
    }
  },

  _onDeviceDisconnected() {
    const [title, body] = lazy.accountsL10n.formatValuesSync([
      "account-connection-title-2",
      "account-connection-disconnected",
    ]);

    let clickCallback = (subject, topic) => {
      if (topic != "alertclickcallback") {
        return;
      }
      this._openPreferences("sync");
    };

    let alert = new AlertNotification({
      title,
      text: body,
      textClickable: true,
    });
    lazy.AlertsService.showAlert(alert, clickCallback);
  },

  _updateFxaBadges(win) {
    let fxaButton = win.document.getElementById("fxa-toolbar-menu-button");
    let badge = fxaButton?.querySelector(".toolbarbutton-badge");

    let state = lazy.UIState.get();
    if (
      state.status == lazy.UIState.STATUS_LOGIN_FAILED ||
      state.status == lazy.UIState.STATUS_NOT_VERIFIED
    ) {
      // If the fxa toolbar button is in the toolbox, we display the notification
      // on the fxa button instead of the app menu.
      let navToolbox = win.document.getElementById("navigator-toolbox");
      let isFxAButtonShown = navToolbox.contains(fxaButton);
      if (isFxAButtonShown) {
        state.status == lazy.UIState.STATUS_LOGIN_FAILED
          ? fxaButton?.setAttribute("badge-status", state.status)
          : badge?.classList.add("feature-callout");
      } else {
        lazy.AppMenuNotifications.showBadgeOnlyNotification(
          "fxa-needs-authentication"
        );
      }
    } else {
      fxaButton?.removeAttribute("badge-status");
      badge?.classList.remove("feature-callout");
      lazy.AppMenuNotifications.removeNotification("fxa-needs-authentication");
    }
  },

  // Open preferences even if there are no open windows.
  async _openPreferences(...args) {
    let chromeWindow = lazy.BrowserWindowTracker.getTopWindow();
    if (!chromeWindow && AppConstants.platform !== "macosx") {
      // If we're not on macOS, there may be no windows open in this
      // workspace, so open a new one. (the macOS case is handled below)
      //
      // This should get cleaned up in bug 1983081 since openPreferences()
      // shouldn't require a window argument.
      chromeWindow = await lazy.BrowserWindowTracker.promiseOpenWindow();
    }
    if (chromeWindow) {
      chromeWindow.openPreferences(...args);
      return;
    }

    if (AppConstants.platform == "macosx") {
      Services.appShell.hiddenDOMWindow.openPreferences(...args);
    }
  },
};
