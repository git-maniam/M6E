/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * M6E: the page telemetry panel (D29). Measures the current page on the
 * device; nothing is sent anywhere.
 *
 * Page size is counted here, in the parent process, from every HTTP channel
 * that finishes: web pages' own Resource Timing reports 0 for cross-origin
 * resources without Timing-Allow-Origin, so it would under-count. Requests
 * are attributed to the top-level page through their window context, and the
 * top-level document's own response, which finishes before its window exists,
 * is matched to the page by URL when the panel asks.
 */

const PREF_ENABLED = "m6e.pageTelemetry.enabled";

/** Totals per top-level page, keyed by its inner window id. */
const gPageSizes = new Map();

/** The latest top-level document response per browsing context id. */
const gDocumentResponses = new Map();

function addSize(innerWindowId, transferred, resources) {
  let size = gPageSizes.get(innerWindowId);
  if (!size) {
    size = { transferred: 0, resources: 0, requests: 0 };
    gPageSizes.set(innerWindowId, size);
  }
  size.transferred += transferred;
  size.resources += resources;
  size.requests += 1;
}

export const M6EPageTelemetry = {
  PREF_ENABLED,

  get enabled() {
    return Services.prefs.getBoolPref(PREF_ENABLED, true);
  },

  init() {
    if (!this.enabled) {
      return;
    }
    Services.obs.addObserver(this, "http-on-stop-request");
    Services.obs.addObserver(this, "window-global-destroyed");
    Services.obs.addObserver(this, "browsing-context-discarded");
  },

  observe(subject, topic) {
    switch (topic) {
      case "window-global-destroyed":
        gPageSizes.delete(subject.innerWindowId);
        return;
      case "browsing-context-discarded":
        gDocumentResponses.delete(subject.id);
        return;
    }

    let channel;
    try {
      channel = subject.QueryInterface(Ci.nsIHttpChannel);
    } catch {
      return;
    }
    let loadInfo = channel.loadInfo;
    let transferred = channel.transferSize;
    let resources = channel.decodedBodySize;
    if (
      loadInfo.externalContentPolicyType == Ci.nsIContentPolicy.TYPE_DOCUMENT
    ) {
      gDocumentResponses.set(loadInfo.browsingContextID, {
        url: channel.URI.spec,
        transferred,
        resources,
      });
      return;
    }
    let topWindow = WindowGlobalParent.getByInnerWindowId(
      loadInfo.innerWindowID
    )?.topWindowContext;
    if (topWindow) {
      addSize(topWindow.innerWindowId, transferred, resources);
    }
  },

  /**
   * The bytes transferred and the decoded size of all the requests of the page
   * shown in a browser so far, including its own document.
   *
   * @param {MozBrowser} browser
   * @returns {{transferred: number, resources: number, requests: number}?}
   */
  getPageSize(browser) {
    let windowGlobal = browser.browsingContext?.currentWindowGlobal;
    if (!windowGlobal) {
      return null;
    }
    let documentResponse = gDocumentResponses.get(browser.browsingContext.id);
    if (documentResponse?.url == windowGlobal.documentURI?.spec) {
      gDocumentResponses.delete(browser.browsingContext.id);
      addSize(
        windowGlobal.innerWindowId,
        documentResponse.transferred,
        documentResponse.resources
      );
    }
    return (
      gPageSizes.get(windowGlobal.innerWindowId) ?? {
        transferred: 0,
        resources: 0,
        requests: 0,
      }
    );
  },

  /**
   * Everything the panel shows for the page in a browser, or null when the
   * page isn't a web page.
   *
   * @param {MozBrowser} browser
   */
  async collect(browser) {
    if (!browser.currentURI?.schemeIs("http") && !browser.currentURI?.schemeIs("https")) {
      return null;
    }
    let actor =
      browser.browsingContext?.currentWindowGlobal?.getActor(
        "M6EPageTelemetry"
      );
    if (!actor) {
      return null;
    }
    let timings = await actor.sendQuery("M6EPageTelemetry:Get");
    return { ...timings, size: this.getPageSize(browser) };
  },
};
