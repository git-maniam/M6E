/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * M6E: reads the page telemetry figures (D29) in the page's process. Times
 * are milliseconds from the start of the navigation.
 */
export class M6EPageTelemetryChild extends JSWindowActorChild {
  receiveMessage(message) {
    if (message.name != "M6EPageTelemetry:Get") {
      return null;
    }
    let win = this.contentWindow;
    let doc = this.document;
    let navigation = win.performance.getEntriesByType("navigation")[0];
    let paint = win.performance.getEntriesByName("first-contentful-paint")[0];
    let timings = doc.getM6EPageTimings();

    let tls = null;
    let tlsState = "none";
    if (doc.documentURIObject.schemeIs("https")) {
      tlsState = "reused";
      if (navigation?.secureConnectionStart > 0) {
        let handshake =
          navigation.connectEnd - navigation.secureConnectionStart;
        if (handshake > 0) {
          tls = handshake;
          tlsState = "handshake";
        }
      }
    }

    return {
      url: doc.documentURI,
      ttfb: navigation?.responseStart > 0 ? navigation.responseStart : null,
      fcp: paint ? paint.startTime : null,
      tti: timings.timeToInteractive ?? null,
      tls,
      tlsState,
      scriptParse: timings.scriptParseTime,
      scriptExecution: timings.scriptExecutionTime,
      loaded: timings.loaded,
    };
  }
}
