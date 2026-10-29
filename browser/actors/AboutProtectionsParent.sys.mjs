/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  LoginBreaches:
    "moz-src:///browser/components/aboutlogins/LoginBreaches.sys.mjs",
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",
  PrivacyMetricsService:
    "moz-src:///browser/components/protections/PrivacyMetricsService.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "TrackingDBService",
  "@mozilla.org/tracking-db-service;1",
  Ci.nsITrackingDBService
);

let idToTextMap = new Map([
  [Ci.nsITrackingDBService.TRACKERS_ID, "tracker"],
  [Ci.nsITrackingDBService.TRACKING_COOKIES_ID, "cookie"],
  [Ci.nsITrackingDBService.CRYPTOMINERS_ID, "cryptominer"],
  [Ci.nsITrackingDBService.FINGERPRINTERS_ID, "fingerprinter"],
  // We map the suspicious fingerprinter to fingerprinter category to aggregate
  // the number.
  [Ci.nsITrackingDBService.SUSPICIOUS_FINGERPRINTERS_ID, "fingerprinter"],
  [Ci.nsITrackingDBService.SOCIAL_ID, "social"],
]);

const FXA_PWDMGR_HOST = "chrome://FirefoxAccounts";
const FXA_PWDMGR_REALM = "Firefox Accounts credentials";

let gTestOverride = null;
let entrypoint = "direct";

export class AboutProtectionsParent extends JSWindowActorParent {
  constructor() {
    super();
  }

  // Some tests wish to override certain functions with ones that mostly do nothing.
  static setTestOverride(callback) {
    gTestOverride = callback;
  }

  /**
   * Retrieves login data for the user.
   *
   * @return {{numLogins: number, potentiallyBreachedLogins: number, mobileDeviceConnected: boolean }}
   */
  async getLoginData() {
    if (gTestOverride && "getLoginData" in gTestOverride) {
      return gTestOverride.getLoginData();
    }

    const userFacingLogins =
      (await Services.logins.countLoginsAsync("", "", "")) -
      (await Services.logins.countLoginsAsync(
        FXA_PWDMGR_HOST,
        null,
        FXA_PWDMGR_REALM
      ));

    let potentiallyBreachedLogins = null;
    // Get the stats for number of potentially breached Lockwise passwords
    // if the Primary Password isn't locked.
    if (userFacingLogins && Services.logins.isLoggedIn) {
      const logins = await lazy.LoginHelper.getAllUserFacingLogins();
      potentiallyBreachedLogins =
        await lazy.LoginBreaches.getPotentialBreachesByLoginGUID(logins);
    }

    return {
      numLogins: userFacingLogins,
      potentiallyBreachedLogins: potentiallyBreachedLogins
        ? potentiallyBreachedLogins.size
        : 0,
      mobileDeviceConnected: false,
    };
  }

  async receiveMessage(aMessage) {
    let win = this.browsingContext.top.embedderElement.documentGlobal;
    switch (aMessage.name) {
      case "OpenAboutLogins":
        lazy.LoginHelper.openPasswordManager(win, {
          entryPoint: "Aboutprotections",
        });
        break;
      case "OpenContentBlockingPreferences":
        win.openPreferences("privacy-trackingprotection", {
          origin: "about-protections",
        });
        break;
      case "FetchContentBlockingEvents": {
        let dataToSend = {};
        let displayNames = new Services.intl.DisplayNames(undefined, {
          type: "weekday",
          style: "abbreviated",
          calendar: "gregory",
        });

        // Weekdays starting Sunday (7) to Saturday (6).
        let weekdays = [7, 1, 2, 3, 4, 5, 6].map(day => displayNames.of(day));
        dataToSend.weekdays = weekdays;

        if (lazy.PrivateBrowsingUtils.isWindowPrivate(win)) {
          dataToSend.isPrivate = true;
          return dataToSend;
        }
        let sumEvents = await lazy.TrackingDBService.sumAllEvents();
        let earliestDate =
          await lazy.TrackingDBService.getEarliestRecordedDate();
        let eventsByDate = await lazy.TrackingDBService.getEventsByDateRange(
          aMessage.data.from,
          aMessage.data.to
        );
        let largest = 0;

        for (let result of eventsByDate) {
          let count = result.getResultByName("count");
          let type = result.getResultByName("type");
          let timestamp = result.getResultByName("timestamp");
          let typeStr = idToTextMap.get(type);
          dataToSend[timestamp] = dataToSend[timestamp] ?? { total: 0 };
          let currentCnt = dataToSend[timestamp][typeStr] ?? 0;
          currentCnt += count;
          dataToSend[timestamp][typeStr] = currentCnt;
          dataToSend[timestamp].total += count;
          // Record the largest amount of tracking events found per day,
          // to create the tallest column on the graph and compare other days to.
          if (largest < dataToSend[timestamp].total) {
            largest = dataToSend[timestamp].total;
          }
        }
        dataToSend.largest = largest;
        dataToSend.earliestDate = earliestDate;
        dataToSend.sumEvents = sumEvents;

        return dataToSend;
      }

      case "FetchUserLoginsData":
        return this.getLoginData();

      case "RecordEntryPoint":
        entrypoint = aMessage.data.entrypoint;
        break;

      case "FetchEntryPoint":
        return entrypoint;

      case "FetchShowVPNCard":
        return lazy.BrowserUtils.shouldShowVPNPromo();

      case "FetchPrivacyMetrics":
        if (lazy.PrivateBrowsingUtils.isWindowPrivate(win)) {
          return { isPrivate: true };
        }
        return lazy.PrivacyMetricsService.getWeeklyStats();
    }

    return undefined;
  }
}
