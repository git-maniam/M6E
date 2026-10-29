/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var AboutTabCrashed = {
  /**
   * The messages that we might receive from the parent.
   */
  MESSAGES: ["Ready", "UpdateCount"],

  /**
   * Items for which we will listen for click events.
   */
  CLICK_TARGETS: ["closeTab", "restoreTab", "restoreAll"],

  /**
   * Returns information about this crashed tab.
   *
   * @return (Object) An object with the following properties:
   *           title (String):
   *             The title of the page that crashed.
   *           URL (String):
   *             The URL of the page that crashed.
   */
  get pageData() {
    delete this.pageData;

    let URL = document.documentURI;
    let queryString = URL.replace(/^about:tabcrashed?e=tabcrashed/, "");

    let titleMatch = queryString.match(/d=([^&]*)/);
    let URLMatch = queryString.match(/u=([^&]*)/);

    return (this.pageData = {
      title:
        titleMatch && titleMatch[1] ? decodeURIComponent(titleMatch[1]) : "",
      URL: URLMatch && URLMatch[1] ? decodeURIComponent(URLMatch[1]) : "",
    });
  },

  init() {
    addEventListener("DOMContentLoaded", this);

    document.title = this.pageData.title;
  },

  receiveMessage(message) {
    switch (message.name) {
      case "UpdateCount": {
        this.setMultiple(message.data.count > 1);
        break;
      }
      case "Ready": {
        let event = new CustomEvent("AboutTabCrashedReady", { bubbles: true });
        document.dispatchEvent(event);
        break;
      }
    }
  },

  handleEvent(event) {
    switch (event.type) {
      case "DOMContentLoaded": {
        this.onDOMContentLoaded();
        break;
      }
      case "click": {
        this.onClick(event);
        break;
      }
    }
  },

  onDOMContentLoaded() {
    this.MESSAGES.forEach(msg =>
      RPMAddMessageListener(msg, this.receiveMessage.bind(this))
    );

    this.CLICK_TARGETS.forEach(targetID => {
      let el = document.getElementById(targetID);
      el.addEventListener("click", this);
    });

    // Error pages are loaded as LOAD_BACKGROUND, so they don't get load events.
    let event = new CustomEvent("AboutTabCrashedLoad", { bubbles: true });
    document.dispatchEvent(event);

    RPMSendAsyncMessage("Load");
  },

  onClick(event) {
    switch (event.target.id) {
      case "closeTab": {
        this.sendMessage("closeTab");
        break;
      }

      case "restoreTab": {
        this.sendMessage("restoreTab");
        break;
      }

      case "restoreAll": {
        this.sendMessage("restoreAll");
        break;
      }
    }
  },

  /**
   * Toggles whether or not the page is one of several visible pages
   * showing the crashed tab page. This controls some of the language
   * on the page, along with what the "primary" button is.
   *
   * @param hasMultiple (bool)
   *        True if there are multiple crashed tab pages being shown.
   */
  setMultiple(hasMultiple) {
    let main = document.getElementById("main");
    main.setAttribute("multiple", hasMultiple);

    let restoreTab = document.getElementById("restoreTab");

    // The "Restore All" button has the "primary" class by default, so
    // we only need to modify the "Restore Tab" button.
    if (hasMultiple) {
      restoreTab.classList.remove("primary");
    } else {
      restoreTab.classList.add("primary");
    }
  },

  /**
   * Sends a message to the parent in response to the user choosing
   * one of the actions available on the page.
   *
   * @param messageName (String)
   *        The message to send to the parent
   */
  sendMessage(messageName) {
    RPMSendAsyncMessage(messageName);
  },
};

AboutTabCrashed.init();
