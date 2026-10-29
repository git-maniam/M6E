/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
});

const MAX_UNSEEN_CRASHED_CHILD_IDS = 20;

// This is SIGUSR1 and indicates a user-invoked crash
const EXIT_CODE_CONTENT_CRASHED = 245;

/**
 * BrowserWeakMap is exactly like a WeakMap, but expects <xul:browser>
 * objects only.
 *
 * Under the hood, BrowserWeakMap keys the map off of the <xul:browser>
 * permanentKey. If, however, the browser has never gotten a permanentKey,
 * it falls back to keying on the <xul:browser> element itself.
 */
class BrowserWeakMap extends WeakMap {
  get(browser) {
    if (browser.permanentKey) {
      return super.get(browser.permanentKey);
    }
    return super.get(browser);
  }

  set(browser, value) {
    if (browser.permanentKey) {
      return super.set(browser.permanentKey, value);
    }
    return super.set(browser, value);
  }

  delete(browser) {
    if (browser.permanentKey) {
      return super.delete(browser.permanentKey);
    }
    return super.delete(browser);
  }
}

export var TabCrashHandler = {
  _crashedTabCount: 0,
  browserMap: new BrowserWeakMap(),
  unseenCrashedChildIDs: [],
  crashedBrowserQueues: new Map(),
  restartRequiredBrowsers: new WeakSet(),
  testBuildIDMismatch: false,

  init() {
    if (this.initialized) {
      return;
    }
    this.initialized = true;

    Services.obs.addObserver(this, "ipc:content-shutdown");
    Services.obs.addObserver(this, "oop-frameloader-crashed");
  },

  observe(aSubject, aTopic) {
    switch (aTopic) {
      case "ipc:content-shutdown": {
        aSubject.QueryInterface(Ci.nsIPropertyBag2);

        if (!aSubject.get("abnormal")) {
          return;
        }

        let childID = aSubject.get("childID");
        let dumpID = aSubject.get("dumpID");

        if (!dumpID) {
          Glean.browserContentCrash.dumpUnavailable.add(1);
        }

        if (!this.flushCrashedBrowserQueue(childID)) {
          this.unseenCrashedChildIDs.push(childID);
          // The elements in unseenCrashedChildIDs will only be removed if
          // the tab crash page is shown. However, ipc:content-shutdown might
          // be fired for processes for which we'll never show the tab crash
          // page - for example, the thumbnailing process.
          //
          // Instead of trying to account for all of those cases, we prevent
          // this list from getting too large by putting a reasonable upper
          // limit on how many childIDs we track. It's unlikely that this
          // array would ever get so large as to be unwieldy (that'd be a lot
          // or crashes!), but a leak is a leak.
          if (
            this.unseenCrashedChildIDs.length > MAX_UNSEEN_CRASHED_CHILD_IDS
          ) {
            this.unseenCrashedChildIDs.shift();
          }
        }

        // check for environment affecting crash reporting
        let shutdown = Services.env.exists("MOZ_CRASHREPORTER_SHUTDOWN");

        if (shutdown) {
          dump(
            "A content process crashed and MOZ_CRASHREPORTER_SHUTDOWN is " +
              "set, shutting down\n"
          );
          Services.startup.quit(
            Ci.nsIAppStartup.eForceQuit,
            EXIT_CODE_CONTENT_CRASHED
          );
        }

        break;
      }
      case "oop-frameloader-crashed": {
        let browser = aSubject.ownerElement;
        if (!browser) {
          return;
        }

        this.browserMap.set(browser, aSubject.childID);
        break;
      }
    }
  },

  /**
   * This should be called once a content process has finished
   * shutting down abnormally. Any tabbrowser browsers that were
   * selected at the time of the crash will then be sent to
   * the crashed tab page.
   *
   * @param childID (int)
   *        The childID of the content process that just crashed.
   * @returns boolean
   *        True if one or more browsers were sent to the tab crashed
   *        page.
   */
  flushCrashedBrowserQueue(childID) {
    let browserQueue = this.crashedBrowserQueues.get(childID);
    if (!browserQueue) {
      return false;
    }

    this.crashedBrowserQueues.delete(childID);

    let sentBrowser = false;
    for (let weakBrowser of browserQueue) {
      let browser = weakBrowser.get();
      if (browser) {
        if (
          this.restartRequiredBrowsers.has(browser) ||
          this.testBuildIDMismatch
        ) {
          this.sendToRestartRequiredPage(browser);
        } else {
          this.sendToTabCrashedPage(browser);
        }
        sentBrowser = true;
      }
    }

    return sentBrowser;
  },

  /**
   * Called by a tabbrowser when it notices that its selected browser
   * has crashed. This will queue the browser to show the tab crash
   * page once the content process has finished tearing down.
   *
   * @param browser (<xul:browser>)
   *        The selected browser that just crashed.
   * @param restartRequired (bool)
   *        Whether or not a browser restart is required to recover.
   */
  onSelectedBrowserCrash(browser, restartRequired) {
    if (!browser.isRemoteBrowser) {
      console.error("Selected crashed browser is not remote.");
      return;
    }
    if (!browser.frameLoader) {
      console.error("Selected crashed browser has no frameloader.");
      return;
    }

    let childID = browser.frameLoader.childID;

    let browserQueue = this.crashedBrowserQueues.get(childID);
    if (!browserQueue) {
      browserQueue = [];
      this.crashedBrowserQueues.set(childID, browserQueue);
    }
    // It's probably unnecessary to store this browser as a
    // weak reference, since the content process should complete
    // its teardown in the same tick of the event loop, and then
    // this queue will be flushed. The weak reference is to avoid
    // leaking browsers in case anything goes wrong during this
    // teardown process.
    browserQueue.push(Cu.getWeakReference(browser));

    if (restartRequired) {
      this.restartRequiredBrowsers.add(browser);
    }

    // In the event that the content process failed to launch, then
    // the childID will be 0. In that case, we will never receive
    // a dumpID nor an ipc:content-shutdown observer notification,
    // so we should flush the queue for childID 0 immediately.
    if (childID == 0) {
      this.flushCrashedBrowserQueue(0);
    }
  },

  /**
   * Called by a tabbrowser when it notices that a background browser
   * has crashed. This will flip its remoteness to non-remote, and attempt
   * to revive the crashed tab so that upon selection the tab either shows
   * an error page, or automatically restores.
   *
   * @param browser (<xul:browser>)
   *        The background browser that just crashed.
   * @param restartRequired (bool)
   *        Whether or not a browser restart is required to recover.
   */
  onBackgroundBrowserCrash(browser, restartRequired) {
    if (restartRequired) {
      this.restartRequiredBrowsers.add(browser);
    }

    let gBrowser = browser.getTabBrowser();
    let tab = gBrowser.getTabForBrowser(browser);

    gBrowser.updateBrowserRemoteness(browser, {
      remoteType: lazy.E10SUtils.NOT_REMOTE,
    });

    lazy.SessionStore.reviveCrashedTab(tab);
  },

  /**
   * This method is exposed for SessionStore to call if the user selects
   * a tab which will restore on demand. It's possible that the tab
   * is in this state because it recently crashed. If that's the case, then
   * it's also possible that the user has not seen the tab crash page for
   * that particular crash, in which case, we might show it to them instead
   * of restoring the tab.
   *
   * @param browser (<xul:browser>)
   *        A browser from a browser tab that the user has just selected
   *        to restore on demand.
   * @returns (boolean)
   *        True if TabCrashHandler will send the user to the tab crash
   *        page instead.
   */
  willShowCrashedTab(browser) {
    let childID = this.browserMap.get(browser);
    // We will only show the tab crash page if:
    // 1) We are aware that this browser crashed
    // 2) We know we've never shown the tab crash page for the
    //    crash yet
    if (childID && this.unseenCrashedChildIDs.includes(childID)) {
      this.sendToTabCrashedPage(browser);
      return true;
    } else if (childID === 0) {
      if (this.restartRequiredBrowsers.has(browser)) {
        this.sendToRestartRequiredPage(browser);
      } else {
        this.sendToTabCrashedPage(browser);
      }
      return true;
    }

    return false;
  },

  sendToRestartRequiredPage(browser) {
    let uri = browser.currentURI;
    let gBrowser = browser.getTabBrowser();
    let tab = gBrowser.getTabForBrowser(browser);
    // The restart required page is non-remote by default.
    gBrowser.updateBrowserRemoteness(browser, {
      remoteType: lazy.E10SUtils.NOT_REMOTE,
    });

    browser.docShell.displayLoadError(Cr.NS_ERROR_BUILDID_MISMATCH, uri, null);
    tab.setAttribute("crashed", true);
  },

  /**
   * We show a special page to users when a normal browser tab has crashed.
   * This method should be called to send a browser to that page once the
   * process has completely closed.
   *
   * @param browser (<xul:browser>)
   *        The browser that has recently crashed.
   */
  sendToTabCrashedPage(browser) {
    let title = browser.contentTitle;
    let uri = browser.currentURI;
    let gBrowser = browser.getTabBrowser();
    let tab = gBrowser.getTabForBrowser(browser);
    // The tab crashed page is non-remote by default.
    gBrowser.updateBrowserRemoteness(browser, {
      remoteType: lazy.E10SUtils.NOT_REMOTE,
    });

    browser.setAttribute("crashedPageTitle", title);
    browser.docShell.displayLoadError(Cr.NS_ERROR_CONTENT_CRASHED, uri, null);
    browser.removeAttribute("crashedPageTitle");
    tab.setAttribute("crashed", true);
  },

  /**
   * Process a crashed tab loaded into a browser.
   *
   * @param browser
   *        The <xul:browser> containing the page that crashed.
   */
  onAboutTabCrashedLoad(browser) {
    this._crashedTabCount++;

    let window = browser.documentGlobal;

    // Reset the zoom for the tabcrashed page.
    window.ZoomManager.setZoomForBrowser(browser, 1);

    let childID = this.browserMap.get(browser);
    let index = this.unseenCrashedChildIDs.indexOf(childID);
    if (index != -1) {
      this.unseenCrashedChildIDs.splice(index, 1);
    }
  },

  onAboutTabCrashedUnload(browser) {
    if (!this._crashedTabCount) {
      console.error("Can not decrement crashed tab count to below 0");
      return;
    }
    this._crashedTabCount--;

    let childID = this.browserMap.get(browser);

    // Make sure to only count once even if there are multiple windows
    // that will all show about:tabcrashed.
    if (this._crashedTabCount == 0 && childID) {
      Glean.browserContentCrash.notSubmitted.add(1);
    }
  },

  /**
   * This is intended for TESTING ONLY. It returns the amount of
   * content processes that have crashed such that we're still waiting
   * for dump IDs for their crash reports.
   *
   * For our automated tests, accessing the crashed content process
   * count helps us test the behaviour when content processes crash due
   * to launch failure, since in those cases we should not increase the
   * crashed browser queue (since we never receive dump IDs for launch
   * failures).
   */
  get queuedCrashedBrowsers() {
    return this.crashedBrowserQueues.size;
  },
};
