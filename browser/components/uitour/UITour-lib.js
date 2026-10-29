/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// create namespace
if (typeof Mozilla == "undefined") {
  var Mozilla = {};
}

(function () {
  "use strict";

  // create namespace
  if (typeof Mozilla.UITour == "undefined") {
    /**
     * Library that exposes an event-based Web API for communicating with the
     * desktop browser chrome. It can be used for tasks such as opening menu
     * panels and highlighting the position of buttons in the toolbar.
     *
     * For security/privacy reasons `Mozilla.UITour` will only work on a list of allowed
     * secure origins. The list of allowed origins can be found in
     * https://searchfox.org/firefox-main/source/browser/app/permissions.
     *
     * @since 29
     * @namespace
     */
    Mozilla.UITour = {};
  }

  function _sendEvent(action, data) {
    var event = new CustomEvent("mozUITour", {
      bubbles: true,
      detail: {
        action,
        data: data || {},
      },
    });

    document.dispatchEvent(event);
  }

  function _generateCallbackID() {
    return Math.random()
      .toString(36)
      .replace(/[^a-z]+/g, "");
  }

  function _waitForCallback(callback) {
    var id = _generateCallbackID();

    function listener(event) {
      if (typeof event.detail != "object") {
        return;
      }
      if (event.detail.callbackID != id) {
        return;
      }

      document.removeEventListener("mozUITourResponse", listener);
      callback(event.detail.data);
    }
    document.addEventListener("mozUITourResponse", listener);

    return id;
  }

  var notificationListener = null;
  function _notificationListener(event) {
    if (typeof event.detail != "object") {
      return;
    }
    if (typeof notificationListener != "function") {
      return;
    }

    notificationListener(event.detail.event, event.detail.params);
  }

  Mozilla.UITour.DEFAULT_THEME_CYCLE_DELAY = 10 * 1000;

  Mozilla.UITour.CONFIGNAME_AVAILABLETARGETS = "availableTargets";

  /**
   * @typedef {string} UITourTarget
   * @memberof Mozilla.UITour
   *
   * @summary Not all targets are available at all times because they may not be visible
   * or UITour doesn't not how to automatically make them visible. Use the
   * following to determine which ones are available at a given time::
   *
   * .. code-block:: javascript
   *
   *    Mozilla.UITour.getConfiguration('availableTargets', callback)
   *
   * @see Mozilla.UITour.getConfiguration
   * @see Mozilla.UITour.showHighlight
   * @see Mozilla.UITour.showInfo
   *
   * @description Valid values:
   *
   * - addons
   * - appMenu
   * - backForward
   * - bookmarks
   * - forget
   * - help
   * - home
   * - logins
   * - pageAction-bookmark
   * - pocket
   * - privateWindow
   * - quit
   * - readerMode-urlBar
   * - search
   * - searchIcon
   * - selectedTabIcon
   * - urlbar
   *
   * Generate using the following in the Browser Console:
   *
   * .. code-block:: javascript
   *
   *    [...UITour.targets.keys()].join("\n* - ")
   */

  /**
   * Ensure the browser is ready to handle this document as a tour.
   *
   * @param {Function} [callback] Callback to call if UITour is working for the document.
   * @since 35
   */
  Mozilla.UITour.ping = function (callback) {
    var data = {};
    if (callback) {
      data.callbackID = _waitForCallback(callback);
    }
    _sendEvent("ping", data);
  };

  /**
   * @summary Register a listener to observe all UITour notifications.
   *
   * @description There can only be one observer per tour tab so calling this a second time will
   * replace any previous `listener`.
   * To remove an observer, call the method with `null` as the first argument.
   *
   * @param {?Function} listener - Called when any UITour notification occurs.
   * @param {Function} [callback] - Called when the browser acknowledges the observer.
   */
  Mozilla.UITour.observe = function (listener, callback) {
    notificationListener = listener;

    if (listener) {
      document.addEventListener("mozUITourNotification", _notificationListener);
      Mozilla.UITour.ping(callback);
    } else {
      document.removeEventListener(
        "mozUITourNotification",
        _notificationListener
      );
    }
  };

  /**
   * Register an identifier to use in
   * `Telemetry <https://wiki.mozilla.org/Telemetry>`_. `pageID` must be a
   * string unique to the page/tour.
   *
   * @example
   * Mozilla.UITour.registerPageID('firstrun-page-firefox-29');
   *
   * @param {string} pageID Unique identifier for the page/tour.
   * @memberof Mozilla.UITour
   */
  Mozilla.UITour.registerPageID = function (pageID) {
    _sendEvent("registerPageID", {
      pageID,
    });
  };

  /**
   * @typedef {string} UITourHighlightEffect
   *
   * Specifies the effect/animation to use when highlighting UI elements.
   * @description Valid values:
   *
   * - random
   * - wobble
   * - zoom
   * - color
   *
   * Generate using the following in the Browser Console:
   *
   * .. code-block:: javascript
   *
   *    [...UITour.highlightEffects].join("\n* - ")
   *
   * @see Mozilla.UITour.showHighlight
   * @memberof Mozilla.UITour
   */

  /**
   * Visually highlight a UI widget.
   *
   * @see Mozilla.UITour.hideHighlight
   * @example Mozilla.UITour.showHighlight('appMenu', 'wobble');
   *
   * @param {Mozilla.UITour.UITourTarget} target - Identifier of the UI widget to show a highlight on.
   * @param {Mozilla.UITour.UITourHighlightEffect} [effect="none"] - Name of the effect to use when highlighting.
   */
  Mozilla.UITour.showHighlight = function (target, effect) {
    _sendEvent("showHighlight", {
      target,
      effect,
    });
  };

  /**
   * Hide any visible UI highlight.
   *
   * @see Mozilla.UITour.showHighlight
   */
  Mozilla.UITour.hideHighlight = function () {
    _sendEvent("hideHighlight");
  };

  /**
   * Show an arrow panel with optional images and buttons anchored at a specific UI target.
   *
   * @see Mozilla.UITour.hideInfo
   *
   * @param {Mozilla.UITour.UITourTarget} target - Identifier of the UI widget to anchor the panel at.
   * @param {string} title - Title text to be shown as the heading of the panel.
   * @param {string} text - Body text of the panel.
   * @param {string} [icon=null] - URL of a 48x48px (96px @ 2dppx) image (which will be resolved
   *                               relative to the tab's URI) to display in the panel.
   * @param {object[]} [buttons=[]] - Array of objects describing buttons.
   * @param {string} buttons[].label - Button label
   * @param {string} buttons[].icon - Button icon URL
   * @param {string} buttons[].style - Button style ("primary" or "link")
   * @param {Function} buttons[].callback - Called when the button is clicked
   * @param {object} [options={}] - Advanced options
   * @param {Function} options.closeButtonCallback - Called when the panel's close button is clicked.
   *
   * @example
   * var buttons = [
   *   {
   *     label: 'Cancel',
   *     style: 'link',
   *     callback: cancelBtnCallback
   *   },
   *   {
   *     label: 'Confirm',
   *     style: 'primary',
   *     callback: confirmBtnCallback
   *   }
   * ];
   *
   * var icon = '//mozorg.cdn.mozilla.net/media/img/firefox/australis/logo.png';
   *
   * var options = {
   *   closeButtonCallback: closeBtnCallback
   * };
   *
   * Mozilla.UITour.showInfo('appMenu', 'my title', 'my text', icon, buttons, options);
   */
  Mozilla.UITour.showInfo = function (
    target,
    title,
    text,
    icon,
    buttons,
    options
  ) {
    var buttonData = [];
    if (Array.isArray(buttons)) {
      for (var i = 0; i < buttons.length; i++) {
        buttonData.push({
          label: buttons[i].label,
          icon: buttons[i].icon,
          style: buttons[i].style,
          callbackID: _waitForCallback(buttons[i].callback),
        });
      }
    }

    var closeButtonCallbackID, targetCallbackID;
    if (options && options.closeButtonCallback) {
      closeButtonCallbackID = _waitForCallback(options.closeButtonCallback);
    }
    if (options && options.targetCallback) {
      targetCallbackID = _waitForCallback(options.targetCallback);
    }

    _sendEvent("showInfo", {
      target,
      title,
      text,
      icon,
      buttons: buttonData,
      closeButtonCallbackID,
      targetCallbackID,
    });
  };

  /**
   * Hide any visible info panels.
   *
   * @see Mozilla.UITour.showInfo
   */
  Mozilla.UITour.hideInfo = function () {
    _sendEvent("hideInfo");
  };

  /**
   * @typedef {string} UITourMenuName
   * Valid values:
   *
   * - appMenu
   * - bookmarks
   * - pocket
   *
   * @see Mozilla.UITour.showMenu
   * @see Mozilla.UITour.hideMenu
   * @memberof Mozilla.UITour
   */

  /**
   * Open the named application menu.
   *
   * @see Mozilla.UITour.hideMenu
   *
   * @param {Mozilla.UITour.UITourMenuName} name - Menu name
   * @param {Function} [callback] - Callback to be called with no arguments when
   *                                the menu opens.
   *
   * @example
   * Mozilla.UITour.showMenu('appMenu', function() {
   *   console.log('menu was opened');
   * });
   */
  Mozilla.UITour.showMenu = function (name, callback) {
    var showCallbackID;
    if (callback) {
      showCallbackID = _waitForCallback(callback);
    }

    _sendEvent("showMenu", {
      name,
      showCallbackID,
    });
  };

  /**
   * Close the named application menu.
   *
   * @see Mozilla.UITour.showMenu
   *
   * @param {Mozilla.UITour.UITourMenuName} name - Menu name
   */
  Mozilla.UITour.hideMenu = function (name) {
    _sendEvent("hideMenu", {
      name,
    });
  };

  /**
   * Loads about:newtab in the tour tab.
   *
   * @param {string} [hash] - Optional fragment identifier to append to
   *   about:newtab, e.g. "customize" to open the Customize panel. Must
   *   consist only of the characters `[a-zA-Z0-9_-]`.
   *
   * @since 51
   */
  Mozilla.UITour.showNewTab = function (hash) {
    _sendEvent("showNewTab", {
      hash,
    });
  };

  /**
   * Loads about:home in the tour tab. Unlike showNewTab, this always loads
   * Firefox Home regardless of the user's new tab page setting.
   *
   * @param {string} [hash] - Optional fragment identifier to append to
   *   about:home, e.g. "customize" to open the Customize panel. Must
   *   consist only of the characters `[a-zA-Z0-9_-]`.
   *
   * @since 157
   */
  Mozilla.UITour.showHome = function (hash) {
    _sendEvent("showHome", {
      hash,
    });
  };

  /**
   * Loads about:protections in the tour tab.
   *
   * @since 70
   */
  Mozilla.UITour.showProtectionReport = function () {
    _sendEvent("showProtectionReport");
  };

  /**
   * @typedef UITourConfigurationName
   * @memberof Mozilla.UITour
   * @description Valid values:
   *
   * - :js:func:`appinfo <Mozilla.UITour.Configuration.UITourConfigurationAppInfo>`
   * - :js:func:`canReset <Mozilla.UITour.Configuration.UITourConfigurationCanReset>`
   * - :js:func:`availableTargets <Mozilla.UITour.Configuration.UITourConfigurationAvailableTargets>`
   * - :js:func:`search <Mozilla.UITour.Configuration.UITourConfigurationSearch>`
   * - :js:func:`selectedSearchEngine <Mozilla.UITour.Configuration.UITourConfigurationSearch>`
   *   DEPRECATED, use 'search'
   */

  /**
   * @namespace Mozilla.UITour.Configuration
   * @see Mozilla.UITour.getConfiguration
   * @see Mozilla.UITour.UITourConfigurationName
   */

  /**
   * @typedef {boolean} UITourConfigurationCanReset
   * @memberof Mozilla.UITour.Configuration
   *
   * @description Indicate whether a user can refresh their Firefox profile via :js:func:`Mozilla.UITour.resetFirefox`.
   *
   * @see Mozilla.UITour.resetFirefox
   * @since 48
   */

  /**
   * @typedef {object} UITourConfigurationAppInfo
   * @memberof Mozilla.UITour.Configuration
   * @property {boolean} canSetDefaultBrowserInBackground - Whether the application can be set as
   *                                                        the default browser in the background
   *                                                        without user interaction.
   * @property {boolean} defaultBrowser - Whether the application is the default browser. Since Fx40.
   * @property {string} defaultUpdateChannel - Update channel e.g. 'release', 'beta', 'aurora',
   *                                           'nightly', 'default'
   *                                           (self-built or automated testing builds)
   * @property {string} distribution - Contains the distributionId property. This value will be
   *                                   "default" in most cases but can differ for repack or
   *                                   funnelcake builds. Since Fx48
   * @property {number} profileCreatedWeeksAgo - The number of weeks since the profile was created,
   *                                             starting from 0 for profiles dating less than
   *                                             seven days old. Since Fx56.
   * @property {number} profileResetWeeksAgo - The number of weeks since the profile was last reset,
   *                                           starting from 0 for profiles reset less than seven
   *                                           days ago. If the profile has never been reset it
   *                                           returns null. Since Fx56.
   * @property {boolean} needsPin - Whether Firefox still needs to be pinned to the taskbar (Windows)
   *                                or Dock (macOS). Since Fx152.
   * @property {string} version - Version string e.g. "48.0a2"
   * @since 35
   */

  /**
   * @summary Search service configuration.
   *
   * @description From version 34 through 42 inclusive, a string was returned for the 'selectedSearchEngine'
   * configuration instead of the object like 'search'.
   *
   * @typedef {string | object} UITourConfigurationSearch
   * @memberof Mozilla.UITour.Configuration
   * @property {string} searchEngineIdentifier - The default engine's identifier
   * @property {string[]} engines - Identifiers of visible engines
   * @since 43
   */

  /**
   * Array of UI :js:func:`Targets <Mozilla.UITour.UITourTarget>` currently available to be annotated.
   *
   * @typedef {Mozilla.UITour.UITourTarget[]} UITourConfigurationAvailableTargets
   * @memberof Mozilla.UITour.Configuration
   */

  /**
   * Retrieve some information about the application/profile.
   *
   * @param {Mozilla.UITour.UITourConfigurationName} configName - Name of configuration to retrieve
   * @param {Function} callback - Called with one argument containing the value of the configuration.
   */
  Mozilla.UITour.getConfiguration = function (configName, callback) {
    _sendEvent("getConfiguration", {
      callbackID: _waitForCallback(callback),
      configuration: configName,
    });
  };

  /**
   * Set some value or take some action.
   *
   * Valid configuration names:
   *
   * defaultBrowser
   *   Try to set the application as the default web browser. Since Fx40
   *
   * @param {string} configName - Configuration name to set (e.g. "defaultBrowser")
   * @param {string | number | boolean} [configValue] - Not currently used
   *
   * @since 40
   * @example
   * Mozilla.UITour.setConfiguration('defaultBrowser');
   */
  Mozilla.UITour.setConfiguration = function (configName, configValue) {
    _sendEvent("setConfiguration", {
      configuration: configName,
      value: configValue,
    });
  };

  /**
   * Show a profile refresh/reset dialog, allowing users to choose to reomve
   * add-ons and customizations as well as restore browser defaults, if possible.
   * `getConfiguration('canReset')` should first be used to determine whether
   * Refresh/Reset is possible for the user's build/profile.
   *
   * @since 48
   * @see Mozilla.UITour.Configuration.UITourConfigurationCanReset
   */
  Mozilla.UITour.resetFirefox = function () {
    _sendEvent("resetFirefox");
  };

  /**
   * Add the specified customizable widget to the navigation toolbar.
   *
   * @param {Mozilla.UITour.UITourTarget} name - Identifier of the customizable widget.
   * @param {Function} callback - Called with no arguments once the icon was successfully added to
   *                              the toolbar. Not called if it doesn't succeed.
   * @since 33.1
   * @example
   * Mozilla.UITour.addNavBarWidget('forget', function () {
   *   console.log('forget button added to toolbar');
   * });
   */
  Mozilla.UITour.addNavBarWidget = function (name, callback) {
    _sendEvent("addNavBarWidget", {
      name,
      callbackID: _waitForCallback(callback),
    });
  };

  /**
   * Set the specified search engine as the user-set default.
   *
   * See https://searchfox.org/mozilla-release/source/browser/locales/search/list.json
   *
   * @param {string} identifier - Identifier of the engine (e.g. 'yahoo').
   * @see Mozilla.UITour.Configuration.UITourConfigurationSearch
   * @since 34
   */
  Mozilla.UITour.setDefaultSearchEngine = function (identifier) {
    _sendEvent("setDefaultSearchEngine", {
      identifier,
    });
  };

  /**
   * Pin Firefox to the taskbar (Windows) or Dock (macOS).
   * `getConfiguration('appinfo')` should first be used to check `data.needsPin` before calling this.
   *
   * @since 152
   */
  Mozilla.UITour.pinToTaskbar = function () {
    _sendEvent("pinToTaskbar");
  };

  /**
   * Set the New Tab Page wallpaper. Enables the wallpaper feature if it is
   * currently off. The wallpaper is identified by its id (title) from the
   * `newtab-wallpapers-v2` Remote Settings collection, e.g. 'moon', a
   * 'solid-color-picker-#RRGGBB' value, or 'custom'. Passing an empty string
   * clears the wallpaper.
   *
   * @param {string} wallpaper - Wallpaper id to set
   * @since 157
   * @example
   * Mozilla.UITour.setNewtabWallpaper('moon');
   */
  Mozilla.UITour.setNewtabWallpaper = function (wallpaper) {
    _sendEvent("setNewtabWallpaper", {
      wallpaper,
    });
  };

  /**
   * Sets a key+value pair as a treatment tag for recording in FHR.
   *
   * @param {string} name - tag name for the treatment
   * @param {string} value - tag value for the treatment
   * @since 34
   * @see Mozilla.UITour.getTreatmentTag
   * @example
   * Mozilla.UITour.setTreatmentTag('srch-chg-action', 'Switch');
   */
  Mozilla.UITour.setTreatmentTag = function (name, value) {
    _sendEvent("setTreatmentTag", {
      name,
      value,
    });
  };

  /**
   * Retrieved the value for a set FHR treatment tag.
   *
   * @param {string} name - Tag name to be retrieved
   * @param {Function} callback - Called once the data has been retrieved
   * @since 34
   * @see Mozilla.UITour.setTreatmentTag
   * @example
   * Mozilla.UITour.getTreatmentTag('srch-chg-action', function(value) {
   *   console.log(value);
   * });
   */
  Mozilla.UITour.getTreatmentTag = function (name, callback) {
    _sendEvent("getTreatmentTag", {
      name,
      callbackID: _waitForCallback(callback),
    });
  };

  /**
   * Set the search term in the search box.
   *
   * This should have been implemented via `setConfiguration("searchTerm", …)`.
   *
   * @param {string} term - Search string e.g. 'Firefox'
   * @since 34
   */
  Mozilla.UITour.setSearchTerm = function (term) {
    _sendEvent("setSearchTerm", {
      term,
    });
  };

  /**
   * @summary Force the reader mode icon to appear in the address bar regardless of whether
   * heuristics determine it's appropriate.
   *
   * @description This is useful if you want to target an annotation (panel/highlight) on it
   * but the tour page doesn't have much textual content.
   */
  Mozilla.UITour.forceShowReaderIcon = function () {
    _sendEvent("forceShowReaderIcon");
  };

  /**
   * Toggle into reader mode for the current tab. Once the user enters reader
   * mode, the UITour document will not be active and therefore cannot call other
   * UITour APIs.
   */
  Mozilla.UITour.toggleReaderMode = function () {
    _sendEvent("toggleReaderMode");
  };

  /**
   * @param {string} pane - Pane to open/switch the preferences to.
   * Valid values are the keys of CONFIG_PANES in preferences.js and the
   * legacy names in LegacyPaneMappings.mjs (which are remapped automatically
   * when browser.settings-redesign.enabled is true).
   *
   * To open the options for sending telemetry, the health report, and
   * crash reports, call `Mozilla.UITour.openPreferences("privacy-reports")`.
   * UITour handles the route mapping automatically. These settings live
   * in the Privacy pane under the legacy UI and in the Permissions and Data
   * pane under the settings-redesign UI.
   *
   * @since 42
   */
  Mozilla.UITour.openPreferences = function (pane) {
    _sendEvent("openPreferences", {
      pane,
    });
  };

  /**
   * @summary Closes the tab where this code is running. As usual, if the tab is in the
   * foreground, the tab that was displayed before is selected.
   *
   * @description The last tab in the current window will never be closed, in which case
   * this call will have no effect. The calling code is expected to take an
   * action after a small timeout in order to handle this case, for example by
   * displaying a goodbye message or a button to restart the tour.
   * @since 46
   */
  Mozilla.UITour.closeTab = function () {
    _sendEvent("closeTab");
  };
})();

// Make this library Require-able.
if (typeof module !== "undefined" && module.exports) {
  module.exports = Mozilla.UITour;
}
